"""Execute exact production traversal on owned temporary fixtures under sanitizers."""
from pathlib import Path
import os,subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1];O=R/'host-out456';O.mkdir(exist_ok=True)
fixture=r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
static void emit(int x,const char *fmt,...){(void)x;(void)fmt;}
static void memory_emit_all(int x,const char *fmt,...){(void)x;(void)fmt;}
static void ns351_memory_snapshot(int n,void (*f)(int,const char*,...)){(void)n;(void)f;}
#include "fs456.h"
static struct fs456_counts run(const char *p) {
 struct stat st;assert(!stat(p,&st));struct fs456_counts c={0};c.device=st.st_dev;c.deadline=fs456_ms()+5000;
 int fd=open(p,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);assert(fd>=0);fs456_walk(fd,0,&c);errno=0;assert(fcntl(fd,F_GETFD)==-1 && errno==EBADF);return c;
}
int main(int argc,char **argv) {
 assert(argc==2);struct fs456_counts c=run(argv[1]);
 assert(c.files==3 && c.dirs==0 && c.links==1 && c.hardlinks==2 && c.other==1);
 assert(c.apparent==3 && c.entries==5 && !c.errors && !c.cap);
 c=(struct fs456_counts){0};c.deadline=fs456_ms()+1000;fs456_walk(-1,0,&c);assert(c.errors==1);
 c=(struct fs456_counts){0};c.deadline=0;assert(fs456_bound(&c) && c.time);
 c=(struct fs456_counts){0};c.entries=FS456_ENTRIES;assert(fs456_bound(&c) && c.cap);
 struct stat st={0};st.st_size=3;st.st_blocks=1;c=(struct fs456_counts){0};c.apparent=UINT64_MAX;fs456_add(&c,&st);assert(c.overflow==1 && c.allocated==0);
 char p[1024];snprintf(p,sizeof p,"%s/deep",argv[1]);assert(!mkdir(p,0700));
 for(int i=0;i<7;i++){size_t n=strlen(p);snprintf(p+n,sizeof p-n,"/d");assert(!mkdir(p,0700));}
 c=run(argv[1]);assert(c.depth && c.dirs==5);
 for(int i=0;i<280;i++){snprintf(p,sizeof p,"%s/f%d",argv[1],i);int fd=open(p,O_CREAT|O_WRONLY|O_EXCL,0600);assert(fd>=0);close(fd);}
 c=run(argv[1]);assert(c.cap && c.entries==FS456_ENTRIES);
 puts("PASS fs456 symlink/hardlink/special exclusion, totals, errors, entry/depth/time caps, overflow; ASan UBSan");return 0;
}
'''
(O/'fixture.c').write_text(fixture)
cmd=['clang','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(R/'src'),str(O/'fixture.c'),'-o',str(O/'fixture')]
subprocess.run(cmd,check=True)
with tempfile.TemporaryDirectory(prefix='fs456-') as tmp:
 p=Path(tmp);(p/'ordinary').write_bytes(b'abc');(p/'shared').write_bytes(b'xxxxxxxx');os.link(p/'shared',p/'shared2');(p/'link').symlink_to('/');os.mkfifo(p/'fifo')
 subprocess.run([str(O/'fixture'),str(p)],check=True,env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
