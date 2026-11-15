/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-netsnap357: own, read-only, bounded snapshot of which processes hold network sockets / NECP clients and whether they
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
#include "netsnap_memory351.h"

#define TAG "NETSNAP357"
#define LINE_MAX_BYTES 480
#define CONSOLE_MAX_ROWS 256
#define MAX_PIDS 4096
#define MAX_FDS 4096

static int g_console_fd = -1;
static unsigned g_console_rows;
static int g_console_proc;
static int g_memory_only;

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
    if (console && g_console_fd >= 0 && g_console_rows < ((g_console_proc || g_memory_only) ? CONSOLE_MAX_ROWS : 64u)) {
        g_console_rows++;
        w = write(g_console_fd, buf, len);
        (void)w;
    }
}

/* -m samples only VM and these three read-only compressor sysctls. No PID/FD calls. */
static void memory_emit_all(int unused, const char *fmt, ...) __attribute__((format(printf,2,3)));
static void memory_emit_all(int unused, const char *fmt, ...) {
    (void)unused;char text[LINE_MAX_BYTES];va_list ap;va_start(ap,fmt);
    int n=vsnprintf(text,sizeof text,fmt,ap);va_end(ap);
    if(n>=0)emit(1,"%s",text);
}
static void compressor_read(int index,const char *name,size_t expected) {
    union {int32_t i;int64_t q;} value={0};
    void *buffer=expected==sizeof(int32_t) ? (void *)&value.i : (void *)&value.q;
    size_t length=expected;errno=0;
    int rc=sysctlbyname(name,buffer,&length,NULL,0);const int error=errno;
    int known=rc==0 && length==expected;char text[32];
    if(known)snprintf(text,sizeof text,"%lld",(long long)(expected==sizeof(int32_t) ? (int64_t)value.i : value.q));
    else snprintf(text,sizeof text,"unknown");
    emit(1,"compressor n=%d key=%s known=%d rc=%d errno=%d expected_len=%zu returned_len=%zu value=%s",index,name,known,rc,error,expected,length,text);
}
static void compressor_snapshot(int index) {
    compressor_read(index,"vm.compressor_mode",sizeof(int32_t));
    compressor_read(index,"vm.compressor_is_active",sizeof(int32_t));
    compressor_read(index,"vm.compressor_bytes_used",sizeof(int64_t));
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
/* Error fields are captured immediately after the relevant call. Counts are
 * positive observations; incomplete coverage never proves an absent process. */
struct quality {
    unsigned fd_ok, fd_fail, fd_empty, fd_trunc, sock_seen, sock_ok, sock_fail, sock_short;
    unsigned sock_invalid, task_ok, task_fail;
    int fd_errno, fd_ret, sock_errno, sock_ret, task_errno, task_ret;
};
struct counts {
    int tcp_state[11];
    int tcp_port[PC_N];
    int udp, udp53, udp443;
    int necp, chan, sockets_other;
    struct quality q;
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

static const char *counts_quality(const struct counts *c) {
    if (!c->q.fd_ok && !c->q.fd_fail) return "unknown"; /* no coverage attempted */
    if (!c->q.fd_ok && c->q.fd_fail) return "unknown";
    if (c->q.fd_fail || c->q.fd_trunc || c->q.sock_fail || c->q.sock_short || c->q.sock_invalid) return "partial";
    return "complete";
}
static void add_counts(struct counts *to, const struct counts *from) {
    for (int s=0;s<11;s++) to->tcp_state[s]+=from->tcp_state[s];
    for (int s=0;s<PC_N;s++) to->tcp_port[s]+=from->tcp_port[s];
    to->udp+=from->udp;to->udp53+=from->udp53;to->udp443+=from->udp443;
    to->necp+=from->necp;to->chan+=from->chan;to->sockets_other+=from->sockets_other;
    if (!to->q.fd_fail && from->q.fd_fail) {to->q.fd_errno=from->q.fd_errno;to->q.fd_ret=from->q.fd_ret;}
    if (!to->q.sock_fail && !to->q.sock_short && (from->q.sock_fail || from->q.sock_short)) {
        to->q.sock_errno=from->q.sock_errno;to->q.sock_ret=from->q.sock_ret;
    }
    if (!to->q.task_fail && from->q.task_fail) {to->q.task_errno=from->q.task_errno;to->q.task_ret=from->q.task_ret;}
#define NS_ADD(field) to->q.field+=from->q.field
    NS_ADD(fd_ok);NS_ADD(fd_fail);NS_ADD(fd_empty);NS_ADD(fd_trunc);
    NS_ADD(sock_seen);NS_ADD(sock_ok);NS_ADD(sock_fail);NS_ADD(sock_short);NS_ADD(sock_invalid);
    NS_ADD(task_ok);NS_ADD(task_fail);
#undef NS_ADD
}
static void scan_fds(int pid, struct counts *c) {
    static struct ns_proc_fdinfo fds[MAX_FDS];
    errno=0;
    int bytes=proc_pidinfo(pid,NS_PROC_PIDLISTFDS,0,fds,(int)sizeof fds);
    const int saved_error=errno;
    if (bytes<0 || (bytes==0 && saved_error) || bytes>(int)sizeof fds || bytes%(int)sizeof fds[0]) {
        c->q.fd_fail++;c->q.fd_errno=saved_error;c->q.fd_ret=bytes;return;
    }
    c->q.fd_ok++;if (!bytes)c->q.fd_empty++;
    if (bytes==(int)sizeof fds)c->q.fd_trunc++; /* possibly full, not proven complete */
    int n=bytes/(int)sizeof fds[0];
    for (int i=0;i<n;i++) {
        if(fds[i].proc_fdtype==NS_PROX_FDTYPE_NETPOLICY){c->necp++;continue;}
        if(fds[i].proc_fdtype==NS_PROX_FDTYPE_CHANNEL){c->chan++;continue;}
        if(fds[i].proc_fdtype!=NS_PROX_FDTYPE_SOCKET)continue;
        c->q.sock_seen++;
        struct ns_socket_fdinfo si;memset(&si,0,sizeof si);
        errno=0;
        int got=proc_pidfdinfo(pid,fds[i].proc_fd,NS_PROC_PIDFDSOCKETINFO,&si,(int)sizeof si);
        const int socket_error=errno;
        if(got!=(int)sizeof si){
            if(!c->q.sock_fail && !c->q.sock_short){c->q.sock_errno=socket_error;c->q.sock_ret=got;}
            if(got>0)c->q.sock_short++;else c->q.sock_fail++;
            continue;
        }
        c->q.sock_ok++;
        int fam=si.soi_family;
        if(fam!=AF_INET && fam!=AF_INET6){c->sockets_other++;continue;}
        if(si.soi_kind==NS_SOCKINFO_TCP){
            int st=si.soi_proto.pri_tcp.tcpsi_state;
            if(st>=0 && st<11)c->tcp_state[st]++;else c->q.sock_invalid++;
            int fport=ntohs((uint16_t)si.soi_proto.pri_tcp.tcpsi_ini.insi_fport);
            if(st!=1)c->tcp_port[port_class(fport)]++;
        }else if(si.soi_kind==NS_SOCKINFO_IN && si.soi_protocol==IPPROTO_UDP){
            int fport=ntohs((uint16_t)si.soi_proto.pri_in.insi_fport);
            c->udp++;if(fport==53)c->udp53++;else if(fport==443)c->udp443++;
        }else c->sockets_other++;
    }
}
struct sysctl_quality {unsigned ok,fail,bad_len;int first_errno;size_t first_len;};
static long rd_long(const char *name,struct sysctl_quality *q) {
    long v=0;size_t len=sizeof v;errno=0;
    int rc=sysctlbyname(name,&v,&len,NULL,0);const int saved_error=errno;
    if(rc || (len!=sizeof(int) && len!=sizeof(long))){
        if(!q->fail){q->first_errno=saved_error;q->first_len=len;}
        q->fail++;if(!rc)q->bad_len++;return -1;
    }
    q->ok++;
    if(len==sizeof(int)){int iv;memcpy(&iv,&v,sizeof iv);return iv;}
    return v;
}
static void emit_coverage(int mirror,int index,const char *name,const struct counts *c) {
    emit(mirror,"coverage n=%d name=%s quality=%s fd_ok=%u fd_fail=%u fd_empty=%u fd_trunc_possible=%u fd_errno=%d fd_ret=%d sock_seen=%u sock_ok=%u sock_fail=%u sock_short=%u sock_invalid=%u sock_errno=%d sock_ret=%d task_ok=%u task_fail=%u task_errno=%d task_ret=%d",
         index,name,counts_quality(c),c->q.fd_ok,c->q.fd_fail,c->q.fd_empty,c->q.fd_trunc,c->q.fd_errno,c->q.fd_ret,
         c->q.sock_seen,c->q.sock_ok,c->q.sock_fail,c->q.sock_short,c->q.sock_invalid,c->q.sock_errno,c->q.sock_ret,
         c->q.task_ok,c->q.task_fail,c->q.task_errno,c->q.task_ret);
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

static void snapshot(int index,int elapsed_s) {
    if(g_memory_only){ns351_memory_snapshot(index,memory_emit_all);compressor_snapshot(index);return;}
    if(index<2)emit(1,"phase n=%d stage=snapshot_begin",index);
    ns351_memory_snapshot(index,emit);
    static int pids[MAX_PIDS];errno=0;
    int listed=proc_listallpids(pids,(int)sizeof pids);const int pid_error=errno;
    int pid_fail=listed<=0, pid_trunc=listed>=MAX_PIDS;
    int npids=listed>0 ? (listed>MAX_PIDS ? MAX_PIDS : listed) : 0;
    if(index<2)emit(1,"phase n=%d stage=enumeration_done pid_ret=%d pid_errno=%d trunc_possible=%d",index,listed,pid_error,pid_trunc);
    unsigned name_ok=0,name_fail=0;int name_error=0;
    unsigned watched=0;
    struct counts total={0},wc[NWATCH]={0};int present[NWATCH]={0};struct pstat cur[NWATCH]={0};
    if(index<2)emit(1,"phase n=%d stage=process_scan_begin scope=watchlist_fd_only",index);
    for(int i=0;i<npids;i++) {
        int pid=pids[i];if(pid<=0)continue;
        char name[64]={0};errno=0;
        int named=proc_name(pid,name,sizeof name);const int saved_error=errno;
        if(named<=0 || named>=(int)sizeof name){if(!name_fail)name_error=saved_error;name_fail++;continue;}
        name_ok++;
        for(size_t w=0;w<NWATCH;w++) {
            if(strncmp(name,k_watch[w],strlen(k_watch[w])))continue;
            /* Enumeration/name lookup only for unrelated processes. Never read
             * their FD lists, sockets or task counters. */
            struct counts c={0};scan_fds(pid,&c);add_counts(&total,&c);watched++;
            if(present[w]){add_counts(&wc[w],&c);present[w]++;break;}
            present[w]=1;wc[w]=c;cur[w].pid=pid;
            struct ns_proc_taskinfo ti;memset(&ti,0,sizeof ti);errno=0;
            int got=proc_pidinfo(pid,NS_PROC_PIDTASKINFO,0,&ti,(int)sizeof ti);const int task_error=errno;
            if(got==(int)sizeof ti){
                wc[w].q.task_ok++;cur[w].cpu_abs=ti.pti_total_user+ti.pti_total_system;
                cur[w].msg_sent=(uint64_t)(uint32_t)ti.pti_messages_sent;
                cur[w].msg_recv=(uint64_t)(uint32_t)ti.pti_messages_received;cur[w].seen=1;
            }else{wc[w].q.task_fail++;wc[w].q.task_errno=task_error;wc[w].q.task_ret=got;}
            break;
        }
    }
    if(index<2)emit(1,"phase n=%d stage=process_scan_end scope=watchlist_fd_only watched=%u",index,watched);
    const char *quality=pid_fail || !watched ? "unknown" : (pid_trunc || name_fail || strcmp(counts_quality(&total),"complete") ? "partial" : "complete");
    emit(1,"watched_total n=%d t=%d scope=watchlist_only quality=%s procs_enumerated=%d watched=%u pid_ok=%d pid_fail=%d pid_errno=%d pid_ret=%d pid_trunc_possible=%d name_ok=%u name_fail=%u name_errno=%d tcp_est=%d tcp_synsent=%d tcp_closewait=%d tcp_timewait=%d tcp443=%d tcp80=%d udp=%d udp53=%d udp443=%d necp_fds=%d chan_fds=%d",
         index,elapsed_s,quality,npids,watched,!pid_fail,pid_fail,pid_error,listed,pid_trunc,name_ok,name_fail,name_error,
         total.tcp_state[4],total.tcp_state[2],total.tcp_state[5],total.tcp_state[10],total.tcp_port[PC_443],total.tcp_port[PC_80],total.udp,total.udp53,total.udp443,total.necp,total.chan);
    emit_coverage(1,index,"watched_total",&total);
    static const char *const keys[]={"net.necp.socket_policy_count","net.necp.socket_non_app_policy_count","net.necp.ip_policy_count","net.necp.session_count","net.necp.drop_all_level","net.necp.drop_unentitled_level","net.necp.drop_management_level"};
    long values[7];struct sysctl_quality sq={0};for(unsigned i=0;i<7;i++)values[i]=rd_long(keys[i],&sq);
    emit(1,"necp n=%d quality=%s read_ok=%u read_fail=%u bad_len=%u first_errno=%d first_len=%zu socket_policies=%ld non_app_policies=%ld ip_policies=%ld sessions=%ld drop_all_level=%ld drop_unentitled_level=%ld drop_management_level=%ld",
         index,sq.fail ? (sq.ok ? "partial" : "unknown") : "complete",sq.ok,sq.fail,sq.bad_len,sq.first_errno,sq.first_len,
         values[0],values[1],values[2],values[3],values[4],values[5],values[6]);
    char unobserved[300]={0};size_t ap=0;int names_truncated=0;
    for(size_t w=0;w<NWATCH;w++){
        if(!present[w]){
            if(!names_truncated){int k=snprintf(unobserved+ap,sizeof unobserved-ap,"%s%s",ap ? "," : "",k_watch[w]);
                if(k>0 && (size_t)k<sizeof unobserved-ap)ap+=(size_t)k;else{unobserved[ap]=0;names_truncated=1;}}
            continue;
        }
        long long cpu_ms=-1,sent=-1,recv=-1;
        if(cur[w].seen && g_prev[w].seen && g_prev[w].pid==cur[w].pid){
            if(cur[w].cpu_abs>=g_prev[w].cpu_abs)cpu_ms=ms_of_abs(cur[w].cpu_abs-g_prev[w].cpu_abs);
            if(cur[w].msg_sent>=g_prev[w].msg_sent)sent=(long long)(cur[w].msg_sent-g_prev[w].msg_sent);
            if(cur[w].msg_recv>=g_prev[w].msg_recv)recv=(long long)(cur[w].msg_recv-g_prev[w].msg_recv);
        }
        const struct counts *c=&wc[w];
        emit(g_console_proc,"proc n=%d name=%s quality=%s pid=%d inst=%d new=%d d_cpu_ms=%lld d_msg_sent=%lld d_msg_recv=%lld tcp est=%d synsent=%d closewait=%d finwait=%d timewait=%d other=%d p443=%d p80=%d p5223=%d p53=%d pother=%d udp=%d udp53=%d udp443=%d necp=%d chan=%d",
             index,k_watch[w],counts_quality(c),cur[w].pid,present[w],!(g_prev[w].seen && g_prev[w].pid==cur[w].pid),cpu_ms,sent,recv,
             c->tcp_state[4],c->tcp_state[2],c->tcp_state[5],c->tcp_state[6]+c->tcp_state[9],c->tcp_state[10],c->tcp_state[0]+c->tcp_state[3]+c->tcp_state[7]+c->tcp_state[8],
             c->tcp_port[PC_443],c->tcp_port[PC_80],c->tcp_port[PC_5223],c->tcp_port[PC_53],c->tcp_port[PC_OTHER],c->udp,c->udp53,c->udp443,c->necp,c->chan);
        emit_coverage(g_console_proc,index,k_watch[w],c);
    }
    emit(g_console_proc,"not_observed n=%d enum_complete=%d absence_proven=0 names_truncated=%d names=%s",index,!pid_fail && !pid_trunc && !name_fail,names_truncated,ap ? unobserved : "-");
    memcpy(g_prev,cur,sizeof cur);
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
        else if (!strcmp(argv[i], "-m")) { g_memory_only = 1; }
        else if (!strcmp(argv[i], "-n") && i + 1 < argc && !parse_int(argv[++i], 1, 60, &count)) {}
        else if (!strcmp(argv[i], "-i") && i + 1 < argc && !parse_int(argv[++i], 2, 60, &interval)) {}
        else if (!strcmp(argv[i], "-d") && i + 1 < argc && !parse_int(argv[++i], 0, 900, &delay)) {}
        else {
            static const char u[] = "usage: unixnu-netsnap357 [-c] [-p] [-m memory-only] [-n count 1..60] [-i seconds 2..60] [-d start-delay 0..900]\n";
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
    emit(1, "start version=357.2 pid=%d uid=%d count=%d interval_s=%d delay_s=%d alarm_s=%u console=%s", (int)getpid(), (int)getuid(),
         count, interval, delay, limit, g_console_fd >= 0 ? "on" : "off");
    emit_clock("start");
    emit(1,"phase stage=memory_begin");
    ns351_memory_snapshot(-1,emit);
    if(g_memory_only)compressor_snapshot(-1);
    emit(1,"phase stage=memory_end");
    if (delay) {
        emit(1,"phase stage=start_delay_begin seconds=%d",delay);
        sleep((unsigned)delay);
        emit(1,"phase stage=start_delay_end");
    }
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
