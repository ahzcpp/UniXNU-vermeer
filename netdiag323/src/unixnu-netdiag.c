/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-netdiag: after Wi-Fi association, obtain IPv4 by DHCP (only if nobody else does), install address and default route,
 * and prove real connectivity: DNS answer from the lease's server, TCP connect, HTTP status line. libSystem only.
 *
 * It never reports a step as passed without the evidence for it: every line is PASS/FAIL/SKIP with the observed value or errno,
 * and the final line says which step failed. It never prints or reads Wi-Fi credentials.
 *
 * modes
 *   observe            read only: interface flags/MAC/IPv4, default route, whether UDP port 68 is free, system resolver,
 *                      and DNS/TCP/HTTP if the interface already has an address. Changes nothing.
 *   dhcp               wait for en0 IFF_UP|IFF_RUNNING, then: if en0 already has IPv4 (someone else configured it) observe only;
 *                      if UDP port 68 is taken (configd's IPConfiguration runs DHCP) observe only; otherwise DHCP DISCOVER/
 *                      OFFER/REQUEST/ACK (broadcast flag set, so replies arrive without an address), SIOCAIFADDR, RTM_ADD
 *                      default route, then the same checks.
 *   dhcp --hold        as dhcp, then stay running and renew at T1 (broadcast REQUEST); on lease expiry remove what it added.
 * options: -i IF (en0)  -w SECONDS wait for link (120)  -H HOST (captive.apple.com)  -P PATH (/hotspot-detect.html)
 *          -l LOGFILE (/private/var/ngpu/log/unixnu-netdiag.log)  -c  also copy each line to /dev/console
 *          -D A.B.C.D  an extra DNS server tried after the lease's (diagnostic; the system resolver is not changed)
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include "netdiag_route.h"
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "netdiag_core.h"

static FILE *g_log;
static struct in_addr g_extra_dns;  /* -D: an additional DNS server to try after the lease's (diagnostics only) */
static int g_console = -1;
static const char *g_if = "en0";

static void say(const char *fmt, ...)
{
    char line[600];
    struct timeval tv; gettimeofday(&tv, NULL);
    int n = snprintf(line, sizeof line, "NETDIAG %ld.%03d ", (long)tv.tv_sec, (int)(tv.tv_usec / 1000));
    va_list ap; va_start(ap, fmt);
    if (n > 0 && n < (int)sizeof line) n += vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof line - 1) n = (int)sizeof line - 2;
    line[n++] = '\n'; line[n] = 0;
    fputs(line, stdout); fflush(stdout);
    if (g_log) {fputs(line, g_log); fflush(g_log);}
    if (g_console >= 0) (void)!write(g_console, line, (size_t)n);
}

static uint64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ---------------- interface state ---------------- */
typedef struct {int present; unsigned flags; unsigned index; uint8_t mac[6]; int have_mac; struct in_addr ip, mask; int have_ip;} IfState;

static void if_state(IfState *s)
{
    memset(s, 0, sizeof *s);
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list)) return;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_name || strcmp(a->ifa_name, g_if)) continue;
        s->present = 1; s->flags = a->ifa_flags;
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_LINK) {
            const struct sockaddr_dl *dl = (const struct sockaddr_dl *)a->ifa_addr;
            s->index = dl->sdl_index;
            if (dl->sdl_alen == 6) {memcpy(s->mac, LLADDR(dl), 6); s->have_mac = 1;}
        } else if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && !s->have_ip) {
            s->ip = ((const struct sockaddr_in *)a->ifa_addr)->sin_addr;
            if (a->ifa_netmask) s->mask = ((const struct sockaddr_in *)a->ifa_netmask)->sin_addr;
            s->have_ip = 1;
        }
    }
    freeifaddrs(list);
    if (!s->index) s->index = if_nametoindex(g_if);
}

