/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SDK and actual-XNU ABI check: the own mirror in netsnap_procinfo.h must match the real <sys/proc_info.h> exactly. */
#include <libproc.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/proc_info.h>
#include "../src/netsnap_procinfo.h"

_Static_assert(_Alignof(struct in_sockinfo) == _Alignof(struct ns_in_sockinfo), "in_sockinfo alignment");
_Static_assert(_Alignof(struct tcp_sockinfo) == _Alignof(struct ns_tcp_sockinfo), "tcp_sockinfo alignment");
_Static_assert(_Alignof(struct socket_fdinfo) == _Alignof(struct ns_socket_fdinfo), "socket_fdinfo alignment");
#define SAME_OFF(a, fa, b, fb) _Static_assert(offsetof(a, fa) == offsetof(b, fb), #fa)
_Static_assert(sizeof(struct proc_fdinfo) == sizeof(struct ns_proc_fdinfo), "fdinfo size");
_Static_assert(sizeof(struct proc_taskinfo) == sizeof(struct ns_proc_taskinfo), "taskinfo size");
_Static_assert(sizeof(struct socket_fdinfo) == sizeof(struct ns_socket_fdinfo), "socket_fdinfo size");
_Static_assert(sizeof(struct in_sockinfo) == sizeof(struct ns_in_sockinfo), "in_sockinfo size");
_Static_assert(sizeof(struct tcp_sockinfo) == sizeof(struct ns_tcp_sockinfo), "tcp_sockinfo size");
SAME_OFF(struct proc_fdinfo, proc_fdtype, struct ns_proc_fdinfo, proc_fdtype);
SAME_OFF(struct proc_taskinfo, pti_total_user, struct ns_proc_taskinfo, pti_total_user);
SAME_OFF(struct proc_taskinfo, pti_total_system, struct ns_proc_taskinfo, pti_total_system);
SAME_OFF(struct proc_taskinfo, pti_messages_sent, struct ns_proc_taskinfo, pti_messages_sent);
SAME_OFF(struct proc_taskinfo, pti_messages_received, struct ns_proc_taskinfo, pti_messages_received);
SAME_OFF(struct socket_fdinfo, psi.soi_protocol, struct ns_socket_fdinfo, soi_protocol);
SAME_OFF(struct socket_fdinfo, psi.soi_family, struct ns_socket_fdinfo, soi_family);
SAME_OFF(struct socket_fdinfo, psi.soi_kind, struct ns_socket_fdinfo, soi_kind);
SAME_OFF(struct socket_fdinfo, psi.soi_proto, struct ns_socket_fdinfo, soi_proto);
SAME_OFF(struct tcp_sockinfo, tcpsi_state, struct ns_tcp_sockinfo, tcpsi_state);
SAME_OFF(struct in_sockinfo, insi_fport, struct ns_in_sockinfo, insi_fport);
_Static_assert(PROC_PIDLISTFDS == NS_PROC_PIDLISTFDS && PROC_PIDTASKINFO == NS_PROC_PIDTASKINFO &&
               PROC_PIDFDSOCKETINFO == NS_PROC_PIDFDSOCKETINFO, "flavors");
_Static_assert(PROX_FDTYPE_SOCKET == NS_PROX_FDTYPE_SOCKET && PROX_FDTYPE_NETPOLICY == NS_PROX_FDTYPE_NETPOLICY &&
               PROX_FDTYPE_CHANNEL == NS_PROX_FDTYPE_CHANNEL, "fdtypes");
_Static_assert(SOCKINFO_IN == NS_SOCKINFO_IN && SOCKINFO_TCP == NS_SOCKINFO_TCP, "sockinfo kinds");
int main(void) {
    printf("ABI PASS socket_fdinfo=%zu proc_taskinfo=%zu soi_proto=%zu\n", sizeof(struct socket_fdinfo), sizeof(struct proc_taskinfo),
           offsetof(struct socket_fdinfo, psi.soi_proto));
    return 0;
}
