/* Owned bounded validation helpers; no report content may become output text. */
#ifndef POLICY464_H
#define POLICY464_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CP_FILE_MAX (2u*1024u*1024u)
#define CP_HEADER_MAX 16384u
#define CP_READ_MAX 4u
#define CP_ENTRY_MAX 512u
#define CP_CANDIDATE_MAX 32u
#define CP_FRAME_MAX 24u
#define CP_THREAD_MAX 256u
static int cp_json_budget(const unsigned char *s,size_t n) {
 unsigned depth=0,tokens=0;int quoted=0,escape=0;
 if(!n||n>CP_FILE_MAX)return 0;
 for(size_t i=0;i<n;i++) { unsigned char c=s[i];
  if(!c)return 0;
  if(quoted){if(escape)escape=0;else if(c=='\\')escape=1;else if(c=='"')quoted=0;continue;}
  if(c=='"'){quoted=1;if(++tokens>50000)return 0;}
  else if(c=='{'||c=='['){if(++depth>64||++tokens>50000)return 0;}
  else if(c=='}'||c==']'){if(!depth)return 0;--depth;}
  else if(c==','||c==':'){if(++tokens>50000)return 0;}
 }
 return !quoted&&!depth;
}
static int cp_u32(const char *s,uint32_t *out) {
 if(!s||!*s)return 0;unsigned base=10;size_t i=0;uint64_t v=0;
 if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;i=2;if(!s[i])return 0;}
 for(;s[i];i++){if(i>=12)return 0;unsigned c=(unsigned char)s[i],d;
  if(c>='0'&&c<='9')d=c-'0';else if(base==16&&c>='a'&&c<='f')d=c-'a'+10;else if(base==16&&c>='A'&&c<='F')d=c-'A'+10;else return 0;
  if(d>=base)return 0;v=v*base+d;if(v>UINT32_MAX)return 0;
 }*out=(uint32_t)v;return 1;
}
static int cp_role(const char *s) {
 if(!s)return 0;
 if(!strcmp(s,"MobileSafari"))return 1;
 if(!strcmp(s,"com.apple.WebKit.Networking"))return 2;
 if(!strcmp(s,"com.apple.WebKit.WebContent"))return 3;
 if(!strcmp(s,"com.apple.WebKit.GPU"))return 4;
 return 0;
}
static int cp_filename(const char *s) {
 size_t n=strnlen(s,256);if(n<5||n>=256||strcmp(s+n-4,".ips"))return 0;
 static const char *const starts[]={"MobileSafari-","com.apple.WebKit.Networking-","com.apple.WebKit.WebContent-","com.apple.WebKit.GPU-"};
 for(size_t i=0;i<4;i++)if(!strncmp(s,starts[i],strlen(starts[i])))return 1;
 return 0;
}
/* A character filter alone cannot exclude usernames. Only these exact constant
   spellings are emitted; all unrecognized symbols become the fixed 'unknown'. */
static const char *cp_symbol(const char *s) {
 static const char *const symbols[]={"mach_msg_trap","mach_msg2_trap","mach_msg","mach_msg2_internal","mach_msg_overwrite","__psynch_cvwait","__psynch_mutexwait","__ulock_wait","__ulock_wait2","semaphore_wait_trap","semaphore_timedwait_trap","kevent","kevent64","kevent_qos","__select","__poll","__workq_kernreturn","pthread_cond_wait","_pthread_cond_wait","pthread_mutex_lock","_dispatch_semaphore_wait_slow","_dispatch_group_wait_slow","_dispatch_sync_f_slow","_dispatch_sync_f","dispatch_sync","_dispatch_main_queue_callback_4CF","_dispatch_client_callout","_dispatch_call_block_and_release","__CFRunLoopRun","CFRunLoopRunSpecific","__CFRunLoopServiceMachPort","__CFRUNLOOP_IS_SERVICING_THE_MAIN_DISPATCH_QUEUE__","CFRunLoopRun","UIApplicationMain","GSEventRunModal","start","main","abort","__pthread_kill","pthread_kill","objc_msgSend"};
 if(!s||strnlen(s,192)>=192)return "unknown";
 for(size_t j=0;s[j];j++)if(!((s[j]>='a'&&s[j]<='z')||(s[j]>='A'&&s[j]<='Z')||(s[j]>='0'&&s[j]<='9')||s[j]=='_'))return "unknown";
 for(size_t i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++)if(!strcmp(s,symbols[i]))return symbols[i];
 return "unknown";
}
static const char *cp_family(const char *s){
 if(!s||strnlen(s,512)>=512)return "unknown";
 static const char *const prefixes[]={"IPC::Connection::waitForSyncReply(","IPC::Connection::sendSyncMessage(","IPC::Connection::waitForMessage(","WTF::RunLoop::run(","WebKit::WebPageProxy::loadRequest("};
 static const char *const tags[]={"ipc_sync_wait","ipc_sync_send","ipc_message_wait","runloop","webkit_load"};
 for(unsigned i=0;i<5;i++)if(!strncmp(s,prefixes[i],strlen(prefixes[i])))return tags[i];return "unknown";
}
#endif
