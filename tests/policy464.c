#include <assert.h>
#include <stdio.h>
#include "../src/policy464.h"
int main(void){
 uint32_t v=0;assert(cp_u32("0x8badf00d",&v)&&v==0x8badf00d);assert(cp_u32("2343432205",&v)&&v==0x8badf00d);
 assert(cp_u32("4294967295",&v)&&v==UINT32_MAX);
 const char *bad[]={"-1","+2","4294967296","0x100000000","nan","1.0","1e3","0x","0xdead/path","999999999999999"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!cp_u32(bad[i],&v));
 assert(cp_filename("MobileSafari-test.ips"));assert(cp_filename("com.apple.WebKit.GPU-test.ips"));assert(!cp_filename("Other-test.ips"));assert(!cp_filename("MobileSafari-test.ips.tmp"));
 assert(cp_role("com.apple.WebKit.Networking")==2&&!cp_role("/Users/private/MobileSafari"));
 assert(!strcmp(cp_symbol("mach_msg2_trap"),"mach_msg2_trap"));
 const char *private[]={"https://private.example","private_user","/private/var/mobile","01234567-89AB-CDEF-0123-456789ABCDEF","mach_msg2_trap\nprivate_user","__private_user"};
 for(unsigned i=0;i<sizeof(private)/sizeof(private[0]);i++)assert(!strcmp(cp_symbol(private[i]),"unknown"));
 assert(!strcmp(cp_family("IPC::Connection::waitForSyncReply(private_user)"),"ipc_sync_wait"));
 unsigned char deep[150];memset(deep,'[',75);memset(deep+75,']',75);assert(!cp_json_budget(deep,sizeof deep));
 assert(cp_json_budget((const unsigned char*)"{\"x\":\"[escaped]\\\"\"}",strlen("{\"x\":\"[escaped]\\\"\"}")));
 assert(!cp_json_budget((const unsigned char*)"{",1));assert(!cp_json_budget((const unsigned char*)"\"bad",4));
 for(unsigned i=0;i<65536;i++){char s[40];snprintf(s,sizeof s,"%u",i);assert(cp_u32(s,&v)&&v==i);}
 puts("PASS bounded numeric, JSON complexity and exact output vocabulary policies; 65536 numeric cases");return 0;
}
