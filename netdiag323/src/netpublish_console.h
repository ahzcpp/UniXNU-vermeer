/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Optional own log mirror only. Never changes stdout, network state or policy. */
#ifndef NETPUBLISH_CONSOLE_H
#define NETPUBLISH_CONSOLE_H
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>
static int np_console_fd=-1;
static unsigned np_console_rows;
static inline int np_console_enable(void) {
 int saved=errno;if(np_console_fd>=0)return 0;
 int fd=open("/dev/console",O_WRONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -1;
 struct stat st;
 if(fstat(fd,&st)){int e=errno;close(fd);errno=e;return -1;}
 if(!S_ISCHR(st.st_mode)){close(fd);errno=ENODEV;return -1;}
 int flags=fcntl(fd,F_GETFL);
 if(flags<0){int e=errno;close(fd);errno=e;return -1;}
 int fdflags=fcntl(fd,F_GETFD);
 if(fdflags<0){int e=errno;close(fd);errno=e;return -1;}
 if(!(flags&O_NONBLOCK)||!(fdflags&FD_CLOEXEC)){close(fd);errno=ENOTSUP;return -1;}
 np_console_fd=fd;errno=saved;return 0;
}
static inline void np_console_emit(const char *line,size_t bytes) {
 int saved=errno;
 /* A single attempt per existing formatted row; no retry or blocking fallback.
  * Cap is per process (fork inherits it), in addition to the700-byte line cap. */
 if(np_console_fd>=0&&line&&bytes>0&&bytes<700&&np_console_rows<256){
  np_console_rows++;(void)write(np_console_fd,line,bytes);
 }
 errno=saved;
}
#endif