static void report_if(const IfState *s, const char *when)
{
    if (!s->present) {say("FAIL interface %s absent (%s)", g_if, when); return;}
    char ip[32] = "-", mask[32] = "-";
    if (s->have_ip) {inet_ntop(AF_INET, &s->ip, ip, sizeof ip); inet_ntop(AF_INET, &s->mask, mask, sizeof mask);}
    say("INFO if=%s when=%s index=%u flags=0x%x up=%d running=%d mac=%s ipv4=%s mask=%s", g_if, when, s->index, s->flags,
        !!(s->flags & IFF_UP), !!(s->flags & IFF_RUNNING), s->have_mac ? "present" : "absent", ip, mask);
}

/* default route from the kernel's table (sysctl NET_RT_DUMP); returns 1 and gateway/ifindex if one exists */
static int default_route(struct in_addr *gw, unsigned *ifindex)
{
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0};
    size_t need = 0;
    if (sysctl(mib, 6, NULL, &need, NULL, 0) || !need) return 0;
    need += 4096;
    char *buf = malloc(need);
    if (!buf) return 0;
    int found = 0;
    if (!sysctl(mib, 6, buf, &need, NULL, 0)) {
        for (char *p = buf; p + sizeof(struct rt_msghdr) <= buf + need && !found;) {
            const struct rt_msghdr *rtm = (const struct rt_msghdr *)p;
            if (rtm->rtm_msglen < sizeof *rtm || p + rtm->rtm_msglen > buf + need) break;
            struct sockaddr *sa[RTAX_MAX] = {0};
            char *q = p + sizeof *rtm;
            for (int i = 0; i < RTAX_MAX; ++i) {
                if (!(rtm->rtm_addrs & (1 << i))) continue;
                struct sockaddr *x = (struct sockaddr *)q;
                sa[i] = x;
                q += x->sa_len ? (1 + ((x->sa_len - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t);
            }
            const struct sockaddr_in *dst = (const struct sockaddr_in *)sa[RTAX_DST];
            const struct sockaddr_in *mask = (const struct sockaddr_in *)sa[RTAX_NETMASK];
            if ((rtm->rtm_flags & RTF_GATEWAY) && dst && dst->sin_family == AF_INET && dst->sin_addr.s_addr == 0 &&
                (!mask || mask->sin_len == 0 || mask->sin_addr.s_addr == 0) && !(rtm->rtm_flags & RTF_IFSCOPE) &&
                sa[RTAX_GATEWAY] && sa[RTAX_GATEWAY]->sa_family == AF_INET) {
                *gw = ((const struct sockaddr_in *)sa[RTAX_GATEWAY])->sin_addr;
                *ifindex = rtm->rtm_index;
                found = 1;
            }
            p += rtm->rtm_msglen;
        }
    }
    free(buf);
    return found;
}

static void report_route(const char *when)
{
    struct in_addr gw; unsigned idx = 0;
    if (default_route(&gw, &idx)) {
        char g[32], name[IF_NAMESIZE] = "?";
        inet_ntop(AF_INET, &gw, g, sizeof g); if_indextoname(idx, name);
        say("INFO default-route when=%s gateway=%s if=%s", when, g, name);
    } else {
        say("INFO default-route when=%s none", when);
    }
}

/* ---------------- DHCP over a raw send socket and a UDP receive socket ---------------- */
typedef struct {int tx, rx; int host_order;} Dhcp;

static int dhcp_open(Dhcp *d, unsigned ifindex)
{
    int one = 1;
    d->host_order = 1;  /* Darwin raw IP_HDRINCL takes ip_len/ip_off in host order; switched once on EINVAL */
    d->rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (d->rx < 0) {say("FAIL dhcp udp socket errno=%d", errno); return -1;}
    setsockopt(d->rx, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(d->rx, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    if (setsockopt(d->rx, IPPROTO_IP, IP_BOUND_IF, &ifindex, sizeof ifindex)) say("WARN rx IP_BOUND_IF errno=%d", errno);
    struct sockaddr_in me = {.sin_len = sizeof me, .sin_family = AF_INET, .sin_port = htons(68)};
    if (bind(d->rx, (struct sockaddr *)&me, sizeof me)) {
        const int e = errno; close(d->rx); d->rx = -1;
        say("%s dhcp bind udp/68 errno=%d%s", e == EADDRINUSE ? "SKIP" : "FAIL", e,
            e == EADDRINUSE ? " (port taken: another DHCP client, most likely configd IPConfiguration, owns it)" : "");
        return e == EADDRINUSE ? 1 : -1;
    }
    d->tx = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (d->tx < 0) {say("FAIL dhcp raw socket errno=%d", errno); close(d->rx); return -1;}
    setsockopt(d->tx, IPPROTO_IP, IP_HDRINCL, &one, sizeof one);
    setsockopt(d->tx, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    if (setsockopt(d->tx, IPPROTO_IP, IP_BOUND_IF, &ifindex, sizeof ifindex)) {
        say("FAIL tx IP_BOUND_IF errno=%d", errno); close(d->tx); close(d->rx); return -1;
    }
    return 0;
}

static void dhcp_close(Dhcp *d) {if (d->tx >= 0) close(d->tx); if (d->rx >= 0) close(d->rx); d->tx = d->rx = -1;}

static int dhcp_send(Dhcp *d, const uint8_t *payload, size_t len)
{
    uint8_t frame[20 + 8 + ND_DHCP_MAX];
    struct in_addr src = {0}, dst = {htonl(INADDR_BROADCAST)};
    size_t n = nd_udp_ip_frame(frame, sizeof frame, src, dst, 68, 67, payload, len, d->host_order);
    if (!n) return -1;
    struct sockaddr_in to = {.sin_len = sizeof to, .sin_family = AF_INET, .sin_addr = dst};
    ssize_t r = sendto(d->tx, frame, n, 0, (struct sockaddr *)&to, sizeof to);
    if (r < 0 && errno == EINVAL && d->host_order) {
        d->host_order = 0;
        n = nd_udp_ip_frame(frame, sizeof frame, src, dst, 68, 67, payload, len, 0);
        r = sendto(d->tx, frame, n, 0, (struct sockaddr *)&to, sizeof to);
        if (r >= 0) say("INFO raw send needed network-order ip_len");
    }
    if (r != (ssize_t)n) {say("FAIL dhcp send errno=%d", errno); return -1;}
    return 0;
}

/* waits for a reply to xid of the wanted type; returns 1 with the lease filled, 0 timeout, -1 error */
static int dhcp_wait(Dhcp *d, uint32_t xid, const uint8_t mac[6], int want, NdLease *lease, unsigned ms)
{
    const uint64_t end = now_ms() + ms;
    for (;;) {
        const uint64_t t = now_ms();
        if (t >= end) return 0;
        struct pollfd p = {d->rx, POLLIN, 0};
        int r = poll(&p, 1, (int)(end - t));
        if (r < 0) {if (errno == EINTR) continue; return -1;}
        if (!r) return 0;
        uint8_t buf[1500];
        ssize_t n = recv(d->rx, buf, sizeof buf, 0);
        if (n <= 0) continue;
        NdLease l;
        const int type = nd_dhcp_parse(buf, (size_t)n, xid, mac, &l);
        if (type == ND_DHCPNAK && (want == ND_DHCPACK)) {say("FAIL dhcp NAK from server"); return -1;}
        if (type == want) {*lease = l; return 1;}
    }
}

static void ip4(char out[32], struct in_addr a) {inet_ntop(AF_INET, &a, out, 32);}

static void report_lease(const char *what, const NdLease *l)
{
    char a[32], m[32], g[32], s[32], d0[32], d1[32];
    ip4(a, l->yiaddr); ip4(m, l->mask); ip4(g, l->router); ip4(s, l->server); ip4(d0, l->dns[0]); ip4(d1, l->dns[1]);
    say("PASS dhcp %s ip=%s mask=%s router=%s server=%s dns=%s,%s lease=%us t1=%us mtu=%u", what, a, m, g, s, d0,
        l->dns_count > 1 ? d1 : "-", l->lease_s, l->t1_s, l->mtu);
}

static int dhcp_acquire(Dhcp *d, const uint8_t mac[6], NdLease *out, int renew, const NdLease *old)
{
    uint8_t pkt[ND_DHCP_MAX];
    uint32_t xid = arc4random();
    NdLease offer;
    if (!renew) {
        int got = 0;
        for (unsigned attempt = 0; attempt < 5 && !got; ++attempt) {
            size_t n = nd_dhcp_build(pkt, sizeof pkt, ND_DHCPDISCOVER, xid, mac, NULL, NULL, (uint16_t)(attempt * 4));
            if (!n || dhcp_send(d, pkt, n)) return -1;
            say("INFO dhcp DISCOVER sent attempt=%u", attempt + 1);
            got = dhcp_wait(d, xid, mac, ND_DHCPOFFER, &offer, 2000u << attempt);
            if (got < 0) return -1;
        }
        if (!got) {say("FAIL dhcp no OFFER after 5 DISCOVERs (62 s); link up but no DHCP server answered / frames not delivered"); return -1;}
        report_lease("OFFER", &offer);
    } else {
        offer = *old;
    }
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        size_t n = nd_dhcp_build(pkt, sizeof pkt, ND_DHCPREQUEST, xid, mac, &offer.yiaddr, renew ? NULL : &offer.server, 0);
        if (!n || dhcp_send(d, pkt, n)) return -1;
        say("INFO dhcp REQUEST sent attempt=%u%s", attempt + 1, renew ? " (renew)" : "");
        int got = dhcp_wait(d, xid, mac, ND_DHCPACK, out, 2000u << attempt);
        if (got < 0) return -1;
        if (got) {
            if (out->yiaddr.s_addr != offer.yiaddr.s_addr) {say("FAIL dhcp ACK for another address"); return -1;}
            report_lease("ACK", out);
            return 0;
        }
    }
    say("FAIL dhcp no ACK after 4 REQUESTs");
    return -1;
}

/* ---------------- address and route ---------------- */
static int set_address(const NdLease *l)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    struct ifaliasreq r;
    memset(&r, 0, sizeof r);
    strlcpy(r.ifra_name, g_if, sizeof r.ifra_name);
    struct sockaddr_in *a = (struct sockaddr_in *)&r.ifra_addr, *m = (struct sockaddr_in *)&r.ifra_mask, *b = (struct sockaddr_in *)&r.ifra_broadaddr;
    a->sin_len = m->sin_len = b->sin_len = sizeof *a;
    a->sin_family = m->sin_family = b->sin_family = AF_INET;
    a->sin_addr = l->yiaddr; m->sin_addr = l->mask;
    b->sin_addr.s_addr = l->yiaddr.s_addr | ~l->mask.s_addr;
    const int rc = ioctl(s, SIOCAIFADDR, &r);
    const int e = errno;
    close(s);
    if (rc) {say("FAIL SIOCAIFADDR errno=%d", e); return -1;}
    say("PASS address installed");
    return 0;
}

static int del_address(const NdLease *l)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    struct ifreq r; memset(&r, 0, sizeof r);
    strlcpy(r.ifr_name, g_if, sizeof r.ifr_name);
    struct sockaddr_in *a = (struct sockaddr_in *)&r.ifr_addr;
    a->sin_len = sizeof *a; a->sin_family = AF_INET; a->sin_addr = l->yiaddr;
    const int rc = ioctl(s, SIOCDIFADDR, &r);
    close(s);
    say("%s address removed", rc ? "FAIL" : "PASS");
    return rc;
}

static int route_msg(int type, struct in_addr gw)
{
    int s = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (s < 0) {say("FAIL routing socket errno=%d", errno); return -1;}
    struct {struct rt_msghdr h; struct sockaddr_in dst, gw, mask;} m;
    memset(&m, 0, sizeof m);
    m.h.rtm_msglen = sizeof m; m.h.rtm_version = RTM_VERSION; m.h.rtm_type = (u_char)type;
    m.h.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC; m.h.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    m.h.rtm_seq = 1; m.h.rtm_pid = getpid();
    m.dst.sin_len = m.gw.sin_len = m.mask.sin_len = sizeof m.dst;
    m.dst.sin_family = m.gw.sin_family = m.mask.sin_family = AF_INET;
    m.gw.sin_addr = gw;
    const ssize_t r = write(s, &m, sizeof m);
    const int e = errno;
    close(s);
    if (r != (ssize_t)sizeof m) {say("%s route %s errno=%d", (type == RTM_ADD && e == EEXIST) ? "WARN" : "FAIL", type == RTM_ADD ? "add" : "delete", e); return -1;}
    say("PASS default route %s", type == RTM_ADD ? "added" : "deleted");
    return 0;
}

/* ---------------- DNS, TCP, HTTP ---------------- */
static int dns_query(struct in_addr server, const char *host, struct in_addr *answer)
{
    uint8_t q[512], a[1500];
    const uint16_t id = (uint16_t)arc4random();
    const size_t n = nd_dns_query(q, sizeof q, id, host);
    if (!n) {say("FAIL dns query build"); return -1;}
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return -1;
    struct sockaddr_in to = {.sin_len = sizeof to, .sin_family = AF_INET, .sin_port = htons(53), .sin_addr = server};
    char srv[32]; ip4(srv, server);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (sendto(s, q, n, 0, (struct sockaddr *)&to, sizeof to) != (ssize_t)n) {say("FAIL dns send to %s errno=%d", srv, errno); break;}
        struct pollfd p = {s, POLLIN, 0};
        if (poll(&p, 1, 2000) <= 0) continue;
        struct sockaddr_in from; socklen_t fl = sizeof from;
        const ssize_t r = recvfrom(s, a, sizeof a, 0, (struct sockaddr *)&from, &fl);
        if (r <= 0 || from.sin_addr.s_addr != server.s_addr) continue;
        const int rc = nd_dns_answer(a, (size_t)r, id, answer);
        if (rc == 1) {char ip[32]; ip4(ip, *answer); close(s); say("PASS dns server=%s %s A=%s", srv, host, ip); return 0;}
        say("FAIL dns server=%s answered without an A record (code %d)", srv, rc);
        close(s); return -1;
    }
    close(s);
    say("FAIL dns server=%s no answer in 6 s", srv);
    return -1;
}

