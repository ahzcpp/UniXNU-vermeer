"""Host-only mocked OS tests of exact production C bodies; no device access.
Requires clang/gcc and POSIX host headers (macOS or an already available Linux).
Never starts WSL. Output remains under candidate/host-out450.
"""
from pathlib import Path
import os, subprocess, json, hashlib
R=Path(__file__).resolve().parents[1];O=R/'host-out450';O.mkdir(exist_ok=True)
s=(R/'src/unixnu-proc389.c').read_text()
def chunk(a,b):return s[s.index(a):s.index(b,s.index(a))]
body='\n'.join([chunk('#define TAG','static void console_enable'),
    chunk('/* console=1:','/* -m samples'),chunk('/* Names are fixed','static void emit_clock')])
(O/'production.inc').write_text(body)
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#define write fixture_write
#define sysctlbyname fixture_sysctl
#include "netsnap_procinfo.h"
#include "proc389_watch.h"
typedef struct {uint32_t numer,denom;} mach_timebase_info_data_t;
static ssize_t fixture_write(int,const void*,size_t);
static int fixture_sysctl(const char*,void*,size_t*,void*,size_t);
#include "production.inc"
static void emit_clock(const char *when){emit(1,"clock fixture=%s",when);}
#include "proc450_webkit.h"
static char output[200000];static size_t used;
static int mode,round_id,ids,paths,fdcalls,namecalls,enum_many;
static int alive=1;
#define CK(x) assert(x)
static ssize_t fixture_write(int fd,const void *p,size_t n){
 CK(n<LINE_MAX_BYTES);if(fd==STDOUT_FILENO){CK(used+n<sizeof output);memcpy(output+used,p,n);used+=n;output[used]=0;}return (ssize_t)n;
}
static int fixture_sysctl(const char *s,void *v,size_t *z,void *n,size_t m){(void)s;(void)v;(void)z;CK(!n&&!m);return -1;}
int proc_listallpids(void *p,int size){CK(size==MAX_PIDS*(int)sizeof(int));if(mode==10){errno=EPERM;return -1;}
 int count=enum_many?MAX_PIDS:1;for(int i=0;i<count;i++)((int*)p)[i]=100+i;return count;}
int proc_name(int pid,void *p,uint32_t size){(void)pid;CK(size==64);namecalls++;if(mode==11){errno=ESRCH;return 0;}
 const char *s=mode==12?"PrivateUnrelated":"com.apple.WebKit";strcpy(p,s);return (int)strlen(s);}
int proc_pidpath(int pid,void *out,uint32_t size){(void)pid;paths++;CK(size==4096);
 if(mode==3){errno=EACCES;return 0;}
 const char *p=mode==4?"/private/sentinel/com.apple.WebKit.Networking.extra":"/private/sentinel/com.apple.WebKit.Networking";
 if(mode==13 && paths%2==0)p="/private/sentinel/com.apple.WebKit.GPU";
 strcpy(out,p);return (int)strlen(p);}
