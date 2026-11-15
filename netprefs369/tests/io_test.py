from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/unixnu-netprefs369.c').read_text()
body=s[s.index('static int console_fd'):s.index('static int argnum')].replace('static volatile sig_atomic_t phase;','')
test=r"""
int main(void) {
 int nullfd=open("/dev/null",O_WRONLY);assert(nullfd>=0);assert(dup2(nullfd,1)==1);close(nullfd);
 for(unsigned i=0;i<300;i++)row("NETPREFS369 fixture=%u\n",i);assert(rows==255);
 int p[2];assert(!pipe(p));int f=fcntl(p[1],F_GETFL);assert(f>=0);assert(fcntl(p[1],F_SETFL,f|O_NONBLOCK)==0);
 char buf[4096]={0};while(write(p[1],buf,sizeof(buf))>0){}assert(errno==EAGAIN||errno==EWOULDBLOCK);
 pid_t child=fork();assert(child>=0);if(child==0){assert(dup2(p[1],1)==1);watchdog(SIGALRM);_exit(99);}
 int status=0;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==124);close(p[0]);close(p[1]);return 0;
}
"""
headers='#include <unistd.h>\n#include <fcntl.h>\n#include <signal.h>\n#include <errno.h>\n#include <stdarg.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include <sys/wait.h>\n'
with tempfile.TemporaryDirectory(prefix='np369-io-') as td:
 p=Path(td);(p/'t.c').write_text(headers+body+test)
 subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True,timeout=5)
print('NETPREFS369 IO PASS actual row cap255; actual watchdog exits124 with full nonblocking output pipe')