static int http_get(struct in_addr ip, const char *host, const char *path)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    struct sockaddr_in to = {.sin_len = sizeof to, .sin_family = AF_INET, .sin_port = htons(80), .sin_addr = ip};
    char a[32]; ip4(a, ip);
    const uint64_t t0 = now_ms();
    if (connect(s, (struct sockaddr *)&to, sizeof to) && errno != EINPROGRESS) {say("FAIL tcp connect %s:80 errno=%d", a, errno); close(s); return -1;}
    struct pollfd p = {s, POLLOUT, 0};
    int err = 0; socklen_t el = sizeof err;
    if (poll(&p, 1, 8000) <= 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el) || err) {
        say("FAIL tcp connect %s:80 %s", a, err ? strerror(err) : "timeout 8 s"); close(s); return -1;
    }
    say("PASS tcp connect %s:80 in %llu ms", a, (unsigned long long)(now_ms() - t0));
    char req[512];
    const int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: unixnu-netdiag\r\nConnection: close\r\n\r\n", path, host);
    if (n <= 0 || n >= (int)sizeof req || write(s, req, (size_t)n) != n) {say("FAIL http write errno=%d", errno); close(s); return -1;}
    char resp[2048]; size_t got = 0;
    const uint64_t end = now_ms() + 8000;
    while (got < sizeof resp - 1 && now_ms() < end) {
        struct pollfd q = {s, POLLIN, 0};
        if (poll(&q, 1, (int)(end - now_ms())) <= 0) break;
        const ssize_t r = read(s, resp + got, sizeof resp - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(s);
    resp[got] = 0;
    NdHttp h;
    if (!nd_http_parse(resp, got, &h)) {say("FAIL http no valid status line (%zu bytes received)", got); return -1;}
    say("%s http status=%d bytes=%zu success_page=%d%s", h.status == 200 ? "PASS" : "FAIL", h.status, got, h.apple_success,
        h.status == 200 && !h.apple_success && !strcmp(host, "captive.apple.com") ? " (200 but not the expected page: captive portal?)" : "");
    return h.status == 200 ? 0 : -1;
}

/* the system resolver (mDNSResponder, fed by configd) in a child with a deadline: shows whether apps would resolve */
static void system_resolver(const char *host)
{
    pid_t pid = fork();
    if (pid == 0) {
        alarm(10);
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
        const int rc = getaddrinfo(host, "80", &hints, &res);
        if (rc == 0 && res) {char ip[32]; ip4(ip, ((struct sockaddr_in *)res->ai_addr)->sin_addr); say("PASS system-resolver %s A=%s", host, ip); _exit(0);}
        say("FAIL system-resolver %s rc=%d (getaddrinfo; DNS is not configured in configd/mDNSResponder)", host, rc);
        _exit(1);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {say("FAIL system-resolver fork"); return;}
    if (WIFSIGNALED(status)) say("FAIL system-resolver %s no answer in 10 s", host);
}

/* ---------------- main ---------------- */
static volatile sig_atomic_t g_stop;
static void on_term(int s) {(void)s; g_stop = 1;}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "observe", *host = "captive.apple.com", *path = "/hotspot-detect.html";
    const char *logfile = "/private/var/ngpu/log/unixnu-netdiag.log";
    int hold = 0; unsigned wait_s = 120;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--hold")) hold = 1;
        else if (!strcmp(argv[i], "-c")) g_console = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);
        else if (i + 1 < argc && !strcmp(argv[i], "-i")) g_if = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "-w")) wait_s = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "-H")) host = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "-P")) path = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "-l")) logfile = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "-D")) {if (inet_pton(AF_INET, argv[++i], &g_extra_dns) != 1) return 64;}
        else {fprintf(stderr, "usage: unixnu-netdiag observe|dhcp [--hold] [-i IF] [-w S] [-H HOST] [-P PATH] [-l LOG] [-D DNS] [-c]\n"); return 64;}
    }
    if (strcmp(mode, "observe") && strcmp(mode, "dhcp")) {fprintf(stderr, "unknown mode\n"); return 64;}
    mkdir("/private/var/ngpu/log", 0700);
    g_log = fopen(logfile, "a");
    signal(SIGTERM, on_term); signal(SIGINT, on_term); signal(SIGPIPE, SIG_IGN);
    say("START mode=%s hold=%d if=%s uid=%d version=323", mode, hold, g_if, (int)getuid());

    IfState st;
    if_state(&st);
    report_if(&st, "start");
    report_route("start");
    if (!strcmp(mode, "dhcp")) {
        const uint64_t end = now_ms() + (uint64_t)wait_s * 1000u;
        while (!g_stop && (!st.present || (st.flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING)) && now_ms() < end) {
            sleep(1); if_state(&st);
        }
        report_if(&st, "link-wait-end");
        if (!st.present || (st.flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING)) {
            say("RESULT fail step=link (interface not up+running after %u s; association not complete)", wait_s); return 2;
        }
    }

    NdLease lease; memset(&lease, 0, sizeof lease);
    int ours = 0;  /* address and route installed by this process */
    if (!strcmp(mode, "dhcp") && !st.have_ip) {
        if (!st.have_mac) {say("RESULT fail step=mac (no link-layer address on %s)", g_if); return 2;}
        Dhcp d = {-1, -1, 1};
        const int o = dhcp_open(&d, st.index);
        if (o < 0) {say("RESULT fail step=dhcp-socket"); return 3;}
        if (o == 0) {
            /* with --hold, a failed exchange is tried again (15 s apart) until the -w budget from start is used up */
            const uint64_t budget_end = now_ms() + (uint64_t)wait_s * 1000u;
            int acquired = dhcp_acquire(&d, st.mac, &lease, 0, NULL) == 0;
            while (!acquired && hold && !g_stop && now_ms() + 15000 < budget_end) {
                say("INFO dhcp retry in 15 s (hold)");
                sleep(15);
                if_state(&st);
                if ((st.flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING)) {say("INFO link not running; waiting"); continue;}
                acquired = dhcp_acquire(&d, st.mac, &lease, 0, NULL) == 0;
            }
            if (!acquired) {dhcp_close(&d); say("RESULT fail step=dhcp"); return 3;}
            if (set_address(&lease)) {dhcp_close(&d); say("RESULT fail step=address"); return 4;}
            struct in_addr gw; unsigned idx;
            if (lease.router.s_addr && !default_route(&gw, &idx)) route_msg(RTM_ADD, lease.router);
            else if (lease.router.s_addr) say("WARN a default route already exists: not replaced");
            ours = 1;
        } else {
            say("INFO observing the other DHCP client for up to 30 s");
            for (int i = 0; i < 30 && !g_stop; ++i) {if_state(&st); if (st.have_ip) break; sleep(1);}
        }
        dhcp_close(&d);
    }
    if_state(&st);
    report_if(&st, "after-config");
    report_route("after-config");
    if (!st.have_ip) {say("RESULT fail step=ipv4 (no IPv4 address on %s)", g_if); return 4;}

    struct in_addr gw; unsigned gwif = 0;
    const int have_gw = default_route(&gw, &gwif);
    if (!have_gw) say("FAIL no default route");
    else if (gwif != st.index) say("WARN default route is on another interface (index %u)", gwif);

    /* DNS: the lease's servers when we have them; otherwise the gateway (common home routers forward DNS) */
    struct in_addr servers[3]; int ns = 0;
    for (unsigned i = 0; i < lease.dns_count && ns < 2; ++i) servers[ns++] = lease.dns[i];
    if (!ns && have_gw) {servers[ns++] = gw; say("INFO no DNS server from our own lease: trying the gateway as a resolver");}
    if (g_extra_dns.s_addr) servers[ns++] = g_extra_dns;
    struct in_addr target = {0}; int dns_ok = 0;
    for (int i = 0; i < ns && !dns_ok; ++i) dns_ok = dns_query(servers[i], host, &target) == 0;
    system_resolver(host);
    int http_ok = 0;
    if (dns_ok) http_ok = http_get(target, host, path) == 0;
    char ip[32], g[32] = "-";
    ip4(ip, st.ip); if (have_gw) ip4(g, gw);
    if (have_gw && dns_ok && http_ok) say("RESULT pass ip=%s gateway=%s dns=yes http=200 dhcp_by=%s", ip, g, ours ? "netdiag" : "other");
    else say("RESULT fail step=%s ip=%s gateway=%s", !have_gw ? "route" : !dns_ok ? "dns" : "http", ip, g);

    if (!hold || !ours) return (have_gw && dns_ok && http_ok) ? 0 : 5;

    /* --hold: renew at T1; give up and remove what we added at expiry */
    uint64_t got_at = now_ms();
    for (;;) {
        const unsigned t1 = lease.t1_s ? lease.t1_s : (lease.lease_s ? lease.lease_s / 2 : 1800);
        while (!g_stop && now_ms() < got_at + (uint64_t)t1 * 1000u) sleep(5);
        if (g_stop) break;
        Dhcp d = {-1, -1, 1};
        NdLease fresh;
        if (dhcp_open(&d, st.index) == 0 && dhcp_acquire(&d, st.mac, &fresh, 1, &lease) == 0) {lease = fresh; got_at = now_ms(); dhcp_close(&d); continue;}
        dhcp_close(&d);
        if (lease.lease_s && now_ms() >= got_at + (uint64_t)lease.lease_s * 1000u) {say("FAIL lease expired without renewal"); break;}
        sleep(30);
    }
    if (lease.router.s_addr) route_msg(RTM_DELETE, lease.router);
    del_address(&lease);
    say("STOP");
    return 0;
}
