/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * wk463: user-space mirrors of the XNU 10063 (xnu-10063.141.1, d8b80295) private records this observer reads.
 * Sources: public apple-oss-distributions/xnu tag xnu-10063.141.1 (commit d8b80295118ef25ac3a784134bcf95cd8e88109f),
 * byte-identical (sha256) to the kernel tree of the target build for every header listed here:
 *   bsd/sys/socketvar.h   xsocket_n / xsockbuf_n / xsockstat_n / data_stats, inside #pragma pack(4) (427..577)
 *   bsd/netinet/in_pcb.h  xinpcb_n / xinpgen, inside #pragma pack(4) (285..493)
 *   bsd/netinet/tcp_var.h xtcpcb_n, inside #pragma pack(4) (1337..1578); TCPT_NTIMERS_EXT = 4 (tcp_timer.h:73)
 *   bsd/netinet/in_pcblist.c get_pcblist_n(): xinpgen, then per PCB ROUNDUP64-sized records (INPCB,SOCKET,RCVBUF,SNDBUF,
 *                         STATS[,TCPCB]), each starting with u32 len, u32 kind; trailing xinpgen.
 *   bsd/sys/stackshot.h   stackshot_config_t, STACKSHOT_CONFIG_TYPE 1; syscalls.master 491 stack_snapshot_with_config
 *   osfmk/kern/debug.h    STACKSHOT_* flags; osfmk/kern/kcdata.h item/array/container encoding, task_snapshot_v2,
 *                         thread_snapshot_v4, thread_waitinfo_v2, thread_turnstileinfo_v2, dyld_uuid_info_64,
 *                         dyld_shared_cache_loadinfo; osfmk/kern/block_hint.h wait types.
 * Only the offsets used below are relied on; tests/abi_real.c checks them against the real header text. */
#ifndef WK463_ABI_H
#define WK463_ABI_H
#include <stdint.h>
#include <stddef.h>

/* ---- pcblist_n ---- */
#define WK_XSO_SOCKET 0x001u
#define WK_XSO_RCVBUF 0x002u
#define WK_XSO_SNDBUF 0x004u
#define WK_XSO_STATS  0x008u
#define WK_XSO_INPCB  0x010u
#define WK_XSO_TCPCB  0x020u
#define WK_ROUNDUP64(x) (((x) + 7u) & ~(size_t)7u)
#define WK_XINPGEN_SIZE 24u          /* u32 len, u32 count, u64 gen, u64 sogen (pack 4) */
/* xsocket_n (pack 4) */
#define WK_XSO_OFF_SO       8u       /* u64 xso_so: permuted kernel handle, used ONLY as in-process identity */
#define WK_XSO_OFF_ERROR    52u      /* u16 so_error */
#define WK_XSO_OFF_LASTPID  68u      /* pid_t so_last_pid */
#define WK_XSO_OFF_EPID     72u      /* pid_t so_e_pid */
#define WK_XSO_MIN_LEN      76u
/* xsockbuf_n */
#define WK_XSB_OFF_CC       8u       /* u32 sb_cc: bytes queued now */
#define WK_XSB_MIN_LEN      12u
/* xsockstat_n: 4 x data_stats{rxpackets,rxbytes,txpackets,txbytes} (u64) from offset 8 */
#define WK_XST_OFF_STATS    8u
#define WK_XST_TC_MAX       4u
#define WK_XST_MIN_LEN      (8u + 4u * 32u)
/* xinpcb_n */
#define WK_XI_OFF_FPORT     16u      /* u16 inp_fport, network byte order */
#define WK_XI_MIN_LEN       18u
/* xtcpcb_n: len,kind,u64 t_segq(8),int t_dupacks(16),int t_timer[4](20..36),int t_state(36) */
#define WK_XT_OFF_STATE     36u
#define WK_XT_MIN_LEN       40u

