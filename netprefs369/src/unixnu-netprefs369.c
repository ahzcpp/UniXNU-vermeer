#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include "netprefs369_cf.h"
static int console_fd=-1; static unsigned rows;
static volatile sig_atomic_t phase;
static void watchdog(int sig) { (void)sig; /* async-signal-safe, bounded literal only */
 const char *s="NETPREFS369 timeout=1\n";
 (void)write(STDOUT_FILENO,s,sizeof("NETPREFS369 timeout=1\n")-1);
 if(console_fd>=0) (void)write(console_fd,s,sizeof("NETPREFS369 timeout=1\n")-1);
 _exit(124);
}
static void row(const char *fmt,...) {
 if(rows>=255) return; char b[768]; va_list ap; va_start(ap,fmt);
 int n=vsnprintf(b,sizeof(b),fmt,ap); va_end(ap);
 if(n<0 || (size_t)n>=sizeof(b)) return;
 (void)write(STDOUT_FILENO,b,(size_t)n);
 if(console_fd>=0) (void)write(console_fd,b,(size_t)n); rows++;
}
static int argnum(const char *s,int max) { char *e=NULL; errno=0;long v=strtol(s,&e,10);return !errno&&s[0]&&e&&!*e&&v>=1&&v<=max?(int)v:0; }
static void sample(unsigned seq) {
 char model[256]={0};size_t len=sizeof(model);errno=0;phase=1;
 int hwrc=sysctlbyname("hw.model",model,&len,NULL,0);int hwe=hwrc?errno:0;
 int bytes=hwrc==0&&np_model_bytes_ok(model,len,sizeof(model));
 CFStringRef hw=bytes?CFStringCreateWithBytes(NULL,(const UInt8*)model,(CFIndex)len-1,kCFStringEncodingUTF8,false):NULL;
 row("NETPREFS369 sample=%u phase=1 hw_rc=%d hw_errno=%d hw_len=%lu hw_known=%d\n",seq,hwrc,hwe,(unsigned long)len,hw!=NULL);
 phase=2;row("NETPREFS369 sample=%u phase=2 begin=1\n",seq);
 SCDynamicStoreRef store=SCDynamicStoreCreate(NULL,CFSTR("netprefs369-readonly"),NULL,NULL);int se=store?0:SCError();
 CFPropertyListRef v=NULL;int ce=0;if(store){v=SCDynamicStoreCopyValue(store,CFSTR("Plugin:InterfaceNamer"));ce=v?0:SCError();}
 int dict=v&&CFGetTypeID(v)==CFDictionaryGetTypeID();
 /* result0=value;1=NoKey;2=read failure;3=create failure. NoKey isn't file absence. */
 int result=!store?3:v?0:ce==kSCStatusNoKey?1:2;
 row("NETPREFS369 sample=%u phase=2 create=%d create_sc=%d result=%d copy_sc=%d present=%d dict=%d type_unknown=%d\n",seq,store!=NULL,se,result,ce,v!=NULL,dict,v!=NULL&&!dict);
 const CFStringRef keys[]={CFSTR("*START*"),CFSTR("*STACK*"),CFSTR("*QUIET*"),CFSTR("*COMPLETE*"),CFSTR("*TIMEOUT*")};
 for(unsigned k=0;k<5;k++){np_stamp t=np_timestamp(v,keys[k]);row("NETPREFS369 sample=%u phase=2 key=%u known=%d present=%d timestamp_known=%d timestamp=%.3f\n",seq,k,dict,t.present,t.numeric,t.stamp);}
 if(v)CFRelease(v);if(store)CFRelease(store);
 phase=3;row("NETPREFS369 sample=%u phase=3 begin=1\n",seq);
 SCPreferencesRef prefs=SCPreferencesCreate(NULL,CFSTR("netprefs369-readonly"),CFSTR("NetworkInterfaces.plist"));int pe=prefs?0:SCError();
 CFPropertyListRef pm=NULL,pi=NULL;int me=0,ie=0;
 if(prefs){pm=SCPreferencesGetValue(prefs,CFSTR("Model"));me=pm?0:SCError(); pi=SCPreferencesGetValue(prefs,CFSTR("Interfaces"));ie=pi?0:SCError();}
 int known=0;int matches=np_model_match(pm,hw,&known);np_interfaces a=np_reduce_interfaces(pi);
 row("NETPREFS369 sample=%u phase=3 create=%d create_sc=%d model_present=%d model_sc=%d model_string=%d model_matches_known=%d model_matches=%d interfaces_present=%d interfaces_sc=%d interfaces_known=%d count=%ld inspected=%ld truncated=%d dicts=%ld prefix_en=%ld unit0=%ld mac6=%ld en0mac6=%ld\n",seq,prefs!=NULL,pe,pm!=NULL,me,pm&&CFGetTypeID(pm)==CFStringGetTypeID(),known,matches,a.present,ie,a.known,a.count,a.inspected,a.truncated,a.dicts,a.prefix_en,a.unit0,a.mac6,a.en0mac6);
 if(prefs)CFRelease(prefs);if(hw)CFRelease(hw);phase=4;
 row("NETPREFS369 sample=%u phase=4 done=1\n",seq);
}
int main(int argc,char **argv) {
 int n=12,interval=30,want_console=0;
 int outflags=fcntl(STDOUT_FILENO,F_GETFL);
 if(outflags<0 || fcntl(STDOUT_FILENO,F_SETFL,outflags|O_NONBLOCK)<0) return 3;
 for(int i=1;i<argc;i++) {
  if(!strcmp(argv[i],"-c")){want_console=1;continue;}
  if((!strcmp(argv[i],"-n")||!strcmp(argv[i],"-i"))&&i+1<argc){int is_n=!strcmp(argv[i],"-n");int x=argnum(argv[++i],is_n?12:30);if(!x)return 2;if(is_n)n=x;else interval=x;continue;}return 2;
 }
 if(want_console)console_fd=open("/dev/console",O_WRONLY|O_NONBLOCK|O_NOCTTY);
 struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=watchdog;sigemptyset(&sa.sa_mask);if(sigaction(SIGALRM,&sa,NULL))return 3;
 /* Finite whole-run deadline; doesn't restart on each successful RPC. */
 alarm((unsigned)((n-1)*interval+25));
 row("NETPREFS369 start=1 n=%d interval=%d console=%d max_rows=256\n",n,interval,console_fd>=0);
 for(int j=0;j<n;j++){sample((unsigned)j);if(j+1<n){struct timespec ts={interval,0};while(nanosleep(&ts,&ts)&&errno==EINTR){}}}
 alarm(0);row("NETPREFS369 result=1 samples=%d rows=%u\n",n,rows+1);
 if(console_fd>=0)close(console_fd);return 0;
}
