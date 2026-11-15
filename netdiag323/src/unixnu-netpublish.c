/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-netpublish: make the IPv4 configuration that unixnu-netdiag obtained visible to iOS applications.
 *
 * It publishes ONLY under its own service ID, ONLY in the dynamic store (not SCPreferences: nothing persistent), with configd's
 * public schema (configd/IPMonitor read these, apps never do):
 *   State:/Network/Service/<own UUID>/IPv4 = {Addresses=[ip], SubnetMasks=[mask], Router=gw, InterfaceName=en0, ConfigMethod=DHCP}
 *   State:/Network/Service/<own UUID>/DNS  = {ServerAddresses=[dns...], InterfaceName=en0}
 * configd's IPMonitor then ranks services and itself writes State:/Network/Global/IPv4 and Global/DNS and the resolver
 * configuration mDNSResponder serves to apps. This tool never writes a Global key and never touches another service.
 *
 * It publishes nothing it has not verified: the lease file's address must be the address the kernel reports on that interface, its
 * router must be the kernel's default route on that interface. When either stops being true (or the lease file goes) it removes its
 * keys. After publishing it reads back IPMonitor's Global keys and resolves a name with getaddrinfo (the apps' resolver) and fetches
 * HTTP with the resolved address: only that is reported as "app-level DNS PASS".
 *
 * modes: run (default: loop every 5 s)   once   dry-run FILE (parse a lease file, build the dictionaries, print them, publish nothing)
 *        list (read only: SCNetworkInterfaceCopyAll and the Global keys)
 * optional: run|once|list -c mirrors existing rows to a nonblocking console; stdout/file unchanged
 */
#include "netpublish_sc.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include "netdiag_route.h"
#include "netpublish_lease.h"
#include "netpublish_console.h"

#define SERVICE_ID "A7C30323-4E45-5444-4941-470000000323"
#define LEASE_FILE "/private/var/ngpu/etc/wifi/netdiag.lease"
static FILE *g_log;

static void say(const char *fmt, ...)
{
    char line[700]; struct timeval tv; gettimeofday(&tv, NULL);
    int n = snprintf(line, sizeof line, "NETPUB %ld.%03d ", (long)tv.tv_sec, (int)(tv.tv_usec / 1000));
    va_list ap; va_start(ap, fmt); if (n > 0) n += vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap); va_end(ap);
    if (n < 0) return; if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n'; line[n] = 0;
    fputs(line, stdout); fflush(stdout);
    if (g_log) {fputs(line, g_log); fflush(g_log);}
    np_console_emit(line,(size_t)n);
}

static CFStringRef cfs(const char *s) {return CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);}
static CFArrayRef cfarray1(const char *s) {CFStringRef v = cfs(s); CFArrayRef a = CFArrayCreate(NULL, (const void **)&v, 1, &kCFTypeArrayCallBacks); CFRelease(v); return a;}

/* the two dictionaries for a verified lease */
static void build(const NpLease *l, CFDictionaryRef *ipv4, CFDictionaryRef *dns)
{
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef a = cfarray1(l->ip), m = cfarray1(l->mask); CFStringRef ifn = cfs(l->ifname), method = cfs("DHCP");
    CFDictionarySetValue(d, NP_PROP_ADDRESSES, a); CFDictionarySetValue(d, NP_PROP_SUBNET_MASKS, m);
    CFDictionarySetValue(d, NP_PROP_INTERFACE_NAME, ifn); CFDictionarySetValue(d, NP_PROP_CONFIG_METHOD, method);
    if (l->router[0]) {CFStringRef r = cfs(l->router); CFDictionarySetValue(d, NP_PROP_ROUTER, r); CFRelease(r);}
    CFRelease(a); CFRelease(m); CFRelease(method);
    *ipv4 = d; *dns = NULL;
    if (l->dns_count) {
        CFMutableArrayRef servers = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
        for (unsigned i = 0; i < l->dns_count; ++i) {CFStringRef s = cfs(l->dns[i]); CFArrayAppendValue(servers, s); CFRelease(s);}
        CFMutableDictionaryRef n = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(n, NP_PROP_SERVER_ADDRESSES, servers); CFDictionarySetValue(n, NP_PROP_INTERFACE_NAME, ifn);
        CFRelease(servers); *dns = n;
    }
    CFRelease(ifn);
}

