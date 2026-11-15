/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Read-only, finite Network.framework / NSURLSession comparison for an OS port.
 * Same own executable can run under root or mobile. Neither identity stands in
 * for Safari/AppStore. Reads no account, cookie, profile or user preference.
 * Fixed public HEAD requests, normal TLS validation, no authentication handler,
 * no redirects, no response bodies retained, no arbitrary URL CLI.
 */
#import <Foundation/Foundation.h>
#import <Network/Network.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <grp.h>
#include <stdint.h>
#include "Identity424.h"
#include "FdGate424.h"
#include "Watchdog424.h"
#include "netpath_classify.h"

#define MAX_ROWS 96
#define REQUEST_SECONDS 8
#define PROCESS_SECONDS 60

static atomic_uint output_rows;
static void rawline(const char *s,size_t n) {
 unsigned row=atomic_fetch_add(&output_rows,1);
 if(row>=MAX_ROWS)return;
 ssize_t ignored;if(stdout_enabled){ignored=write(STDOUT_FILENO,s,n);(void)ignored;}
 if(console_fd>=0){ignored=write(console_fd,s,n);(void)ignored;}
}
static void emit(const char *fmt,...) __attribute__((format(printf,1,2)));
static void emit(const char *fmt,...) {
 char b[640];int off=snprintf(b,sizeof b,"NETPATH424 ");va_list ap;va_start(ap,fmt);int n=vsnprintf(b+off,sizeof b-(size_t)off-1,fmt,ap);va_end(ap);if(n<0)return;
 size_t len=(size_t)off+(size_t)n;if(len>sizeof b-2)len=sizeof b-2;b[len++]='\n';rawline(b,len);
}
static long long mono_ms(void) {
 struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
 return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void token(NSString *s,char b[72]) {
 const char *p=s.UTF8String;if(!p){b[0]='-';b[1]=0;return;}size_t n=0;
 for(;n<70&&p[n];n++){unsigned char c=(unsigned char)p[n];b[n]=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-')?(char)c:'?';}b[n]=0;
}

struct path_result { int known,status,reason,expensive,constrained,dns,ipv4,ipv6,wifi,cellular,wired; };
static void inspect_path(void) {
 NSObject *guard=[NSObject new];__block struct path_result value={0};
 dispatch_semaphore_t seen=dispatch_semaphore_create(0);
 dispatch_queue_t q=dispatch_queue_create("org.unixnu.diag.netpath424.path",DISPATCH_QUEUE_SERIAL);
 nw_path_monitor_t m=nw_path_monitor_create();
 if(!m){emit("path known=0 reason=create_failed");return;}
 nw_path_monitor_set_update_handler(m,^(nw_path_t p){
  @synchronized(guard){if(value.known)return;value.known=1;value.status=(int)nw_path_get_status(p);value.reason=(int)nw_path_get_unsatisfied_reason(p);value.expensive=nw_path_is_expensive(p);value.constrained=nw_path_is_constrained(p);value.dns=nw_path_has_dns(p);value.ipv4=nw_path_has_ipv4(p);value.ipv6=nw_path_has_ipv6(p);value.wifi=nw_path_uses_interface_type(p,nw_interface_type_wifi);value.cellular=nw_path_uses_interface_type(p,nw_interface_type_cellular);value.wired=nw_path_uses_interface_type(p,nw_interface_type_wired);}
  dispatch_semaphore_signal(seen);
 });
 nw_path_monitor_set_queue(m,q);nw_path_monitor_start(m);
 (void)dispatch_semaphore_wait(seen,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC));nw_path_monitor_cancel(m);
 struct path_result r;@synchronized(guard){r=value;}
 emit("path known=%d status=%d unsatisfied_reason=%d expensive=%d constrained=%d dns=%d ipv4=%d ipv6=%d wifi=%d cellular=%d wired=%d",r.known,r.status,r.reason,r.expensive,r.constrained,r.dns,r.ipv4,r.ipv6,r.wifi,r.cellular,r.wired);
}

/* Each delegate is owned by one session. Completion is the publication barrier;
 * a deadline never reads delegate fields still being modified by callbacks. */
@interface Probe424 : NSObject <NSURLSessionDataDelegate>
@property(nonatomic,strong) dispatch_semaphore_t done;
@property(nonatomic,strong) dispatch_semaphore_t metricsDone;
@property(nonatomic,strong) NSLock *guard;
@property(nonatomic,assign) long status;
@property(nonatomic,assign) BOOL gotHTTP;
@property(nonatomic,assign) BOOL cancelledAfterHeaders;
@property(nonatomic,assign) unsigned redirects;
@property(nonatomic,strong) NSError *error;
@property(nonatomic,strong) NSURLSessionTaskMetrics *metrics;
@end
@implementation Probe424
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveResponse:(NSURLResponse *)r completionHandler:(void (^)(NSURLSessionResponseDisposition))h {
 (void)s;(void)t;[self.guard lock];if([r isKindOfClass:[NSHTTPURLResponse class]]){self.status=((NSHTTPURLResponse *)r).statusCode;self.gotHTTP=YES;}self.cancelledAfterHeaders=YES;[self.guard unlock];
 h(NSURLSessionResponseCancel); /* Normal TLS has completed before HTTP headers. Retain no body. */
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t willPerformHTTPRedirection:(NSHTTPURLResponse *)r newRequest:(NSURLRequest *)q completionHandler:(void (^)(NSURLRequest *))h {
 (void)s;(void)t;(void)q;[self.guard lock];self.redirects++;self.gotHTTP=YES;self.status=r.statusCode;[self.guard unlock];h(nil);
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t didCompleteWithError:(NSError *)e {
 (void)s;(void)t;[self.guard lock];self.error=e;[self.guard unlock];dispatch_semaphore_signal(self.done);
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)m {
 (void)s;(void)t;[self.guard lock];self.metrics=m;[self.guard unlock];dispatch_semaphore_signal(self.metricsDone);
}
@end

static int inspect_endpoint(const char *name,NSString *url,BOOL waits) {
 NSURLSessionConfiguration *c=[NSURLSessionConfiguration ephemeralSessionConfiguration];
 c.HTTPCookieStorage=nil;c.HTTPShouldSetCookies=NO;c.HTTPCookieAcceptPolicy=NSHTTPCookieAcceptPolicyNever;c.URLCache=nil;c.URLCredentialStorage=nil;
 c.requestCachePolicy=NSURLRequestReloadIgnoringLocalAndRemoteCacheData;c.timeoutIntervalForRequest=REQUEST_SECONDS;c.timeoutIntervalForResource=REQUEST_SECONDS;c.waitsForConnectivity=waits;c.HTTPMaximumConnectionsPerHost=1;
 Probe424 *d=[Probe424 new];d.guard=[NSLock new];d.done=dispatch_semaphore_create(0);d.metricsDone=dispatch_semaphore_create(0);
 NSOperationQueue *q=[NSOperationQueue new];q.maxConcurrentOperationCount=1;
 NSURLSession *s=[NSURLSession sessionWithConfiguration:c delegate:d delegateQueue:q];
 NSMutableURLRequest *r=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:url] cachePolicy:NSURLRequestReloadIgnoringLocalAndRemoteCacheData timeoutInterval:REQUEST_SECONDS];r.HTTPMethod=@"HEAD";r.HTTPShouldHandleCookies=NO;
 NSURLSessionDataTask *t=[s dataTaskWithRequest:r];long long begin=mono_ms();[t resume];
 long w=dispatch_semaphore_wait(d.done,dispatch_time(DISPATCH_TIME_NOW,(REQUEST_SECONDS+2)*NSEC_PER_SEC));
 if(w){[s invalidateAndCancel];emit("endpoint name=%s completed=0 reason=bounded_wait_timeout waits=%d elapsed_ms=%lld",name,waits,begin<0?-1:mono_ms()-begin);return 0;}
 (void)dispatch_semaphore_wait(d.metricsDone,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC));
 [d.guard lock];BOOL got=d.gotHTTP,cancel=d.cancelledAfterHeaders;long status=d.status;unsigned redir=d.redirects;NSError *e=d.error;NSURLSessionTaskMetrics *metrics=d.metrics;[d.guard unlock];
 BOOL intended=cancel&&e&&[e.domain isEqualToString:NSURLErrorDomain]&&e.code==NSURLErrorCancelled;
 BOOL headers=netpath_http_reply(got,status,e!=nil,intended);
 char domain[72];token(e.domain,domain);NSError *under=e.userInfo[NSUnderlyingErrorKey];if(![under isKindOfClass:[NSError class]])under=nil;char udomain[72];token(under.domain,udomain);
 emit("endpoint name=%s completed=1 headers_received=%d http_status=%ld expected_http_success=%d error_domain=%s error_code=%ld underlying_domain=%s underlying_code=%ld cancelled_after_headers=%d expected_cancel=%d redirects_stopped=%u waits=%d elapsed_ms=%lld",name,headers,status,headers&&status>=200&&status<300,domain,(long)e.code,udomain,(long)under.code,cancel,intended,redir,waits,begin<0?-1:mono_ms()-begin);
 NSUInteger i=0;for(NSURLSessionTaskTransactionMetrics *m in metrics.transactionMetrics){if(i++>=2)break;char proto[72];token(m.networkProtocolName,proto);emit("metrics name=%s protocol=%s proxy=%d reused=%d tls_known=%d tls_version=%u dns_known=%d connect_known=%d",name,proto,m.proxyConnection,m.reusedConnection,m.negotiatedTLSProtocolVersion!=nil,m.negotiatedTLSProtocolVersion.unsignedIntValue,m.domainLookupEndDate!=nil,m.connectEndDate!=nil);}
 [s finishTasksAndInvalidate];return headers;
}

