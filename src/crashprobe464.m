/* Standalone read-only report observer. Not a kernel export or network probe. */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "policy464.h"

static unsigned rows,outputBytes,entries,scanErrors,scanCapped,candidatesDropped,readCount,skippedType,skippedSize,skippedAge;
static double deadline;
static time_t wall;
static unsigned currentSerial;
static time_t currentMtime,currentEpoch;
static double monotime(void){struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return -1;return ts.tv_sec+ts.tv_nsec/1e9;}
static int expired(void){double t=monotime();return t<0||t>deadline;}
static void timeoutHandler(int sig){(void)sig;static const char msg[]="CP464 {\"event\":\"end\",\"status\":\"unknown\",\"reason\":\"deadline\"}\n";ssize_t unused=write(1,msg,sizeof(msg)-1);(void)unused;_exit(124);}
static NSDictionary *dict(id x){return [x isKindOfClass:[NSDictionary class]]?x:nil;}
static NSArray *array(id x){return [x isKindOfClass:[NSArray class]]?x:nil;}
static NSString *string(id x){return [x isKindOfClass:[NSString class]]?x:nil;}
static const char *smallString(id x){NSString *s=string(x);return s&&s.length<192?[s UTF8String]:NULL;}
static BOOL number(id x,uint64_t max,uint64_t *out){
 if(![x isKindOfClass:[NSNumber class]]||CFGetTypeID((__bridge CFTypeRef)x)==CFBooleanGetTypeID())return NO;
 const char *t=[x objCType];if(!t||!strchr("cCsSiIlLqQ",t[0])||[x longLongValue]<0)return NO;
 uint64_t n=[x unsignedLongLongValue];if(n>max)return NO;*out=n;return YES;
}
static void emit(NSDictionary *row){
 if(rows>=230||outputBytes>=60000)return;
 if([row[@"event"] isEqualToString:@"report"]&&[row[@"report"] unsignedIntValue]==currentSerial&&currentSerial){
  NSMutableDictionary *r=[row mutableCopy];BOOL known=currentEpoch>0&&currentMtime>=0;
  r[@"file_time_known"]=@(known);r[@"mtime_epoch"]=known?@((long long)currentMtime):@(-1);
  r[@"current_epoch"]=known?@((long long)currentEpoch):@(-1);r[@"age_seconds"]=known?@((long long)currentEpoch-(long long)currentMtime):@(-1);
  r[@"file_time_not_crash_time"]=@YES;row=r;
 }
 NSData *data=[NSJSONSerialization dataWithJSONObject:row options:0 error:NULL];
 if(!data||data.length>2048||outputBytes+data.length+7>60000)return;
 char line[2055];memcpy(line,"CP464 ",6);memcpy(line+6,data.bytes,data.length);line[6+data.length]='\n';size_t length=data.length+7;
 /* Exactly one write per complete line. Kernel prints can still interleave;
    a short/error write ends this run, never retries a private partial fragment. */
 if(write(STDOUT_FILENO,line,length)!=(ssize_t)length)_exit(74);
 outputBytes+=(unsigned)length;rows++;
}
static void unknown(unsigned n,const char *reason,int err){emit(@{@"event":@"report",@"report":@(n),@"status":@"unknown",@"reason":@(reason),@"errno":@(err)});}
static NSString *fixed(id x,NSArray *allow){NSString *s=string(x);return s&&[allow containsObject:s]?s:@"unknown";}
static BOOL trueValue(id x){return x&&CFGetTypeID((__bridge CFTypeRef)x)==CFBooleanGetTypeID()&&[x boolValue];}
static void reasonFlags(id value,NSMutableSet *tags){
 NSString *s=string(value);if(!s||s.length>8192)return;
 /* Never print source text. Only constant watchdog event terms. */
 for(NSString *term in @[@"scene-create",@"scene-update",@"scene-destroy",@"process-launch",@"process-exit",@"suspend",@"resume",@"watchdog",@"wall clock",@"CPU"])
  if([s rangeOfString:term options:NSCaseInsensitiveSearch].location!=NSNotFound)[tags addObject:term];
}
static NSString *imageKind(id image){return fixed(dict(image)[@"name"],@[@"MobileSafari",@"WebKit",@"JavaScriptCore",@"UIKitCore",@"Foundation",@"CoreFoundation",@"libsystem_kernel.dylib",@"libsystem_pthread.dylib",@"libdispatch.dylib",@"libobjc.A.dylib",@"libsystem_c.dylib",@"dyld",@"com.apple.WebKit.Networking",@"com.apple.WebKit.WebContent",@"com.apple.WebKit.GPU"]);}
static void stack(unsigned serial,NSArray *threads,NSArray *images,NSUInteger index,BOOL isMain,BOOL triggered){
 NSDictionary *t=dict(threads[index]);NSArray *frames=array(t[@"frames"]);
 emit(@{@"event":@"thread",@"report":@(serial),@"index":@(index),@"main_confirmed":@(isMain),@"thread0_candidate":@(index==0&&!isMain),@"triggered":@(triggered),@"frames_known":@(frames!=nil),@"frames_total":@(frames.count),@"frames_capped":@(frames.count>CP_FRAME_MAX)});
 for(NSUInteger i=0;i<MIN(frames.count,CP_FRAME_MAX)&&!expired();i++){
  NSDictionary *f=dict(frames[i]);uint64_t ii=0,off=0,loc=0;
  BOOL ik=number(f[@"imageIndex"],65535,&ii)&&ii<images.count;
  BOOL ok=number(f[@"imageOffset"],((uint64_t)1<<40)-1,&off),lk=number(f[@"symbolLocation"],UINT32_MAX,&loc);
  const char *sym=cp_symbol(smallString(f[@"symbol"]));
  emit(@{@"event":@"frame",@"report":@(serial),@"thread":@(index),@"frame":@(i),@"symbol":@(sym),@"symbol_known":@(strcmp(sym,"unknown")!=0),@"symbol_family":@(cp_family(smallString(f[@"symbol"]))),@"image_kind":ik?imageKind(images[ii]):@"unknown",@"image_index_known":@(ik),@"image_index":ik?@(ii):@(-1),@"image_offset_known":@(ok),@"image_offset":ok?@(off):@(-1),@"symbol_location_known":@(lk),@"symbol_location":lk?@(loc):@(-1)});
 }
}
static void parseReport(NSData *data,unsigned serial){
 const unsigned char *b=data.bytes;size_t n=data.length,split=0;
 while(split<n&&split<CP_HEADER_MAX&&b[split]!='\n')split++;
 if(split==n||split==CP_HEADER_MAX||!cp_json_budget(b,split)||!cp_json_budget(b+split+1,n-split-1)){unknown(serial,"format_or_budget",0);return;}
 NSDictionary *header=dict([NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:b length:split] options:0 error:NULL]);
 NSDictionary *body=dict([NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:b+split+1 length:n-split-1] options:0 error:NULL]);
 uint64_t bt=0;BOOL kind=[string(header[@"bug_type"]) isEqualToString:@"309"]|| (number(header[@"bug_type"],999,&bt)&&bt==309);
 if(!header||!body||!kind){unknown(serial,"unsupported_report",0);return;}
 int role=cp_role(smallString(body[@"procName"]));int hr=cp_role(smallString(header[@"name"]));if(!hr)hr=cp_role(smallString(header[@"app_name"]));
 if(!role||(hr&&role!=hr)){unknown(serial,"process_unconfirmed",0);return;}
 NSDictionary *term=dict(body[@"termination"]);uint64_t code=0;BOOL ck=number(term[@"code"],UINT32_MAX,&code);
 if(!ck){uint32_t v=0;if(cp_u32(smallString(term[@"code"]),&v)){ck=YES;code=v;}}
 NSMutableSet *tags=[NSMutableSet set];reasonFlags(term[@"indicator"],tags);reasonFlags(term[@"description"],tags);
 for(NSString *key in @[@"details",@"reasons",@"explanation"]){id v=term[key];reasonFlags(v,tags);NSArray *a=array(v);for(NSUInteger i=0;i<MIN(a.count,8u);i++)reasonFlags(a[i],tags);}
 /* Some reports put structured explanation in asi. No arbitrary recursive walk. */
 NSDictionary *asi=dict(body[@"asi"]);for(NSString *key in @[@"SpringBoard",@"FrontBoard",@"RunningBoard"]){NSArray *a=array(asi[key]);for(NSUInteger i=0;i<MIN(a.count,8u);i++)reasonFlags(a[i],tags);}
 NSDictionary *exc=dict(body[@"exception"]);uint64_t pid=0;BOOL pk=number(body[@"pid"],INT32_MAX,&pid);
 emit(@{@"event":@"report",@"report":@(serial),@"status":@"parsed",@"role":@(role),@"pid_known":@(pk),@"pid":pk?@(pid):@(-1),@"termination_known":@(term!=nil),@"namespace":fixed(term[@"namespace"],@[@"FRONTBOARD",@"SPRINGBOARD",@"RUNNINGBOARD",@"SIGNAL",@"WATCHDOG",@"CODESIGNING",@"DYLD",@"LIBSYSTEM"]),@"code_known":@(ck),@"code":ck?@(code):@(-1),@"watchdog_code":@(ck&&code==0x8badf00d),@"reason_tags":[[tags allObjects] sortedArrayUsingSelector:@selector(compare:)],@"description_verbatim":@NO,@"exception_type":fixed(exc[@"type"],@[@"EXC_CRASH",@"EXC_BAD_ACCESS",@"EXC_RESOURCE",@"EXC_GUARD",@"EXC_BREAKPOINT"]),@"signal":fixed(exc[@"signal"],@[@"SIGKILL",@"SIGABRT",@"SIGSEGV",@"SIGBUS",@"SIGTRAP"]),@"atomic_snapshot":@NO});
 NSArray *threads=array(body[@"threads"]),*images=array(body[@"usedImages"]);
 if(!threads||!threads.count||threads.count>CP_THREAD_MAX||images.count>4096){unknown(serial,"thread_layout_or_budget",0);return;}
 NSUInteger mainIndex=0;BOOL mainKnown=NO;NSUInteger trigger=NSNotFound;unsigned triggers=0;uint64_t ft=0;
 for(NSUInteger i=0;i<threads.count;i++){NSDictionary *t=dict(threads[i]);if([string(t[@"queue"]) isEqualToString:@"com.apple.main-thread"]){if(mainKnown){unknown(serial,"ambiguous_main_thread",0);return;}mainKnown=YES;mainIndex=i;}if(trueValue(t[@"triggered"])){trigger=i;triggers++;}}
 if(triggers>1){unknown(serial,"ambiguous_trigger",0);return;}
 if(number(body[@"faultingThread"],threads.count-1,&ft)){if(triggers&&trigger!=ft){unknown(serial,"trigger_mismatch",0);return;}trigger=(NSUInteger)ft;}
 stack(serial,threads,images,mainIndex,mainKnown,trigger==mainIndex);
 if(trigger!=NSNotFound&&trigger!=mainIndex)stack(serial,threads,images,trigger,NO,YES);
}
typedef struct {char name[256];unsigned dir;struct stat st;} Candidate;
static Candidate candidates[CP_CANDIDATE_MAX];static unsigned count;
static int same(struct stat a,struct stat b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;}
static void collect(int fd,unsigned dir){
 int copyfd=dup(fd);if(copyfd<0){scanErrors++;return;}DIR *d=fdopendir(copyfd);if(!d){close(copyfd);scanErrors++;return;}
 while(!expired()&&entries<CP_ENTRY_MAX){errno=0;struct dirent *de=readdir(d);if(!de){if(errno)scanErrors++;break;}entries++;
  if(!cp_filename(de->d_name))continue;struct stat st;if(fstatat(fd,de->d_name,&st,AT_SYMLINK_NOFOLLOW)){scanErrors++;continue;}
  if(!S_ISREG(st.st_mode)||st.st_nlink!=1){skippedType++;continue;}
  if(st.st_size<=0||st.st_size>CP_FILE_MAX){skippedSize++;continue;}
  if(st.st_mtimespec.tv_sec>wall+300||st.st_mtimespec.tv_sec<wall-86400){skippedAge++;continue;}
  Candidate c={.dir=dir,.st=st};memcpy(c.name,de->d_name,strlen(de->d_name)+1);
  if(count<CP_CANDIDATE_MAX)candidates[count++]=c;
  else{unsigned oldest=0;for(unsigned i=1;i<count;i++)if(candidates[i].st.st_mtimespec.tv_sec<candidates[oldest].st.st_mtimespec.tv_sec)oldest=i;
   if(c.st.st_mtimespec.tv_sec>candidates[oldest].st.st_mtimespec.tv_sec)candidates[oldest]=c;candidatesDropped++;}
 }
 if(expired()||entries>=CP_ENTRY_MAX)scanCapped=1;closedir(d);
}
static int newer(const void *a,const void *b){time_t x=((const Candidate*)a)->st.st_mtimespec.tv_sec,y=((const Candidate*)b)->st.st_mtimespec.tv_sec;return x==y?0:(x>y?-1:1);}
static int productionRoot(void){
 const char *parts[]={"private","var","mobile","Library","Logs","CrashReporter"};int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
 for(size_t i=0;i<sizeof(parts)/sizeof(parts[0])&&fd>=0;i++){int next=openat(fd,parts[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);int e=errno;close(fd);fd=next;errno=e;}return fd;
}
int main(int argc,char **argv){@autoreleasepool{
 signal(SIGPIPE,SIG_IGN);signal(SIGALRM,timeoutHandler);alarm(15);setvbuf(stdout,NULL,_IONBF,0);
 double start=monotime();wall=time(NULL);if(start<0||wall==(time_t)-1){unknown(0,"clock",errno);return 1;}deadline=start+10;
 int root=-1;
#ifdef CP464_TEST
 (void)productionRoot;
 if(argc==3&&!strcmp(argv[1],"--fixture-root"))root=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);else {unknown(0,"arguments",0);return 2;}
#else
 if(argc==2&&!strcmp(argv[1],"-c")){
  int console=open("/dev/console",O_WRONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
  if(console<0){unknown(0,"console_unavailable",errno);return 1;}
  if(dup2(console,STDOUT_FILENO)<0){int e=errno;close(console);unknown(0,"console_redirect",e);return 1;}
  if(console!=STDOUT_FILENO)close(console);
 }else if(argc!=1){unknown(0,"arguments",0);return 2;}root=productionRoot();
#endif
 if(root<0){unknown(0,"root_unavailable",errno);emit(@{@"event":@"end",@"status":@"unknown",@"absence_proven":@NO});return 1;}
 int dirs[2]={root,-1};dirs[1]=openat(root,"Retired",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(dirs[1]<0&&errno!=ENOENT)scanErrors++;
 for(unsigned i=0;i<2;i++)if(dirs[i]>=0)collect(dirs[i],i);
 qsort(candidates,count,sizeof(Candidate),newer);
 for(unsigned i=0;i<count&&readCount<CP_READ_MAX&&!expired();i++){@autoreleasepool{
  Candidate *c=&candidates[i];unsigned serial=++readCount;currentSerial=serial;currentMtime=c->st.st_mtimespec.tv_sec;currentEpoch=time(NULL);
  int fd=openat(dirs[c->dir],c->name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  if(fd<0){unknown(serial,"open",errno);continue;}struct stat before,after,pathAfter;
  if(fstat(fd,&before)||!same(c->st,before)||!S_ISREG(before.st_mode)||before.st_nlink!=1){close(fd);unknown(serial,"changed_before_read",0);continue;}
  size_t size=(size_t)before.st_size;NSMutableData *data=[NSMutableData dataWithLength:size];unsigned char *dst=data.mutableBytes;size_t used=0;int err=0;
  while(used<size&&!expired()){ssize_t got=read(fd,dst+used,MIN(size-used,65536u));if(got<0){if(errno==EINTR)continue;err=errno;break;}if(!got)break;used+=(size_t)got;}
  int stable=!fstat(fd,&after)&&!fstatat(dirs[c->dir],c->name,&pathAfter,AT_SYMLINK_NOFOLLOW)&&same(before,after)&&same(after,pathAfter);close(fd);
  if(used!=size||err||!stable||expired()){unknown(serial,"incomplete_or_changed",err);continue;}
  @try{parseReport(data,serial);}@catch(NSException *exception){(void)exception;unknown(serial,"parser_exception",0);}
 }}
 for(unsigned i=0;i<2;i++)if(dirs[i]>=0)close(dirs[i]);
 emit(@{@"event":@"end",@"status":@"bounded_observation",@"entries":@(entries),@"candidate_count":@(count),@"candidates_dropped":@(candidatesDropped),@"read_count":@(readCount),@"reports_not_read":@(count-readCount),@"skipped_type":@(skippedType),@"skipped_size":@(skippedSize),@"skipped_age":@(skippedAge),@"scan_errors":@(scanErrors),@"scan_capped":@(scanCapped),@"time_capped":@(expired()),@"absence_proven":@NO,@"atomic_snapshot":@NO});alarm(0);return 0;
}}
