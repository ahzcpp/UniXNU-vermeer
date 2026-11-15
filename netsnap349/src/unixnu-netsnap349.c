/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-netsnap349: own, read-only, bounded snapshot of which processes hold network sockets / NECP clients and whether they
 * are busy or waiting, taken repeatedly while someone opens the App Store on the K70 iOS 16.7 port.
 *
 * Reads only (root): proc_listallpids, proc_name, proc_pidinfo(NS_PROC_PIDTASKINFO / NS_PROC_PIDLISTFDS),
 * proc_pidfdinfo(NS_PROC_PIDFDSOCKETINFO) and the read-only net.necp.* counters. Changes nothing (no sysctl writes, no signals,
 * no sockets of its own, no file writes besides stdout).
 *
 * Prints per snapshot: system totals, NECP policy/session counters, and for a fixed list of App-Store-related processes:
 * pid, CPU ms and Mach messages sent/received since the previous snapshot, TCP sockets by state, remote-port class
 * (443/80/5223/53/other), UDP (53/443/other), NECP client fds, channel fds. Never prints addresses, hostnames, paths,
 * arguments, environment or any payload.
 */
#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>
#include "netsnap_procinfo.h"

#define TAG "NETSNAP349"
#define LINE_MAX_BYTES 480
#define CONSOLE_MAX_ROWS 256
#define MAX_PIDS 4096
#define MAX_FDS 4096

static int g_console_fd = -1;
static unsigned g_console_rows;
static int g_console_proc;

static void console_enable(void) {
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    int fl = fcntl(fd, F_GETFL);
    if (fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode) || fl < 0 || !(fl & O_NONBLOCK)) { close(fd); return; }
    g_console_fd = fd;
}

/* console=1: also mirror this line (summary lines by default; fixed watched-process rows only with explicit -p), at most CONSOLE_MAX_ROWS, one non-blocking attempt each */
static void emit(int console, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void emit(int console, const char *fmt, ...) {
    char buf[LINE_MAX_BYTES];
    int p = snprintf(buf, sizeof buf, TAG " ");
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + p, sizeof buf - (size_t)p - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = (size_t)p + (size_t)n;
    if (len > sizeof buf - 2) len = sizeof buf - 2;
    buf[len++] = '\n';
    ssize_t w = write(STDOUT_FILENO, buf, len);
    (void)w;
    if (console && g_console_fd >= 0 && g_console_rows < (g_console_proc ? CONSOLE_MAX_ROWS : 64u)) {
        g_console_rows++;
        w = write(g_console_fd, buf, len);
        (void)w;
    }
}

static void on_alarm(int sig) {
    (void)sig;
    static const char m[] = TAG " RESULT result=stopped step=alarm (time limit reached)\n";
    ssize_t w = write(STDOUT_FILENO, m, sizeof m - 1);
    (void)w;
    _exit(124);
}

/* Fixed watch list (prefix match on proc_name). */
static const char *const k_watch[] = {
    "AppStore", "appstored", "appstorecomponen", "amsaccountsd", "amsengagementd", "itunesstored", "storekitd",
    "StoreKitUIService", "itunescloudd", "akd", "adid", "fairplayd.H2", "accountsd", "nsurlsessiond", "trustd",
    "mDNSResponder", "CommCenter", "symptomsd", "networkserviceproxy", "nesessionmanager", "nehelper", "apsd",
    "mobileactivationd", "configd", "cfprefsd", "SpringBoard",
};
#define NWATCH (sizeof k_watch / sizeof k_watch[0])

struct pstat {
    int pid;
    uint64_t cpu_abs, msg_sent, msg_recv;
    int seen;
};
static struct pstat g_prev[NWATCH];
static mach_timebase_info_data_t g_tb;

enum { PC_443, PC_80, PC_5223, PC_53, PC_OTHER, PC_N };
struct counts {
    int tcp_state[11];
    int tcp_port[PC_N];
    int udp, udp53, udp443;
    int necp, chan, sockets_other;
};

static int port_class(int port) {
    switch (port) {
    case 443: return PC_443;
    case 80: return PC_80;
    case 5223: return PC_5223;
    case 53: return PC_53;
    default: return PC_OTHER;
    }
}

static void scan_fds(int pid, struct counts *c) {
    static struct ns_proc_fdinfo fds[MAX_FDS];
    int bytes = proc_pidinfo(pid, NS_PROC_PIDLISTFDS, 0, fds, (int)sizeof fds);
    if (bytes <= 0) return;
    int n = bytes / (int)sizeof(struct ns_proc_fdinfo);
    for (int i = 0; i < n; i++) {
        if (fds[i].proc_fdtype == NS_PROX_FDTYPE_NETPOLICY) { c->necp++; continue; }
        if (fds[i].proc_fdtype == NS_PROX_FDTYPE_CHANNEL) { c->chan++; continue; }
        if (fds[i].proc_fdtype != NS_PROX_FDTYPE_SOCKET) continue;
        struct ns_socket_fdinfo si;
        memset(&si, 0, sizeof si);
        if (proc_pidfdinfo(pid, fds[i].proc_fd, NS_PROC_PIDFDSOCKETINFO, &si, (int)sizeof si) != (int)sizeof si) continue;
        int fam = si.soi_family;
        if (fam != AF_INET && fam != AF_INET6) { c->sockets_other++; continue; }
        if (si.soi_kind == NS_SOCKINFO_TCP) {
            int st = si.soi_proto.pri_tcp.tcpsi_state;
            if (st >= 0 && st < 11) c->tcp_state[st]++;
            int fport = ntohs((uint16_t)si.soi_proto.pri_tcp.tcpsi_ini.insi_fport);
            if (st != 1 /* LISTEN */) c->tcp_port[port_class(fport)]++;
        } else if (si.soi_kind == NS_SOCKINFO_IN && si.soi_protocol == IPPROTO_UDP) {
            int fport = ntohs((uint16_t)si.soi_proto.pri_in.insi_fport);
            c->udp++;
            if (fport == 53) c->udp53++;
            else if (fport == 443) c->udp443++;
        } else {
            c->sockets_other++;
        }
    }
}

static long rd_long(const char *name) {
    long v = -1;
    size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, NULL, 0) != 0) return -1;
    if (len == sizeof(int)) { int iv; memcpy(&iv, &v, sizeof iv); return iv; }
    return v;
}