/* Real libSystem call, not a stub. Native XNU proc_info.h: flavor1, 8-byte fd entries.
 * Declared here because iPhoneOS SDK visibility of libproc.h varies. No fallback.
 * Only getpid() is queried; no process/account enumeration or account lookup.
 */
extern int proc_pidinfo(int, int, uint64_t, void *, int);
_Static_assert(sizeof(uid_t)==4&&sizeof(gid_t)==4,"native XNU ID width");
_Static_assert(sizeof(sig_atomic_t)>=sizeof(int),"signal-safe descriptor width");
static struct nw424_ids native_ids(void *unused) {
 (void)unused;return (struct nw424_ids){(uint32_t)getuid(),(uint32_t)geteuid(),(uint32_t)getgid(),(uint32_t)getegid()};
}
static int native_groups(void *unused){(void)unused;return setgroups(0,NULL);}
static int native_gid(void *unused){(void)unused;return setgid((gid_t)NW424_MOBILE_ID);}
static int native_uid(void *unused){(void)unused;return setuid((uid_t)NW424_MOBILE_ID);}
static int native_group_count(void *unused){(void)unused;return getgroups(0,NULL);}
static int native_group_one(void *unused,uint32_t *out){
 (void)unused;gid_t one=0;int rc=getgroups(1,&one);if(rc==1)*out=(uint32_t)one;return rc;
}
static int native_errno(void *unused){(void)unused;return errno;}
static int native_fd_list(void *unused,struct nw424_fdentry *out,size_t count){
 (void)unused;errno=0;return proc_pidinfo(getpid(),1,0,out,(int)(count*sizeof(*out)));
}
static int native_fd_close(void *unused,int fd){(void)unused;return close(fd);}
static int open_console424(int *stage,int *saved) {
 *saved=0;*stage=1;int fd=open("/dev/console",O_WRONLY|O_NONBLOCK|O_NOCTTY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){*saved=errno;return -1;}
 struct stat st;*stage=2;if(fstat(fd,&st)){*saved=errno;close(fd);return -1;}
 *stage=3;if(!S_ISCHR(st.st_mode)){close(fd);return -1;}
 *stage=4;int flags=fcntl(fd,F_GETFL);if(flags<0){*saved=errno;close(fd);return -1;}
 if((flags&O_ACCMODE)!=O_WRONLY||!(flags&O_NONBLOCK)){close(fd);return -1;}
 if(fd<3){*stage=5;int copy=fcntl(fd,F_DUPFD_CLOEXEC,3);if(copy<0){*saved=errno;close(fd);return -1;}close(fd);fd=copy;}
 *stage=6;return fd;
}
static int null_stdio424(int *stage,int *saved) {
 *saved=0;*stage=1;int fd=open("/dev/null",O_RDWR|O_NONBLOCK|O_NOCTTY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){*saved=errno;return -1;}
 struct stat st;*stage=2;if(fstat(fd,&st)){*saved=errno;close(fd);return -1;}
 if(!S_ISCHR(st.st_mode)){close(fd);return -1;}
 for(int i=0;i<3;i++){
  *stage=3;if(fd!=i&&dup2(fd,i)<0){*saved=errno;if(fd>2)close(fd);return -1;}
  *stage=4;if(fcntl(i,F_SETFD,FD_CLOEXEC)<0){*saved=errno;if(fd>2)close(fd);return -1;}
 }
 if(fd>2)close(fd);*stage=5;return 0;
}
static void drop_mobile424(void) {
 struct nw424_ids launch=native_ids(NULL);if(launch.ruid!=0||launch.euid!=0)_exit(19);
 int step,saved;int fd=open_console424(&step,&saved);
 if(fd<0)_exit(20+step); /* No valid console: cannot invent an audit row. Fixed exit, no network. */
 console_fd=fd;emit("drop_begin requested=1 account_lookup=0 target_uid=501 target_gid=501 launch_root_verified=1");
 struct nw424_fdops fops={NULL,native_fd_list,native_fd_close,native_errno};struct nw424_fdresult fr;
 if(nw424_isolate_fds(&fops,fd,&fr)){
  emit("fd_gate phase=0 ok=0 stage=%u sys_errno=%d count=%d closed=%d",fr.stage,fr.error,fr.count,fr.closed);_exit(31);
 }
 emit("fd_gate phase=0 ok=1 stage=%u sys_errno=0 count=%d closed=%d",fr.stage,fr.count,fr.closed);
 struct nw424_identity_ops ops={NULL,native_ids,native_groups,native_gid,native_uid,native_group_count,native_group_one,native_errno};struct nw424_identity id;
 int rc=nw424_drop_identity(&ops,&id);id.ids=native_ids(NULL); /* Failure audit includes current partial-drop IDs. */
 emit("identity ok=%d stage=%u sys_errno=%d uid=%u euid=%u gid=%u egid=%u group_raw_count=%d extra_groups=%d",id.ok,id.stage,id.error,id.ids.ruid,id.ids.euid,id.ids.rgid,id.ids.egid,id.group_raw_count,id.extra_groups);
 if(rc)_exit(40+(int)id.stage); /* Never return to root-mode networking. */
 if(null_stdio424(&step,&saved)){emit("stdio ok=0 stage=%d sys_errno=%d",step,saved);_exit(51);}
 if(nw424_verify_final_fds(&fops,fd,&fr)){emit("fd_gate phase=1 ok=0 stage=%u sys_errno=%d count=%d closed=%d",fr.stage,fr.error,fr.count,fr.closed);_exit(52);}
 struct nw424_ids final=native_ids(NULL);
 if(final.ruid!=501||final.euid!=501||final.rgid!=501||final.egid!=501){emit("identity_final ok=0 uid=%u euid=%u gid=%u egid=%u",final.ruid,final.euid,final.rgid,final.egid);_exit(53);}
 emit("identity_final ok=1 uid=%u euid=%u gid=%u egid=%u inherited_extra_fd=0 stdio_opened_after_drop=1",final.ruid,final.euid,final.rgid,final.egid);
}
int main(int argc,char **argv) {
 /* C-only setup before autoreleasepool, Foundation objects, or network API calls.
  * Linked SDK/dyld initializers have already run; this is not a stock-app persona.
  */
 struct sigaction a;memset(&a,0,sizeof a);a.sa_handler=watchdog;
 if(sigemptyset(&a.sa_mask)||sigaction(SIGALRM,&a,NULL))_exit(3);
 alarm(PROCESS_SECONDS);
 if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)_exit(3);
 int mirror=0,waits=0,control=0,drop=0;
 if(argc>8)_exit(2);
 for(int i=1;i<argc;i++){
  if(!strcmp(argv[i],"-c"))mirror=1;
  else if(!strcmp(argv[i],"--waits"))waits=1;
  else if(!strcmp(argv[i],"--control-only"))control=1;
  else if(!strcmp(argv[i],"--drop-to-mobile")){if(drop)_exit(2);drop=1;}
  else _exit(2);
 }
 if(drop)drop_mobile424();
 else {
  int flags=fcntl(STDOUT_FILENO,F_GETFL);if(flags<0||fcntl(STDOUT_FILENO,F_SETFL,flags|O_NONBLOCK))_exit(3);stdout_enabled=1;
  if(mirror){int step,saved;console_fd=open_console424(&step,&saved);}
 }
 emit("start version=424.1 uid=%u euid=%u gid=%u egid=%u waits=%d control_only=%d console=%d body_retained=0 drop_requested=%d",getuid(),geteuid(),getgid(),getegid(),waits,control,console_fd>=0,drop);
 @autoreleasepool {
  struct timespec wall;int clockok=clock_gettime(CLOCK_REALTIME,&wall)==0;emit("clock known=%d epoch=%lld",clockok,clockok?(long long)wall.tv_sec:0);
  inspect_path();
  int replies=inspect_endpoint("apple_control",@"https://captive.apple.com/hotspot-detect.html",waits);
  int tested=1;if(!control){replies+=inspect_endpoint("baidu_search_landing",@"https://www.baidu.com/",waits);replies+=inspect_endpoint("google_search_control",@"https://www.google.com/generate_204",waits);replies+=inspect_endpoint("store_public_bag",@"https://init.itunes.apple.com/bag.xml",waits);tested=4;}
  emit("result=complete tested=%d http_replies=%d stock_app_success_unverified=1",tested,replies);
 }
 /* Keep watchdog armed through autoreleasepool/session cleanup. */
 alarm(0);int fd=(int)console_fd;console_fd=-1;if(fd>=0)close(fd);_exit(0);
}
