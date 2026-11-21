/* Run on genuine macOS SDK; also check against the actual target XNU header.
 * Existing tests/abi_check.c remains mandatory for the unchanged socket ABI. */
#include <libproc.h>
#include <sys/proc_info.h>
#include <stddef.h>
#include <stdint.h>
/* This test extracts the production mirror instead of maintaining a second one.
 * build450.sh generates abi450_mirror.inc from proc450_webkit.h. */
#include "abi450_mirror.inc"
_Static_assert(PROC_PIDTBSDINFO==NS450_BSDINFO, "BSDINFO flavor");
_Static_assert(PROC_PIDPATHINFO_MAXSIZE==NS450_PATH_BYTES, "path buffer");
_Static_assert(sizeof(struct proc_bsdinfo)==sizeof(struct ns450_bsdinfo), "BSDINFO size");
_Static_assert(_Alignof(struct proc_bsdinfo)==_Alignof(struct ns450_bsdinfo), "BSDINFO alignment");
_Static_assert(offsetof(struct proc_bsdinfo,pbi_pid)==offsetof(struct ns450_bsdinfo,pid), "pid offset");
_Static_assert(offsetof(struct proc_bsdinfo,pbi_start_tvsec)==offsetof(struct ns450_bsdinfo,start_sec), "seconds offset");
_Static_assert(offsetof(struct proc_bsdinfo,pbi_start_tvusec)==offsetof(struct ns450_bsdinfo,start_usec), "useconds offset");
int main(void){return 0;}
