/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Opt-in N1/N2 observer. Included after the unchanged PROC389 socket reader.
 * Paths are transient classification input only. Never emit any path bytes.
 * Own LP64 BSDINFO mirror MUST pass tests/abi450.c against genuine headers
 * and the actual target XNU header before native admission.
 */
#ifndef PROC450_WEBKIT_H
#define PROC450_WEBKIT_H
#include <stddef.h>
#define NS450_BSDINFO 3
#define NS450_PATH_BYTES 4096u
#define NS450_SAMPLES 16
#define NS450_INTERVAL 2
#define NS450_PIDS 8
#define NS450_HISTORY (NS450_SAMPLES * NS450_PIDS)
struct ns450_bsdinfo {
    uint32_t flags, status, xstatus, pid, ppid, uid, gid, ruid, rgid;
    uint32_t svuid, svgid, rfu;
    char comm[16], name[32];
    uint32_t nfiles, pgid, pjobc, e_tdev, e_tpgid;
    int32_t nice;
    uint64_t start_sec, start_usec;
};
_Static_assert(sizeof(struct ns450_bsdinfo)==136, "BSDINFO size");
_Static_assert(offsetof(struct ns450_bsdinfo,start_sec)==120, "BSDINFO start");
int proc_pidpath(int pid, void *buffer, uint32_t buffersize);

struct ns450_id { uint64_t sec,usec; int known,ret,error; };
struct ns450_path { int role,ret,error; };
struct ns450_history { int pid,first,last; uint64_t sec,usec; };
static struct ns450_history ns450_history[NS450_HISTORY];
static unsigned ns450_used;

/* Exact executable BASENAME whitelist; directories are deliberately not logged
 * or asserted. This is role evidence, not code-signature/owner attestation.
 * 1 Networking, 2 GPU, 3 WebContent, 4 MobileSafari; 0 unknown. */