/* ---- stackshot ---- */
#define WK_SYS_STACK_SNAPSHOT_WITH_CONFIG 491
#define WK_STACKSHOT_CONFIG_TYPE 1
#define WK_SS_SAVE_LOADINFO   0x02ull
#define WK_SS_KCDATA_FORMAT   0x10000ull
#define WK_SS_NO_IO_STATS     0x800000ull
#define WK_SS_THREAD_WAITINFO 0x1000000ull
typedef struct {
	int      sc_pid;
	uint64_t sc_flags;
	uint64_t sc_delta_timestamp;
	uint64_t sc_buffer;
	uint32_t sc_size;
	uint64_t sc_out_buffer_addr;
	uint64_t sc_out_size_addr;
	uint64_t sc_pagetable_mask;
} wk_stackshot_config_t;          /* natural alignment, as bsd/sys/stackshot.h (no pragma) */

#define WK_KCDATA_BEGIN_STACKSHOT 0x59a25807u
#define WK_KCDATA_BUFFER_END      0xF19158EDu
#define WK_KC_CONTAINER_BEGIN     0x13u
#define WK_KC_CONTAINER_END       0x14u
#define WK_KC_ARRAY_PAD0          0x20u
#define WK_KC_LIBRARY_LOADINFO64  0x31u
#define WK_KC_CONT_TASK           0x903u
#define WK_KC_CONT_THREAD         0x904u
#define WK_KC_TASK_SNAPSHOT       0x905u
#define WK_KC_THREAD_SNAPSHOT     0x906u
#define WK_KC_SHAREDCACHE_LOADINFO 0x908u
#define WK_KC_KERN_STACKLR64      0x914u
#define WK_KC_USER_STACKLR64      0x916u
#define WK_KC_THREAD_WAITINFO     0x91du
#define WK_KC_THREAD_TURNSTILE    0x929u
#define WK_KC_ITEM_HDR            16u
/* task_snapshot_v2 (packed): ts_pid at 7*8 + 7*4 = 84 */
#define WK_TS_OFF_PID             84u
#define WK_TS_MIN_LEN             88u
/* thread_snapshot_v4 (packed) */
#define WK_TH_OFF_ID              0u
#define WK_TH_OFF_USER_TIME       48u
#define WK_TH_OFF_SYS_TIME        56u
#define WK_TH_OFF_SS_FLAGS        64u
#define WK_TH_OFF_STATE           88u
#define WK_TH_OFF_SCHED_PRIO      98u
#define WK_TH_MIN_LEN             100u
#define WK_TH_FLAG_MAIN           0x2000ull
#define WK_TH_FLAG_ONCORE         0x800ull
/* thread_waitinfo_v2 (packed, 31 bytes) */
#define WK_WI_OFF_OWNER   0u
#define WK_WI_OFF_WAITER  8u
#define WK_WI_OFF_TYPE    24u
#define WK_WI_OFF_FLAGS   27u
#define WK_WI_MIN_LEN     25u
#define WK_WI_FLAG_SPECIALREPLY 0x1u
/* thread_turnstileinfo_v2 (packed, 36 bytes) */
#define WK_TSI_OFF_WAITER  0u
#define WK_TSI_OFF_CONTEXT 8u
#define WK_TSI_OFF_HOPS    17u
#define WK_TSI_OFF_FLAGS   18u
#define WK_TSI_MIN_LEN     26u
#define WK_TSI_STATUS_THREAD  0x08ull
#define WK_TSI_STATUS_ON_TASK 0x10ull
/* dyld_uuid_info_64: u64 load address, uuid[16]; dyld_shared_cache_loadinfo: u64 slide, uuid[16], u64 base, u64 firstmapping */
#define WK_SC_OFF_FIRSTMAP 32u
#define WK_SC_OFF_BASE     24u
#define WK_STACKSHOT_WAITOWNER_FIRST_SPECIAL (UINT64_MAX - 7u)
#endif
