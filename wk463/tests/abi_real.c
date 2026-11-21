/* Host ABI check: our wk463_abi.h offsets vs the real xnu-10063 struct text (tests/real_structs.inc). */
#include <stddef.h>
#include <stdio.h>
#include "real_structs.inc"
#include "../src/wk463_abi.h"
#define CHECK(expr) do { if (!(expr)) { printf("FAIL %s\n", #expr); fails++; } else printf("ok   %s\n", #expr); } while (0)
int main(void)
{
	int fails = 0;
	CHECK(sizeof(struct xinpgen) == WK_XINPGEN_SIZE);
	CHECK(offsetof(struct xsocket_n, xso_so) == WK_XSO_OFF_SO);
	CHECK(offsetof(struct xsocket_n, so_error) == WK_XSO_OFF_ERROR);
	CHECK(offsetof(struct xsocket_n, so_last_pid) == WK_XSO_OFF_LASTPID);
	CHECK(offsetof(struct xsocket_n, so_e_pid) == WK_XSO_OFF_EPID);
	CHECK(sizeof(struct xsocket_n) >= WK_XSO_MIN_LEN);
	CHECK(offsetof(struct xsockbuf_n, sb_cc) == WK_XSB_OFF_CC);
	CHECK(offsetof(struct xsockstat_n, xst_tc_stats) == WK_XST_OFF_STATS);
	CHECK(sizeof(struct data_stats) == 32 && sizeof(struct xsockstat_n) == WK_XST_MIN_LEN);
	CHECK(offsetof(struct xinpcb_n, inp_fport) == WK_XI_OFF_FPORT);
	CHECK(offsetof(struct xtcpcb_n, t_state) == WK_XT_OFF_STATE);
	CHECK(sizeof(struct kcdata_item) == WK_KC_ITEM_HDR);
	CHECK(offsetof(struct task_snapshot_v2, ts_pid) == WK_TS_OFF_PID);
	CHECK(offsetof(struct thread_snapshot_v4, ths_thread_id) == WK_TH_OFF_ID);
	CHECK(offsetof(struct thread_snapshot_v4, ths_user_time) == WK_TH_OFF_USER_TIME);
	CHECK(offsetof(struct thread_snapshot_v4, ths_sys_time) == WK_TH_OFF_SYS_TIME);
	CHECK(offsetof(struct thread_snapshot_v4, ths_ss_flags) == WK_TH_OFF_SS_FLAGS);
	CHECK(offsetof(struct thread_snapshot_v4, ths_state) == WK_TH_OFF_STATE);
	CHECK(offsetof(struct thread_snapshot_v4, ths_sched_priority) == WK_TH_OFF_SCHED_PRIO);
	CHECK(offsetof(struct stackshot_thread_waitinfo_v2, owner) == WK_WI_OFF_OWNER);
	CHECK(offsetof(struct stackshot_thread_waitinfo_v2, waiter) == WK_WI_OFF_WAITER);
	CHECK(offsetof(struct stackshot_thread_waitinfo_v2, wait_type) == WK_WI_OFF_TYPE);
	CHECK(offsetof(struct stackshot_thread_waitinfo_v2, wait_flags) == WK_WI_OFF_FLAGS);
	CHECK(offsetof(struct stackshot_thread_turnstileinfo_v2, waiter) == WK_TSI_OFF_WAITER);
	CHECK(offsetof(struct stackshot_thread_turnstileinfo_v2, turnstile_context) == WK_TSI_OFF_CONTEXT);
	CHECK(offsetof(struct stackshot_thread_turnstileinfo_v2, number_of_hops) == WK_TSI_OFF_HOPS);
	CHECK(offsetof(struct stackshot_thread_turnstileinfo_v2, turnstile_flags) == WK_TSI_OFF_FLAGS);
	CHECK(offsetof(struct dyld_shared_cache_loadinfo, sharedCacheUnreliableSlidBaseAddress) == WK_SC_OFF_BASE);
	CHECK(offsetof(struct dyld_shared_cache_loadinfo, sharedCacheSlidFirstMapping) == WK_SC_OFF_FIRSTMAP);
	CHECK(sizeof(stackshot_config_t) == sizeof(wk_stackshot_config_t));
	CHECK(offsetof(stackshot_config_t, sc_buffer) == offsetof(wk_stackshot_config_t, sc_buffer));
	CHECK(offsetof(stackshot_config_t, sc_size) == offsetof(wk_stackshot_config_t, sc_size));
	CHECK(offsetof(stackshot_config_t, sc_out_size_addr) == offsetof(wk_stackshot_config_t, sc_out_size_addr));
	printf("%s fails=%d\n", fails ? "FAIL" : "PASS", fails);
	return fails != 0;
}