static int ns450_role(const char *path,size_t cap,int ret) {
    if(ret<=0 || (size_t)ret>=cap || !cap || path[0]!='/')return 0;
    size_t n=0;while(n<cap && path[n])n++;
    if(n==cap || n!=(size_t)ret)return 0;
    const char *base=strrchr(path,'/');
    if(!base || !base[1])return 0;
    base++;
    if(!strcmp(base,"com.apple.WebKit.Networking"))return 1;
    if(!strcmp(base,"com.apple.WebKit.GPU"))return 2;
    if(!strcmp(base,"com.apple.WebKit.WebContent"))return 3;
    if(!strcmp(base,"MobileSafari"))return 4;
    return 0;
}
static struct ns450_path ns450_path_read(int pid) {
    char path[NS450_PATH_BYTES];memset(path,0,sizeof path);
    struct ns450_path p={0};errno=0;
    p.ret=proc_pidpath(pid,path,(uint32_t)sizeof path);p.error=errno;
    p.role=ns450_role(path,sizeof path,p.ret);
    /* No path retained in history or passed to an output function. */
    return p;
}
static struct ns450_id ns450_id_read(int pid) {
    struct ns450_bsdinfo b;memset(&b,0,sizeof b);
    struct ns450_id id={0};errno=0;
    id.ret=proc_pidinfo(pid,NS450_BSDINFO,0,&b,(int)sizeof b);id.error=errno;
    if(id.ret==(int)sizeof b && b.pid==(uint32_t)pid && b.start_sec && b.start_usec<1000000){
        id.known=1;id.sec=b.start_sec;id.usec=b.start_usec;
    }
    return id;
}
static int ns450_same(struct ns450_id a,struct ns450_id b) {
    return a.known && b.known && a.sec==b.sec && a.usec==b.usec;
}
static int ns450_note(int pid,struct ns450_id id,int n,int *previous,int *reused) {
    *previous=0;*reused=0;
    for(unsigned i=0;i<ns450_used;i++){
        struct ns450_history *h=&ns450_history[i];
        if(h->pid!=pid)continue;
        if(h->sec==id.sec && h->usec==id.usec){
            *previous=(h->last==n-1);h->last=n;return h->first;
        }
        *reused=1;
    }
    if(ns450_used>=NS450_HISTORY)return -1;
    ns450_history[ns450_used++]=(struct ns450_history){pid,n,n,id.sec,id.usec};
    return n;
}
static long long ns450_ms(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void ns450_snapshot(int n,long long origin) {
    static int pids[MAX_PIDS];memset(pids,0,sizeof pids);errno=0;
    int listed=proc_listallpids(pids,(int)sizeof pids),pe=errno;
    int np=listed>0?(listed>MAX_PIDS?MAX_PIDS:listed):0;
    unsigned named_fail=0,candidates=0,selected=0,duplicates=0;
    int seen[NS450_PIDS]={0};
    for(int i=0;i<np;i++){
        int pid=pids[i];if(pid<=0)continue;
        char name[64]={0};errno=0;
        int nr=proc_name(pid,name,sizeof name);
        if(nr<=0 || nr>=(int)sizeof name){named_fail++;continue;}
        int w=ns389_watch_index(name,sizeof name);
        if(w<0 || (strcmp(ns389_watch[w].label,"WebKit") && strcmp(ns389_watch[w].label,"MobileSafari")))continue;
        int duplicate=0;for(unsigned j=0;j<selected;j++)if(seen[j]==pid)duplicate=1;
        if(duplicate){duplicates++;continue;}
        candidates++;
        if(selected>=NS450_PIDS)continue;
        seen[selected++]=pid;
        long long begin=ns450_ms();
        struct ns450_id a=ns450_id_read(pid);
        struct ns450_path pa=ns450_path_read(pid);
        struct counts c={0};scan_fds(pid,&c);
        struct ns450_path pb=ns450_path_read(pid);
        struct ns450_id b=ns450_id_read(pid);
        long long end=ns450_ms();
        int current=ns450_same(a,b);
        int role=current && pa.role==pb.role ? pa.role : 0;
        int first=-1,previous=0,reused=0;
        if(current)first=ns450_note(pid,a,n,&previous,&reused);
        emit(g_console_proc,"wk450 n=%d pid=%d role=%d current=%d first_seen_n=%d prev_sample=%d pid_reused=%d begin_ms=%lld end_ms=%lld quality=%s est=%d synsent=%d closewait=%d timewait=%d p443=%d p80=%d udp=%d udp53=%d udp443=%d necp_fds=%d chan_fds=%d",
             n,pid,role,current,first,previous,reused,
             begin>=origin && origin>=0?begin-origin:-1,end>=origin && origin>=0?end-origin:-1,
             counts_quality(&c),c.tcp_state[4],c.tcp_state[2],c.tcp_state[5],c.tcp_state[10],c.tcp_port[PC_443],c.tcp_port[PC_80],c.udp,c.udp53,c.udp443,c.necp,c.chan);
        emit(g_console_proc,"wk450_coverage n=%d pid=%d fd_ok=%u fd_fail=%u fd_empty=%u fd_trunc=%u fd_ret=%d fd_errno=%d sock_seen=%u sock_ok=%u sock_fail=%u sock_short=%u sock_invalid=%u sock_ret=%d sock_errno=%d br0=%d be0=%d br1=%d be1=%d pr0=%d pe0=%d pr1=%d pe1=%d",
             n,pid,c.q.fd_ok,c.q.fd_fail,c.q.fd_empty,c.q.fd_trunc,c.q.fd_ret,c.q.fd_errno,
             c.q.sock_seen,c.q.sock_ok,c.q.sock_fail,c.q.sock_short,c.q.sock_invalid,c.q.sock_ret,c.q.sock_errno,
             a.ret,a.error,b.ret,b.error,pa.ret,pa.error,pb.ret,pb.error);
    }
    emit(1,"wk450_sample n=%d pid_ret=%d pid_errno=%d pid_trunc=%d name_fail=%u candidates=%u selected=%u selected_cap=%d duplicates=%u absence_proven=0",
         n,listed,pe,listed>=MAX_PIDS,named_fail,candidates,selected,candidates>NS450_PIDS,duplicates);
}
static void ns450_timeout(int sig) {
    (void)sig;
    /* Do not risk blocking the watchdog on an inherited full stdout pipe. */
    _exit(124);
}
static int ns450_run(void) {
    /* Independent hard bound; legacy -n/-i/-d/-m cannot change this mode. */
    struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=ns450_timeout;
    if(sigaction(SIGALRM,&sa,NULL))return 2;
    alarm(65);
    emit(1,"wk450_start version=450.1 observer_pid=%d uid=%d samples=16 interval_s=2 pid_cap=8 alarm_s=65",(int)getpid(),(int)getuid());
    emit_clock("wk450_start");
    long long origin=ns450_ms();
    for(int n=0;n<NS450_SAMPLES;n++){
        ns450_snapshot(n,origin);
        if(n+1<NS450_SAMPLES)sleep(NS450_INTERVAL);
    }
    emit_clock("wk450_end");
    emit(1,"RESULT result=done mode=wk450 snapshots=16");
    alarm(0);return 0;
}
#endif