static long long ms_of_abs(uint64_t abs) {
    if (!g_tb.denom) return -1;
    return (long long)(abs * g_tb.numer / g_tb.denom / 1000000ull);
}

static void emit_clock(const char *when) {
    struct timespec rt;
    clock_gettime(CLOCK_REALTIME, &rt);
    struct tm tm;
    time_t t = rt.tv_sec;
    gmtime_r(&t, &tm);
    char iso[40];
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &tm);
    emit(1, "clock when=%s utc=%s epoch=%lld", when, iso, (long long)rt.tv_sec);
}

static void snapshot(int index, int elapsed_s) {
    static int pids[MAX_PIDS];
    int bytes = proc_listallpids(pids, (int)sizeof pids);
    int npids = bytes > 0 ? bytes : 0; /* proc_listallpids returns a count */
    if (npids > MAX_PIDS) npids = MAX_PIDS;
    struct counts total;
    memset(&total, 0, sizeof total);
    int present[NWATCH];
    memset(present, 0, sizeof present);
    struct counts wc[NWATCH];
    memset(wc, 0, sizeof wc);
    struct pstat cur[NWATCH];
    memset(cur, 0, sizeof cur);

    for (int i = 0; i < npids; i++) {
        int pid = pids[i];
        if (pid <= 0) continue;
        char name[64];
        memset(name, 0, sizeof name);
        if (proc_name(pid, name, sizeof name) <= 0) continue;
        struct counts c;
        memset(&c, 0, sizeof c);
        scan_fds(pid, &c);
        for (int s = 0; s < 11; s++) total.tcp_state[s] += c.tcp_state[s];
        for (int s = 0; s < PC_N; s++) total.tcp_port[s] += c.tcp_port[s];
        total.udp += c.udp; total.udp53 += c.udp53; total.udp443 += c.udp443; total.necp += c.necp; total.chan += c.chan;
        for (size_t w = 0; w < NWATCH; w++) {
            if (strncmp(name, k_watch[w], strlen(k_watch[w])) != 0) continue;
            if (present[w]) { /* second instance: add sockets, keep first pid for CPU */
                for (int s = 0; s < 11; s++) wc[w].tcp_state[s] += c.tcp_state[s];
                for (int s = 0; s < PC_N; s++) wc[w].tcp_port[s] += c.tcp_port[s];
                wc[w].udp += c.udp; wc[w].necp += c.necp; wc[w].chan += c.chan;
                present[w]++;
                break;
            }
            present[w] = 1;
            wc[w] = c;
            struct ns_proc_taskinfo ti;
            memset(&ti, 0, sizeof ti);
            cur[w].pid = pid;
            if (proc_pidinfo(pid, NS_PROC_PIDTASKINFO, 0, &ti, (int)sizeof ti) == (int)sizeof ti) {
                cur[w].cpu_abs = ti.pti_total_user + ti.pti_total_system;
                cur[w].msg_sent = (uint64_t)(uint32_t)ti.pti_messages_sent;
                cur[w].msg_recv = (uint64_t)(uint32_t)ti.pti_messages_received;
                cur[w].seen = 1;
            }
            break;
        }
    }

    emit(1, "snap n=%d t=%d procs=%d tcp_est=%d tcp_synsent=%d tcp_closewait=%d tcp_timewait=%d tcp443=%d tcp80=%d udp=%d udp53=%d udp443=%d necp_fds=%d chan_fds=%d",
         index, elapsed_s, npids, total.tcp_state[4], total.tcp_state[2], total.tcp_state[5], total.tcp_state[10],
         total.tcp_port[PC_443], total.tcp_port[PC_80], total.udp, total.udp53, total.udp443, total.necp, total.chan);
    emit(1, "necp n=%d socket_policies=%ld non_app_policies=%ld ip_policies=%ld sessions=%ld drop_all_level=%ld drop_unentitled_level=%ld drop_management_level=%ld",
         index, rd_long("net.necp.socket_policy_count"), rd_long("net.necp.socket_non_app_policy_count"),
         rd_long("net.necp.ip_policy_count"), rd_long("net.necp.session_count"), rd_long("net.necp.drop_all_level"),
         rd_long("net.necp.drop_unentitled_level"), rd_long("net.necp.drop_management_level"));

    char absent[LINE_MAX_BYTES];
    size_t ap = 0;
    absent[0] = 0;
    for (size_t w = 0; w < NWATCH; w++) {
        if (!present[w]) {
            int k = snprintf(absent + ap, sizeof absent - ap, "%s%s", ap ? "," : "", k_watch[w]);
            if (k > 0 && (size_t)k < sizeof absent - ap) ap += (size_t)k;
            continue;
        }
        long long cpu_ms = -1, sent = -1, recv = -1;
        if (cur[w].seen && g_prev[w].seen && g_prev[w].pid == cur[w].pid) {
            cpu_ms = ms_of_abs(cur[w].cpu_abs - g_prev[w].cpu_abs);
            sent = (long long)(cur[w].msg_sent - g_prev[w].msg_sent);
            recv = (long long)(cur[w].msg_recv - g_prev[w].msg_recv);
        }
        const struct counts *c = &wc[w];
        emit(g_console_proc, "proc n=%d name=%s pid=%d inst=%d new=%d d_cpu_ms=%lld d_msg_sent=%lld d_msg_recv=%lld tcp est=%d synsent=%d closewait=%d "
                "finwait=%d timewait=%d other=%d p443=%d p80=%d p5223=%d p53=%d pother=%d udp=%d udp53=%d udp443=%d necp=%d chan=%d",
             index, k_watch[w], cur[w].pid, present[w], !(g_prev[w].seen && g_prev[w].pid == cur[w].pid), cpu_ms, sent, recv,
             c->tcp_state[4], c->tcp_state[2], c->tcp_state[5], c->tcp_state[6] + c->tcp_state[9], c->tcp_state[10],
             c->tcp_state[0] + c->tcp_state[3] + c->tcp_state[7] + c->tcp_state[8], c->tcp_port[PC_443], c->tcp_port[PC_80],
             c->tcp_port[PC_5223], c->tcp_port[PC_53], c->tcp_port[PC_OTHER], c->udp, c->udp53, c->udp443, c->necp, c->chan);
    }
    emit(g_console_proc, "absent n=%d names=%s", index, ap ? absent : "-");
    memcpy(g_prev, cur, sizeof cur);
}

