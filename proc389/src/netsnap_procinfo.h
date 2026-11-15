/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Own minimal mirror of the XNU proc_info ABI (arm64/LP64) needed by netsnap357, because the iPhoneOS SDK does not ship
 * <sys/proc_info.h>. Only the fields that are read are named; everything else is opaque padding. CI compiles
 * tests/abi_check.c against the real macOS <sys/proc_info.h> and asserts every offset/size below is identical. An independent source-only
 * check_actual_xnu_abi.py also checks the pinned running-kernel XNU10063 header, including alignment.
 */
#ifndef NETSNAP_PROCINFO_H
#define NETSNAP_PROCINFO_H
#include <stdint.h>

#define NS_PROC_PIDLISTFDS 1
#define NS_PROC_PIDTASKINFO 4
#define NS_PROC_PIDFDSOCKETINFO 3
#define NS_PROX_FDTYPE_SOCKET 2
#define NS_PROX_FDTYPE_NETPOLICY 9
#define NS_PROX_FDTYPE_CHANNEL 10
#define NS_SOCKINFO_IN 1
#define NS_SOCKINFO_TCP 2

struct ns_proc_fdinfo {
    int32_t proc_fd;
    uint32_t proc_fdtype;
};

struct ns_proc_taskinfo {
    uint64_t pti_virtual_size;
    uint64_t pti_resident_size;
    uint64_t pti_total_user;
    uint64_t pti_total_system;
    uint64_t pti_threads_user;
    uint64_t pti_threads_system;
    int32_t pti_policy;
    int32_t pti_faults;
    int32_t pti_pageins;
    int32_t pti_cow_faults;
    int32_t pti_messages_sent;
    int32_t pti_messages_received;
    int32_t pti_syscalls_mach;
    int32_t pti_syscalls_unix;
    int32_t pti_csw;
    int32_t pti_threadnum;
    int32_t pti_numrunning;
    int32_t pti_priority;
};

struct __attribute__((aligned(8))) ns_in_sockinfo {
    int insi_fport;           /* network byte order in the low 16 bits */
    int insi_lport;
    uint8_t opaque_8_80[72];  /* gencnt, flags, flow, vflag, ttl, addresses (never read) */
};

struct __attribute__((aligned(8))) ns_tcp_sockinfo {
    struct ns_in_sockinfo tcpsi_ini;
    int tcpsi_state;
    uint8_t opaque_84_120[36];
};

struct __attribute__((aligned(8))) ns_socket_fdinfo {
    uint8_t pfi_opaque[24];   /* struct proc_fileinfo */
    uint8_t soi_opaque_0_156[156]; /* vinfo_stat, so, pcb, type */
    int soi_protocol;         /* psi offset 156 */
    int soi_family;           /* psi offset 160 */
    uint8_t soi_opaque_164_232[68];
    int soi_kind;             /* psi offset 232 */
    uint32_t soi_rfu_1;
    union {
        struct ns_in_sockinfo pri_in;
        struct ns_tcp_sockinfo pri_tcp;
        uint8_t pri_max[528];
    } soi_proto;              /* psi offset 240 */
};

_Static_assert(_Alignof(struct ns_in_sockinfo)==8 && _Alignof(struct ns_tcp_sockinfo)==8 && _Alignof(struct ns_socket_fdinfo)==8, "LP64 socket ABI alignment");
_Static_assert(sizeof(struct ns_proc_fdinfo) == 8, "proc_fdinfo");
_Static_assert(sizeof(struct ns_proc_taskinfo) == 96, "proc_taskinfo");
_Static_assert(sizeof(struct ns_in_sockinfo) == 80, "in_sockinfo");
_Static_assert(sizeof(struct ns_tcp_sockinfo) == 120, "tcp_sockinfo");
_Static_assert(sizeof(struct ns_socket_fdinfo) == 792, "socket_fdinfo");

int proc_listallpids(void *buffer, int buffersize);
int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
int proc_pidfdinfo(int pid, int fd, int flavor, void *buffer, int buffersize);
int proc_name(int pid, void *buffer, uint32_t buffersize);
#endif