static void show(const char *what, CFTypeRef v)
{
    if (!v) {say("INFO %s = (none)", what); return;}
    CFStringRef desc = CFCopyDescription(v); char buf[600] = "";
    CFStringGetCString(desc, buf, sizeof buf, kCFStringEncodingUTF8); CFRelease(desc);
    for (char *p = buf; *p; ++p) if (*p == '\n' || *p == '\t') *p = ' ';
    say("INFO %s = %s", what, buf);
}

/* verification against the kernel: the interface has this IPv4 and the default route is this router on this interface */
static int kernel_agrees(const NpLease *l)
{
    struct in_addr want; if (inet_pton(AF_INET, l->ip, &want) != 1) return 0;
    int have = 0; struct ifaddrs *list = NULL;
    if (!getifaddrs(&list)) {
        for (struct ifaddrs *a = list; a; a = a->ifa_next)
            if (a->ifa_name && !strcmp(a->ifa_name, l->ifname) && a->ifa_addr && a->ifa_addr->sa_family == AF_INET &&
                ((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr == want.s_addr && (a->ifa_flags & IFF_RUNNING)) have = 1;
        freeifaddrs(list);
    }
    if (!have) {say("WARN kernel: %s does not carry %s (or is not running)", l->ifname, l->ip); return 0;}
    if (!l->router[0]) return 1;
    struct in_addr gw; inet_pton(AF_INET, l->router, &gw);
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0}; size_t need = 0; int ok = 0;
    if (sysctl(mib, 6, NULL, &need, NULL, 0) || !need) return 0;
    need += 4096; char *buf = malloc(need); if (!buf) return 0;
    const unsigned index = if_nametoindex(l->ifname);
    if (!sysctl(mib, 6, buf, &need, NULL, 0)) {
        for (char *p = buf; p + sizeof(struct rt_msghdr) <= buf + need && !ok;) {
            const struct rt_msghdr *rtm = (const struct rt_msghdr *)p;
            if (rtm->rtm_msglen < sizeof *rtm || p + rtm->rtm_msglen > buf + need) break;
            struct sockaddr *sa[RTAX_MAX] = {0}; char *q = p + sizeof *rtm;
            for (int i = 0; i < RTAX_MAX; ++i) if (rtm->rtm_addrs & (1 << i)) {struct sockaddr *x = (struct sockaddr *)q; sa[i] = x;
                q += x->sa_len ? (1 + ((x->sa_len - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t);}
            const struct sockaddr_in *dst = (const struct sockaddr_in *)sa[RTAX_DST], *g = (const struct sockaddr_in *)sa[RTAX_GATEWAY];
            if ((rtm->rtm_flags & RTF_GATEWAY) && dst && dst->sin_family == AF_INET && !dst->sin_addr.s_addr && g && g->sin_family == AF_INET &&
                g->sin_addr.s_addr == gw.s_addr && rtm->rtm_index == index) ok = 1;
            p += rtm->rtm_msglen;
        }
    }
    free(buf);
    if (!ok) say("WARN kernel: no default route via %s on %s", l->router, l->ifname);
    return ok;
}

/* the applications' resolver and an HTTP fetch with its answer, in a child with a deadline */
static int app_check(const char *host)
{
    pid_t pid = fork();
    if (pid == 0) {
        alarm(15);
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
        int rc = getaddrinfo(host, "80", &hints, &res);
        if (rc || !res) {say("FAIL app-dns getaddrinfo(%s) rc=%d", host, rc); _exit(2);}
        char ip[32]; inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, sizeof ip);
        say("PASS app-dns getaddrinfo(%s) A=%s (system resolver)", host, ip);
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0 || connect(s, res->ai_addr, res->ai_addrlen)) {say("FAIL app-http connect %s errno=%d", ip, errno); _exit(3);}
        char req[256]; int n = snprintf(req, sizeof req, "GET /hotspot-detect.html HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
        if (write(s, req, (size_t)n) != n) _exit(3);
        char resp[1024]; size_t got = 0; ssize_t r;
        while (got < sizeof resp - 1 && (r = read(s, resp + got, sizeof resp - 1 - got)) > 0) got += (size_t)r;
        resp[got] = 0;
        const int status = got > 12 && !memcmp(resp, "HTTP/1.", 7) ? atoi(resp + 9) : 0;
        const int success = strstr(resp, "<TITLE>Success</TITLE>") != NULL;
        say("%s app-http status=%d success_page=%d", status == 200 && success ? "PASS" : "FAIL", status, success);
        _exit(status == 200 && success ? 0 : 4);
    }
    int st = 0; if (pid < 0 || waitpid(pid, &st, 0) < 0) return -1;
    if (WIFSIGNALED(st)) {say("FAIL app-dns no answer in 15 s"); return -1;}
    return WEXITSTATUS(st) == 0 ? 0 : -1;
}

static void list_interfaces(void)
{
    CFArrayRef all = SCNetworkInterfaceCopyAll();
    CFIndex n = all ? CFArrayGetCount(all) : 0; int en0 = 0;
    for (CFIndex i = 0; i < n; ++i) {
        SCNetworkInterfaceRef x = (SCNetworkInterfaceRef)CFArrayGetValueAtIndex(all, i);
        CFStringRef bsd = SCNetworkInterfaceGetBSDName(x), type = SCNetworkInterfaceGetInterfaceType(x);
        char b[32] = "-", t[64] = "-";
        if (bsd) CFStringGetCString(bsd, b, sizeof b, kCFStringEncodingUTF8);
        if (type) CFStringGetCString(type, t, sizeof t, kCFStringEncodingUTF8);
        if (!strcmp(b, "en0")) en0 = 1;
        say("INFO SCNetworkInterface bsd=%s type=%s", b, t);
    }
    /* Not needed for the dynamic-store path; reported because a SCPreferences service could only be built on a visible interface. */
    say("%s SCNetworkInterfaceCopyAll count=%ld en0_visible=%d%s", en0 ? "INFO" : "WARN", (long)n, en0,
        en0 ? "" : " (classic ifnet not enumerated by SystemConfiguration: no SCPreferences service possible; State keys still work)");
    if (all) CFRelease(all);
}

static volatile sig_atomic_t g_stop;
static void on_term(int s) {(void)s; g_stop = 1;}

static void report_namer_state(SCDynamicStoreRef store)
{
    CFPropertyListRef state = SCDynamicStoreCopyValue(store, CFSTR("Plugin:InterfaceNamer"));
    int dictionary = state && CFGetTypeID(state) == CFDictionaryGetTypeID();
    say("INFO namer present=%d dictionary=%d quiet_key=%d complete_key=%d timeout_key=%d",
        state != NULL, dictionary,
        dictionary && CFDictionaryContainsKey(state, CFSTR("Quiet")),
        dictionary && CFDictionaryContainsKey(state, CFSTR("Complete")),
        dictionary && CFDictionaryContainsKey(state, CFSTR("Timeout")));
    if (state) CFRelease(state);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "run";
    signal(SIGTERM, on_term); signal(SIGINT, on_term); signal(SIGPIPE, SIG_IGN);
    int console_error=0;
    /* Mode and dry-run FILE retain their original positions. Other arguments
     * keep their existing handling; -c only enables an optional log mirror. */
    for(int i=!strcmp(mode,"dry-run")?3:2;i<argc;i++) {
        if(!strcmp(argv[i],"-c")&&np_console_enable())console_error=errno;
    }
    if (!strcmp(mode, "dry-run")) {
        if (argc < 3) return 64;
        NpLease l; char text[512]; FILE *f = fopen(argv[2], "r"); size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0; if (f) fclose(f); text[n] = 0;
        if (np_lease_parse(text, n, &l)) {say("FAIL dry-run lease rejected"); return 2;}
        CFDictionaryRef v4, dns; build(&l, &v4, &dns);
        show("would-set State:/Network/Service/" SERVICE_ID "/IPv4", v4); show("would-set State:/Network/Service/" SERVICE_ID "/DNS", dns);
        CFRelease(v4); if (dns) CFRelease(dns);
        return 0;
    }
    mkdir("/private/var/ngpu/log", 0700);
    g_log = fopen("/private/var/ngpu/log/unixnu-netpublish.log", "a");
    say("START mode=%s service=%s uid=%d version=323b", mode, SERVICE_ID, (int)getuid());
    if(console_error)say("WARN optional console open errno=%d (stdout/file retained)",console_error);
    /* Enumeration waits for InterfaceNamer and is only diagnostic. The
     * publication path independently validates its real kernel lease/route. */
    if (!strcmp(mode, "list")) {
        say("INFO interface-enumeration begin");
        list_interfaces();
        say("INFO interface-enumeration end");
    } else say("INFO interface-enumeration skipped for lease publication");
    SCDynamicStoreRef store = SCDynamicStoreCreate(NULL, CFSTR("org.unixnu.wifi.netpublish"), NULL, NULL);
    if (!store) {say("FAIL SCDynamicStoreCreate: %s", SCErrorString(SCError())); return 3;}
    report_namer_state(store);
    CFStringRef kv4 = NP_KEY_SERVICE_IPV4(SERVICE_ID), kdns = NP_KEY_SERVICE_DNS(SERVICE_ID), gv4 = NP_KEY_GLOBAL_IPV4, gdns = NP_KEY_GLOBAL_DNS;
    {CFPropertyListRef a = SCDynamicStoreCopyValue(store, gv4), b = SCDynamicStoreCopyValue(store, gdns);
     show("before Global/IPv4", a); show("before Global/DNS", b); if (a) CFRelease(a); if (b) CFRelease(b);}
    if (!strcmp(mode, "list")) return 0;
    if (strcmp(mode, "run") && strcmp(mode, "once")) return 64;
    char published[512] = ""; int app_ok = -1;
    for (;;) {
        NpLease l; char text[512] = ""; int valid = 0;
        int fd = open(LEASE_FILE, O_RDONLY);
        if (fd >= 0) {ssize_t n = read(fd, text, sizeof text - 1); close(fd); if (n > 0) {text[n] = 0; valid = !np_lease_parse(text, (size_t)n, &l);}}
        if (valid) valid = kernel_agrees(&l);
        if (valid && strcmp(text, published)) {
            CFDictionaryRef v4, dns; build(&l, &v4, &dns);
            CFMutableDictionaryRef set = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue(set, kv4, v4); if (dns) CFDictionarySetValue(set, kdns, dns);
            CFArrayRef remove = dns ? NULL : CFArrayCreate(NULL, (const void **)&kdns, 1, &kCFTypeArrayCallBacks);
            const Boolean ok = SCDynamicStoreSetMultiple(store, set, remove, NULL);
            say("%s publish State:/Network/Service/%s/IPv4%s ip=%s router=%s dns_count=%u%s%s", ok ? "PASS" : "FAIL", SERVICE_ID, dns ? "+DNS" : "",
                l.ip, l.router[0] ? l.router : "-", l.dns_count, ok ? "" : " error=", ok ? "" : SCErrorString(SCError()));
            CFRelease(set); CFRelease(v4); if (dns) CFRelease(dns); if (remove) CFRelease(remove);
            if (ok) {
                strlcpy(published, text, sizeof published);
                sleep(2);  /* IPMonitor reacts to the notification */
                CFPropertyListRef a = SCDynamicStoreCopyValue(store, gv4), b = SCDynamicStoreCopyValue(store, gdns);
                show("after Global/IPv4 (written by IPMonitor)", a); show("after Global/DNS (written by IPMonitor)", b);
                if (a) CFRelease(a); if (b) CFRelease(b);
                app_ok = app_check("captive.apple.com");
                say("RESULT %s app-level DNS+HTTP via the system resolver", app_ok == 0 ? "pass" : "fail");
            }
        } else if (!valid && published[0]) {
            CFStringRef keys[2] = {kv4, kdns}; CFArrayRef remove = CFArrayCreate(NULL, (const void **)keys, 2, &kCFTypeArrayCallBacks);
            const Boolean ok = SCDynamicStoreSetMultiple(store, NULL, remove, NULL);
            say("%s withdrew own service keys (lease gone or no longer matches the kernel)", ok ? "PASS" : "FAIL");
            CFRelease(remove); published[0] = 0;
        } else if (!valid && !strcmp(mode, "once")) {
            say("RESULT fail step=lease (no lease file, or it does not match the kernel's address/route)"); return 4;
        }
        if (!strcmp(mode, "once") && published[0]) return app_ok == 0 ? 0 : 5;
        for (int i = 0; i < 5 && !g_stop; ++i) sleep(1);
        if (g_stop) break;
    }
    if (published[0]) {CFStringRef keys[2] = {kv4, kdns}; CFArrayRef r = CFArrayCreate(NULL, (const void **)keys, 2, &kCFTypeArrayCallBacks);
        SCDynamicStoreSetMultiple(store, NULL, r, NULL); CFRelease(r); say("INFO withdrew own service keys on stop");}
    say("STOP");
    return 0;
}