static int parse_int(const char *s, int lo, int hi, int *out) {
    char *e = NULL;
    errno = 0;
    long v = strtol(s, &e, 10);
    if (errno || !e || *e || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

int main(int argc, char **argv) {
    int count = 12, interval = 10, delay = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) console_enable();
        else if (!strcmp(argv[i], "-p")) { g_console_proc = 1; console_enable(); }
        else if (!strcmp(argv[i], "-n") && i + 1 < argc && !parse_int(argv[++i], 1, 60, &count)) {}
        else if (!strcmp(argv[i], "-i") && i + 1 < argc && !parse_int(argv[++i], 2, 60, &interval)) {}
        else if (!strcmp(argv[i], "-d") && i + 1 < argc && !parse_int(argv[++i], 0, 900, &delay)) {}
        else {
            static const char u[] = "usage: unixnu-netsnap349 [-c] [-p] [-n count 1..60] [-i seconds 2..60] [-d start-delay 0..900]\n";
            ssize_t w = write(STDERR_FILENO, u, sizeof u - 1);
            (void)w;
            return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    unsigned limit = (unsigned)(delay + count * interval + 30);
    alarm(limit);
    mach_timebase_info(&g_tb);
    emit(1, "start version=349.1 pid=%d uid=%d count=%d interval_s=%d delay_s=%d alarm_s=%u console=%s", (int)getpid(), (int)getuid(),
         count, interval, delay, limit, g_console_fd >= 0 ? "on" : "off");
    emit_clock("start");
    if (delay) sleep((unsigned)delay);
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int n = 0; n < count; n++) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        snapshot(n, (int)(t.tv_sec - t0.tv_sec));
        if (n + 1 < count) sleep((unsigned)interval);
    }
    emit_clock("end");
    emit(1, "RESULT result=done snapshots=%d", count);
    alarm(0);
    return 0;
}
