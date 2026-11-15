/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WATCHDOG437_H
#define WATCHDOG437_H
#include <signal.h>
#include <unistd.h>
static volatile sig_atomic_t console_fd=-1;
static volatile sig_atomic_t stdout_enabled;
static void watchdog(int sig) {
 (void)sig;static const char s[]="NETBODY437 deadline=1 exit_code=124\n";
 ssize_t ignored;if(stdout_enabled){ignored=write(STDOUT_FILENO,s,sizeof s-1);(void)ignored;}
 int fd=(int)console_fd;if(fd>=0){ignored=write(fd,s,sizeof s-1);(void)ignored;}_exit(124);
}
#endif
