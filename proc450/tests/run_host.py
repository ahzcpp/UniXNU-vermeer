from pathlib import Path
import re,subprocess,os,json,hashlib
P=Path(__file__).resolve().parents[1];S=P/'src';O=P/'host-out';O.mkdir(exist_ok=True)
s=(S/'unixnu-proc389.c').read_text()
# Legacy-only fixtures: the new mode has its own full production-body harness.
s=s.replace('#include \"proc450_webkit.h\"', '').replace('static int g_webkit450, g_custom_schedule;', '')
def between(a,b):return s[s.index(a):s.index(b,s.index(a))]
chunks=[between('#define TAG','static void console_enable'),between('/* console=1:','/* -m samples'),between('/* -m samples','static void on_alarm'),between('/* Names are fixed','static void emit_clock'),between('static void snapshot(','static int parse_int')]
# Exact production bodies; public OS calls replaced with host-only fixture symbols.
inc='\n'.join(chunks)
(O/'actual-source.inc').write_text(inc)
fixture=r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <arpa/inet.h>
#include <unistd.h>
#define write fixture_write
#define sysctlbyname fixture_sysctl
#define proc_listallpids fixture_list
#define proc_name fixture_name
#define proc_pidinfo fixture_info
#define proc_pidfdinfo fixture_fd
#include "netsnap_procinfo.h"
#include "proc389_watch.h"
typedef struct {uint32_t numer,denom;} mach_timebase_info_data_t;
static ssize_t fixture_write(int,const void*,size_t);
static int fixture_sysctl(const char*,void*,size_t*,void*,size_t);
static void ns351_memory_snapshot(int index,void(*log)(int,const char*,...)){
 log(index<2,"memory_hw n=%d quality=fixture",index);log(index<2,"memory_vm n=%d quality=fixture",index);log(index<2,"memory_io n=%d quality=fixture",index);
}
#include "actual-source.inc"
struct fp{int pid;const char*name;uint64_t cpu;uint32_t sent,recv;int fd_error,task_error,sock_short;};
static struct fp entries[64];static unsigned nentries,read_fds,read_tasks,outrows,conrows,write_fail;
static int fail_name_pid,force_pidtrunc;
static char output[262144];static size_t outused;static unsigned checks;
#define CK(x) do{checks++;assert(x);}while(0)
static ssize_t fixture_write(int fd,const void *data,size_t n){
 CK(n<=LINE_MAX_BYTES);if(fd==STDOUT_FILENO){outrows++;if(outused+n<sizeof output){memcpy(output+outused,data,n);outused+=n;output[outused]=0;}}
 else {CK(fd==42);conrows++;}
 errno=EIO;return write_fail?-1:(ssize_t)n;
}
static struct fp *find(int pid){for(unsigned i=0;i<nentries;i++)if(entries[i].pid==pid)return &entries[i];assert(0);return NULL;}
int fixture_list(void *out,int size){CK(size==(int)(sizeof(int)*MAX_PIDS));int *p=out;if(force_pidtrunc){p[0]=entries[0].pid;for(unsigned i=1;i<MAX_PIDS;i++)p[i]=999;return MAX_PIDS;}for(unsigned i=0;i<nentries;i++)p[i]=entries[i].pid;return (int)nentries;}
int fixture_name(int pid,void *out,uint32_t size){const char *name=find(pid)->name;CK(size==64);if(pid==fail_name_pid){errno=ESRCH;return 0;}size_t n=strlen(name);CK(n<size);memcpy(out,name,n+1);return (int)n;}
int fixture_info(int pid,int flavor,uint64_t arg,void *out,int size){
 struct fp *p=find(pid);CK(ns389_watch_index(p->name,strlen(p->name)+1)>=0);CK(arg==0);
 if(flavor==NS_PROC_PIDLISTFDS){read_fds++;CK(size==(int)(MAX_FDS*sizeof(struct ns_proc_fdinfo)));if(p->fd_error){errno=EACCES;return 0;}struct ns_proc_fdinfo *f=out;f[0]=(struct ns_proc_fdinfo){3,NS_PROX_FDTYPE_SOCKET};return sizeof *f;}
 CK(flavor==NS_PROC_PIDTASKINFO&&size==(int)sizeof(struct ns_proc_taskinfo));read_tasks++;if(p->task_error){errno=EPERM;return 0;}
 struct ns_proc_taskinfo *t=out;memset(t,0,sizeof *t);t->pti_total_user=p->cpu;t->pti_messages_sent=(int32_t)p->sent;t->pti_messages_received=(int32_t)p->recv;return sizeof *t;
}
int fixture_fd(int pid,int fd,int flavor,void *out,int size){
 struct fp *p=find(pid);CK(fd==3&&flavor==NS_PROC_PIDFDSOCKETINFO&&size==792);if(p->sock_short)return size-8;
 struct ns_socket_fdinfo *si=out;memset(si,0,sizeof *si);si->soi_family=AF_INET;si->soi_protocol=IPPROTO_TCP;si->soi_kind=NS_SOCKINFO_TCP;si->soi_proto.pri_tcp.tcpsi_state=4;si->soi_proto.pri_tcp.tcpsi_ini.insi_fport=htons(443);return size;
}
static int fixture_sysctl(const char *name,void *out,size_t *len,void *newp,size_t newlen){
 CK(!newp&&!newlen);CK(!strncmp(name,"net.necp.",9));CK(*len==sizeof(long));long v=0;memcpy(out,&v,sizeof v);return 0;
}
static void clear_output(void){outused=0;output[0]=0;outrows=conrows=0;g_output_rows=g_console_rows=0;read_fds=read_tasks=0;}
static void take(int sample){clear_output();snapshot(sample,sample*30);CK(outrows<100&&conrows<=outrows);}
static void has(const char *text){CK(strstr(output,text)!=NULL);}
static void add(int pid,const char *name){CK(nentries<64);entries[nentries++]=(struct fp){.pid=pid,.name=name,.cpu=1000000,.sent=10,.recv=11};}
int main(void){
 g_tb.numer=g_tb.denom=1;g_console_fd=42;g_console_proc=1;
 CK(NS389_NWATCH==28);for(size_t i=0;i<NS389_NWATCH;i++){
 const char *key=ns389_watch[i].key;CK(strlen(key)<=15);CK(ns389_watch_index(key,strlen(key)+1)==(int)i);
 if(strcmp(ns389_watch[i].label,"WebKit"))CK(ns389_watch_index(ns389_watch[i].label,strlen(ns389_watch[i].label)+1)==(int)i);
 for(size_t j=i+1;j<NS389_NWATCH;j++)CK(strcmp(key,ns389_watch[j].key));
 }
 int web=ns389_watch_index("com.apple.WebKi",16);CK(web>=0);
 CK(ns389_watch_index("com.apple.WebKit.WebContent",28)==web);
 CK(ns389_watch_index("com.apple.WebKit.Networking",28)==web);
 CK(ns389_watch_index("com.apple.WebKit.GPU",21)==web);
 CK(ns389_watch_index("MobileSafari",13)>=0);CK(ns389_watch_index("MobileSafariFoo",16)==-1);
 CK(ns389_watch_index("AppStoreExtra",14)==-1);CK(ns389_watch_index("",1)==-1);CK(ns389_watch_index(NULL,0)==-1);
 char nonul[15];memset(nonul,'A',sizeof nonul);CK(ns389_watch_index(nonul,sizeof nonul)==-1);
 for(size_t i=0;i<NS389_NWATCH-1;i++)add(100+(int)i,ns389_watch[i].label);
 add(900,"com.apple.WebKit.WebContent");add(901,"com.apple.WebKit.Networking");add(902,"com.apple.WebKit.GPU");
 add(999,"UnrelatedPrivate");add(998,"MobileSafariFoo");
 take(0);CK(read_fds==30&&read_tasks==28);has("procs_enumerated=32 watched=30");has("name=WebKit quality=complete pid=0 inst=3 delta_single_pid=0 d_cpu_ms=-1 d_msg_sent=-1 d_msg_recv=-1");has("name=MobileSafari quality=complete");CK(!strstr(output,"UnrelatedPrivate"));CK(!strstr(output,"MobileSafariFoo"));has("absence_proven=0");
 for(unsigned i=0;i<nentries;i++){entries[i].cpu+=1000000;entries[i].sent++;entries[i].recv++;}take(1);
 has("name=AppStore quality=complete pid=100 inst=1 delta_single_pid=1 d_cpu_ms=1 d_msg_sent=1 d_msg_recv=1");has("name=WebKit quality=complete pid=0 inst=3 delta_single_pid=0 d_cpu_ms=-1");
 /* Collapse and stabilize: a previous multi-PID group must not yield delta. */
 nentries=0;add(900,"com.apple.WebKi");take(2);has("name=WebKit quality=complete pid=900 inst=1 delta_single_pid=0 d_cpu_ms=-1");
 entries[0].cpu+=1000000;entries[0].sent++;entries[0].recv++;take(3);has("delta_single_pid=1 d_cpu_ms=1 d_msg_sent=1 d_msg_recv=1");
 entries[0].pid=903;take(4);has("delta_single_pid=0 d_cpu_ms=-1");
 entries[0].task_error=1;take(5);has("delta_single_pid=0 d_cpu_ms=-1 d_msg_sent=-1 d_msg_recv=-1");has("task_fail=1 task_errno=1 task_ret=0");
 entries[0].task_error=0;take(6);has("delta_single_pid=0 d_cpu_ms=-1");
 entries[0].fd_error=1;entries[0].sock_short=0;take(7);has("name=WebKit quality=unknown");has("fd_errno=13 fd_ret=0");
 entries[0].fd_error=0;entries[0].sock_short=1;take(8);has("name=WebKit quality=partial");has("sock_short=1");
 add(999,"UnrelatedPrivate");fail_name_pid=999;take(9);has("name_fail=1 name_errno=3");has("delta_single_pid=0 d_cpu_ms=-1");fail_name_pid=0;take(10);has("delta_single_pid=0 d_cpu_ms=-1");
 force_pidtrunc=1;take(11);has("pid_trunc_possible=1");has("delta_single_pid=0 d_cpu_ms=-1");force_pidtrunc=0;
 nentries=0;take(12);has("quality=unknown procs_enumerated=0 watched=0");has("absence_proven=0");add(903,"com.apple.WebKi");take(10);has("delta_single_pid=0 d_cpu_ms=-1");
 struct ns389_pstat a={.pid=1,.cpu_abs=10,.msg_sent=UINT32_MAX,.msg_recv=11,.seen=1,.instances=1},b=a;b.cpu_abs=9;b.msg_sent=1;b.msg_recv=12;
 struct ns389_delta d=ns389_delta_for(&a,&b);CK(d.same_single_pid&&!d.cpu_known&&!d.sent_known&&d.recv_known&&d.msg_recv==1);
 b.instances=2;d=ns389_delta_for(&a,&b);CK(!d.same_single_pid&&!d.cpu_known&&!d.sent_known&&!d.recv_known);
 CK(ms_of_abs(UINT64_MAX)==(long long)(UINT64_MAX/1000000));g_tb.numer=2;CK(ms_of_abs(UINT64_MAX)==-1);g_tb.numer=0;CK(ms_of_abs(5)==-1);g_tb.numer=1;
 clear_output();for(unsigned i=0;i<1000;i++)emit(1,"cap fixture=%u",i);CK(outrows==319&&conrows==319&&g_output_rows==319&&g_console_rows==319);
 clear_output();write_fail=1;for(unsigned i=0;i<1000;i++)emit(1,"failed_write fixture=%u",i);CK(outrows==319&&conrows==319);write_fail=0;
 clear_output();g_console_proc=0;for(unsigned i=0;i<1000;i++)emit(1,"summary fixture=%u",i);CK(outrows==319&&conrows==64);
 /* Worst normal4-snapshot, all-groups scenario stays below the hard cap. */
 g_console_proc=1;clear_output();nentries=0;for(size_t i=0;i<NS389_NWATCH;i++)add(100+(int)i,ns389_watch[i].key);
 for(unsigned i=0;i<9;i++)emit(1,"start-or-end fixture=%u",i);
 for(int i=0;i<4;i++)snapshot(i,i*30);CK(outrows==269&&conrows==263);CK(g_output_rows<319);
 printf("PASS proc389 %u checks:28 groups/15byte keys, real snapshot multi/single PID deltas, failures/unknown coverage,319+alarm1 stdout/console320 caps; normal4sample rows=%u/%u\n",checks,outrows,conrows);return 0;
}
'''
(O/'integration.c').write_text(fixture)
cc=os.environ.get('CC','clang')
cmd=[cc,'-std=gnu11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(S),'-I'+str(O),str(O/'integration.c'),'-o',str(O/'integration')]
r=subprocess.run(cmd,text=True,capture_output=True);(O/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(O/'integration')],text=True,capture_output=True);(O/'test.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
print(r.stdout,end='')
(P/'HOST-PROOF.json').write_text(json.dumps(dict(actual_source_chunks=True,source_sha256=hashlib.sha256(s.encode()).hexdigest(),actual_ABI_header=True,host_only_OS_fixtures=True,native_mock_headers=False,sanitizers=['ASan','UBSan'],device_tested=False,native_compiled=False,normal_four_sample_stdout_rows=269,normal_four_sample_console_rows=263,stdout_hard_data_rows=319,alarm_reserved_rows=1,console_hard_rows=320),indent=2)+'\n')