int proc_pidinfo(int pid,int flavor,uint64_t arg,void *out,int size){CK(arg==0);
 if(flavor==NS450_BSDINFO){ids++;CK(size==136);if(mode==1){errno=EPERM;return 0;}
  struct ns450_bsdinfo *b=out;memset(b,0,sizeof *b);b->pid=(uint32_t)pid;b->start_sec=123+(unsigned)round_id;
  if(mode==2 && ids%2==0)b->start_sec++;if(!alive){errno=ESRCH;return 0;}return size;}
 CK(flavor==NS_PROC_PIDLISTFDS);fdcalls++;
 if(mode==5){errno=EACCES;return 0;}if(mode==6)return 0;
 struct ns_proc_fdinfo *f=out;f[0]=(struct ns_proc_fdinfo){3,NS_PROX_FDTYPE_SOCKET};
 f[1]=(struct ns_proc_fdinfo){4,NS_PROX_FDTYPE_NETPOLICY};
 if(mode==7){for(int i=2;i<MAX_FDS;i++)f[i]=(struct ns_proc_fdinfo){i+3,0};return size;}
 return 2*(int)sizeof *f;
}
int proc_pidfdinfo(int pid,int fd,int flavor,void *out,int size){(void)pid;CK(fd==3&&flavor==NS_PROC_PIDFDSOCKETINFO);
 if(mode==8)return size-8;if(mode==9){errno=EBADF;return -1;}
 struct ns_socket_fdinfo *s=out;memset(s,0,sizeof *s);s->soi_family=AF_INET;s->soi_kind=NS_SOCKINFO_TCP;
 s->soi_proto.pri_tcp.tcpsi_state=4;s->soi_proto.pri_tcp.tcpsi_ini.insi_fport=htons(443);return size;
}
static void take(int n){used=0;output[0]=0;ids=paths=fdcalls=namecalls=0;g_output_rows=g_console_rows=0;ns450_snapshot(n,ns450_ms());CK(!strstr(output,"sentinel"));CK(!strstr(output,"PrivateUnrelated"));}
static void has(const char *s){if(!strstr(output,s)){fputs(output,stderr);fprintf(stderr,"missing %s\n",s);abort();}}
int main(void){
 const char *names[]={"com.apple.WebKit.Networking","com.apple.WebKit.GPU","com.apple.WebKit.WebContent","MobileSafari"};
 for(int r=1;r<=4;r++){char p[128];snprintf(p,sizeof p,"/private/sentinel/%s",names[r-1]);CK(ns450_role(p,sizeof p,(int)strlen(p))==r);strcat(p,".extra");CK(!ns450_role(p,sizeof p,(int)strlen(p)));}
 CK(!ns450_role("relative/MobileSafari",21,20));CK(!ns450_role("/MobileSafari",14,-1));
 char nonul[20];memset(nonul,'x',sizeof nonul);nonul[0]='/';CK(!ns450_role(nonul,sizeof nonul,19));
 CK(!ns450_role("/MobileSafari",14,14));CK(!ns450_role("/MobileSafari",14,12));
 take(0);has("role=1 current=1 first_seen_n=0 prev_sample=0");has("est=1");has("necp_fds=1");
 take(1);has("first_seen_n=0 prev_sample=1");take(3);has("first_seen_n=0 prev_sample=0");
 round_id++;take(4);has("first_seen_n=4 prev_sample=0 pid_reused=1");
 mode=1;take(5);has("role=0 current=0 first_seen_n=-1");has("br0=0 be0=1");
 mode=2;take(6);has("role=0 current=0 first_seen_n=-1");
 mode=3;take(7);has("role=0 current=1");has("pr0=0 pe0=13");
 mode=4;take(8);has("role=0 current=1");mode=5;take(9);has("quality=unknown");has("fd_errno=13");
 mode=6;take(10);has("quality=complete est=0");has("absence_proven=0");
 mode=7;take(11);has("quality=partial");has("fd_trunc=1");
 mode=8;take(12);has("sock_short=1");mode=9;take(13);has("sock_fail=1");
 mode=10;take(14);has("pid_ret=-1 pid_errno=1");CK(!fdcalls);
 mode=11;take(15);has("name_fail=1");CK(!fdcalls);mode=12;take(16);CK(!fdcalls&&!paths&&!ids);
 mode=13;take(17);has("role=0 current=1");
 mode=0;enum_many=1;take(18);CK(fdcalls==8&&paths==16&&ids==16);has("selected=8 selected_cap=1");has("pid_trunc=1");
 g_output_rows=0;used=0;for(int i=0;i<16;i++)ns450_snapshot(i,ns450_ms());CK(g_output_rows==272);
 for(int i=0;i<100;i++)emit(1,"cap");CK(g_output_rows==319);
 puts("PASS production450 role, identity, reuse/gaps, errors, socket coverage, cap and privacy fixtures; host-only");return 0;
}
'''
(O/'fixture.c').write_text(fixture)
cc=os.environ.get('CC','clang')
cmd=[cc,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(R/'src'),'-I'+str(O),str(O/'fixture.c'),'-o',str(O/'fixture')]
try:r=subprocess.run(cmd,capture_output=True,text=True)
except FileNotFoundError:
    (O/'result.json').write_text(json.dumps({'status':'NOT_RUN','reason':'C compiler unavailable','native_compiled':False,'hardware_tested':False},indent=2)+'\n')
    raise SystemExit('NOT RUN: C compiler unavailable; no C execution/ABI claim')
(O/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(O/'fixture')],capture_output=True,text=True)
(O/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
(O/'result.json').write_text(json.dumps({'status':'PASS','production_header_sha256':hashlib.sha256((R/'src/proc450_webkit.h').read_bytes()).hexdigest(),'native_compiled':False,'hardware_tested':False,'host_only_fixtures':True,'sanitizers':['ASan','UBSan']},indent=2)+'\n')
print(r.stdout,end='')
