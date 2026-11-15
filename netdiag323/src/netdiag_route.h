/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The routing-socket ABI. The iPhoneOS SDK does not ship <net/route.h>; the kernel ABI is the same on macOS and iOS. These are the
 * definitions of XNU bsd/net/route.h (struct rt_metrics, struct rt_msghdr, the flags/types/address bits used here), checked
 * against the local XNU source copy. With an SDK that has the header, the header is used instead. */
#ifndef NETDIAG_ROUTE_H
#define NETDIAG_ROUTE_H
#if __has_include(<net/route.h>)
#include <net/route.h>
#else
#include <sys/types.h>
#include <stdint.h>
struct rt_metrics {
    u_int32_t rmx_locks, rmx_mtu, rmx_hopcount;
    int32_t rmx_expire;
    u_int32_t rmx_recvpipe, rmx_sendpipe, rmx_ssthresh, rmx_rtt, rmx_rttvar, rmx_pksent;
    u_int32_t rmx_filler[4];
};
struct rt_msghdr {
    u_short rtm_msglen;
    u_char rtm_version;
    u_char rtm_type;
    u_short rtm_index;
    int rtm_flags;
    int rtm_addrs;
    pid_t rtm_pid;
    int rtm_seq;
    int rtm_errno;
    int rtm_use;
    u_int32_t rtm_inits;
    struct rt_metrics rtm_rmx;
};
#define RTF_UP 0x1
#define RTF_GATEWAY 0x2
#define RTF_STATIC 0x800
#define RTF_IFSCOPE 0x1000000
#define RTM_VERSION 5
#define RTM_ADD 0x1
#define RTM_DELETE 0x2
#define RTA_DST 0x1
#define RTA_GATEWAY 0x2
#define RTA_NETMASK 0x4
#define RTAX_DST 0
#define RTAX_GATEWAY 1
#define RTAX_NETMASK 2
#define RTAX_MAX 8
#endif
#ifndef NET_RT_DUMP
#define NET_RT_DUMP 1
#endif
_Static_assert(sizeof(struct rt_msghdr) == 92, "rt_msghdr is 92 bytes in XNU");
#endif
