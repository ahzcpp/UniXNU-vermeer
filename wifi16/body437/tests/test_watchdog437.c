#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include "../src/Watchdog437.h"
#include "../src/Identity424.h"
static struct nw424_ids ids(void*v){(void)v;return(struct nw424_ids){0};}
static int ok(void*v){(void)v;return 0;}
static int hang(void*v){(void)v;for(;;)pause();}
static int count(void*v){(void)v;return 1;}
static int one(void*v,uint32_t*out){(void)v;*out=501;return 1;}
int main(int argc,char**argv){assert(argc==2);struct sigaction a;memset(&a,0,sizeof a);a.sa_handler=watchdog;assert(!sigemptyset(&a.sa_mask)&&!sigaction(SIGALRM,&a,0));alarm(1);
 if(!strcmp(argv[1],"no-sink")){for(;;)pause();}
 assert(!strcmp(argv[1],"uid-callback-hang"));int flags=fcntl(1,F_GETFL);assert(flags>=0&&!fcntl(1,F_SETFL,flags|O_NONBLOCK));console_fd=1;stdout_enabled=0;
 struct nw424_identity_ops op={0,ids,ok,ok,hang,count,one,ok};struct nw424_identity r;(void)nw424_drop_identity(&op,&r);return 99;}
