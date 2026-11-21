/* Host semantic test of wk463_parse.h with buffers built from the REAL xnu-10063 struct definitions and the kernel's
 * own encoding rules (get_pcblist_n ROUNDUP64 records; kcdata_get_memory_addr_with_flavor 16-byte padding with the
 * padded size stored in the header; arrays as KCDATA_TYPE_ARRAY_PAD0|pad with flags = el_type<<32 | count).
 * Synthetic data only; proves parser logic/ABI agreement, not device behaviour. Run under ASan/UBSan. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "real_structs.inc"
#include "../src/wk463_parse.h"

static uint8_t B[1 << 16]; static size_t BL;
static void put(const void *p, size_t n) { memcpy(B + BL, p, n); BL += n; }
static void pad8(void) { while (BL % 8) B[BL++] = 0; }
static void rec(uint32_t kind, void *s, size_t sz) { memcpy(s, &(uint32_t){(uint32_t)sz}, 4); memcpy((uint8_t *)s + 4, &kind, 4); put(s, sz); pad8(); }

static int fails;
#define CHECK(e) do { if (!(e)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #e); fails++; } } while (0)

struct seen { int n; struct wk_pcb p[8]; };
static void cb(const struct wk_pcb *p, void *c) { struct seen *s = c; if (s->n < 8) s->p[s->n++] = *p; }

static void test_pcblist(void)
{
	BL = 0;
	struct xinpgen g = {0}; g.xig_len = sizeof g; g.xig_count = 2; put(&g, sizeof g); pad8();
	for (int k = 0; k < 2; k++) {
		struct xinpcb_n xi = {0}; xi.inp_fport = k ? 0x5000 /* 80 */ : 0xbb01 /* 443 */; rec(WK_XSO_INPCB, &xi, sizeof xi);
		struct xsocket_n xs = {0}; xs.xso_so = 0x1111u + (unsigned)k; xs.so_last_pid = 370 + k; xs.so_e_pid = 900; xs.so_error = (u_short)(k ? 54 : 0);
		rec(WK_XSO_SOCKET, &xs, sizeof xs);
		struct xsockbuf_n rb = {0}; rb.sb_cc = 1000u + (unsigned)k; rec(WK_XSO_RCVBUF, &rb, sizeof rb);
		struct xsockbuf_n sb = {0}; sb.sb_cc = 7; rec(WK_XSO_SNDBUF, &sb, sizeof sb);
		struct xsockstat_n st = {0}; st.xst_tc_stats[0].rxbytes = 100; st.xst_tc_stats[2].rxbytes = 23; st.xst_tc_stats[1].txbytes = 50;
		st.xst_tc_stats[0].rxpackets = 3; rec(WK_XSO_STATS, &st, sizeof st);
		struct xtcpcb_n xt = {0}; xt.t_state = k ? 5 : 4; rec(WK_XSO_TCPCB, &xt, sizeof xt);
	}
	put(&g, sizeof g);
	struct seen s = {0}; int err = 0;
	int n = wk_parse_pcblist(B, BL, cb, &s, &err);
	CHECK(n == 2 && err == 0 && s.n == 2);
	CHECK(s.p[0].fport == 443 && s.p[1].fport == 80);
	CHECK(s.p[0].last_pid == 370 && s.p[1].last_pid == 371 && s.p[0].e_pid == 900);
	CHECK(s.p[0].rxbytes == 123 && s.p[0].txbytes == 50 && s.p[0].rxpackets == 3);
	CHECK(s.p[0].rcv_cc == 1000 && s.p[1].rcv_cc == 1001 && s.p[0].snd_cc == 7);
	CHECK(s.p[0].t_state == 4 && s.p[1].t_state == 5 && s.p[1].so_error == 54);
	CHECK(s.p[0].handle == 0x1111 && s.p[1].handle == 0x1112);
	/* truncation at every length must never crash and never over-deliver */
	for (size_t cut = 0; cut < BL; cut++) { struct seen t = {0}; int e = 0; int m = wk_parse_pcblist(B, cut, cb, &t, &e); CHECK(m <= 2); }
	/* corrupt record length */
	uint8_t save[4]; memcpy(save, B + WK_ROUNDUP64(sizeof g), 4); memcpy(B + WK_ROUNDUP64(sizeof g), &(uint32_t){0xfffffff0u}, 4);
	{ struct seen t = {0}; int e = 0; int m = wk_parse_pcblist(B, BL, cb, &t, &e); CHECK(m == 0 && e == 1); }
	memcpy(B + WK_ROUNDUP64(sizeof g), save, 4);
	printf("pcblist tests done bytes=%zu\n", BL);
}

/* kcdata writer following kern_cdata.c */
static void kc_item(uint32_t type, const void *data, uint32_t size, uint64_t flags)
{
	uint32_t padding = (16u - (size % 16u)) % 16u;
	struct kcdata_item it = {0}; it.type = type; it.size = size + padding; it.flags = flags;
	put(&it, sizeof it); if (size) put(data, size); memset(B + BL, 0, padding); BL += padding;
}
static void kc_array(uint32_t el_type, const void *data, uint32_t el_size, uint32_t count)
{
	uint32_t total = el_size * count, pad = (16u - (total % 16u)) % 16u;
	kc_item(WK_KC_ARRAY_PAD0 | pad, data, total, ((uint64_t)el_type << 32) | count);
}
static void kc_begin(uint32_t ctype, uint64_t id) { kc_item(WK_KC_CONTAINER_BEGIN, &ctype, 4, id); }
static void kc_end(uint32_t ctype, uint64_t id) { kc_item(WK_KC_CONTAINER_END, &ctype, 4, id); }

static struct wk_ss SS;
static void add_thread(uint64_t tid, uint64_t flags, int nuser, uint64_t first_lr)
{
	kc_begin(WK_KC_CONT_THREAD, tid);
	struct thread_snapshot_v4 th = {0}; th.ths_thread_id = tid; th.ths_ss_flags = flags; th.ths_state = 0x2; th.ths_sched_priority = 47;
	th.ths_user_time = 240000; th.ths_sys_time = 24000;
	kc_item(WK_KC_THREAD_SNAPSHOT, &th, sizeof th, 0);
	uint64_t lr[6]; for (int i = 0; i < 6; i++) lr[i] = first_lr + 0x10u * (unsigned)i;
	kc_array(WK_KC_USER_STACKLR64, lr, 8, (uint32_t)nuser);
	uint64_t klr[3] = {0xfffffff007100000ull, 0xfffffff007200000ull, 0xfffffff007300000ull};
	kc_array(WK_KC_KERN_STACKLR64, klr, 8, 3);
	kc_end(WK_KC_CONT_THREAD, tid);
}
static void add_task(int pid, uint64_t main_tid, uint64_t other_tid, int with_waits, uint64_t server_tid, int server_pid)
{
	kc_begin(WK_KC_CONT_TASK, (uint64_t)pid);
	struct task_snapshot_v2 ts; memset(&ts, 0, sizeof ts); ts.ts_pid = pid; kc_item(WK_KC_TASK_SNAPSHOT, &ts, sizeof ts, 0);
	struct dyld_uuid_info_64 img[2]; memset(img, 0, sizeof img); img[0].imageLoadAddress = 0x100000000ull; img[0].imageUUID[0] = 0xab;
	img[1].imageLoadAddress = 0x100800000ull; kc_array(WK_KC_LIBRARY_LOADINFO64, img, sizeof img[0], 2);
	add_thread(main_tid, WK_TH_FLAG_MAIN, 5, 0x180001000ull);   /* shared cache frames */
	add_thread(other_tid, 0, 2, 0x100004000ull);                /* main image frames */
	if (with_waits) {
		struct stackshot_thread_waitinfo_v2 wi[2]; memset(wi, 0, sizeof wi);
		wi[0].waiter = main_tid; wi[0].owner = (uint64_t)pid; wi[0].wait_type = 0x02; wi[0].wait_flags = 1; /* special reply recv */
		wi[1].waiter = other_tid; wi[1].owner = (uint64_t)server_pid; wi[1].wait_type = 0x04;
		kc_array(WK_KC_THREAD_WAITINFO, wi, sizeof wi[0], 2);
		struct stackshot_thread_turnstileinfo_v2 tsi; memset(&tsi, 0, sizeof tsi);
		tsi.waiter = main_tid; tsi.turnstile_context = server_tid; tsi.number_of_hops = 2; tsi.turnstile_flags = WK_TSI_STATUS_THREAD;
		kc_array(WK_KC_THREAD_TURNSTILE, &tsi, sizeof tsi, 1);
	}
	kc_end(WK_KC_CONT_TASK, (uint64_t)pid);
}
static void test_kcdata(void)
{
	BL = 0;
	kc_item(WK_KCDATA_BEGIN_STACKSHOT, NULL, 0, 0);
	struct dyld_shared_cache_loadinfo sc; memset(&sc, 0, sizeof sc); sc.sharedCacheSlidFirstMapping = 0x180000000ull; sc.sharedCacheUUID[0] = 0x5c;
	kc_item(WK_KC_SHAREDCACHE_LOADINFO, &sc, sizeof sc, 0);
	add_task(362, 1001, 1002, 1, 2001, 368);     /* MobileSafari blocked on sync IPC to WebContent thread 2001 */
	add_task(368, 2001, 2002, 0, 0, 0);          /* WebContent */
	add_task(500, 5001, 5002, 0, 0, 0);          /* unrelated task: compact records only, no frames kept */
	kc_item(WK_KCDATA_BUFFER_END, NULL, 0, 0);
	static const int keep[2] = {362, 368};
	int rc = wk_parse_stackshot(B, BL, &SS, keep, 2);
	CHECK(rc == 0 && SS.errors == 0 && SS.n_tasks == 3 && SS.n_threads == 6 && SS.n_frames == 4 && !SS.frames_truncated);
	CHECK(SS.has_sc && SS.sc_base == 0x180000000ull && SS.sc_uuid[0] == 0x5c);
	CHECK(SS.task[0].pid == 362 && SS.task[1].pid == 368 && SS.task[0].n_images == 2 && SS.task[0].img_uuid[0][0] == 0xab);
	CHECK(SS.thread[4].pid == 500 && SS.thread[4].frames == -1 && SS.thread[4].tid == 5001);
	struct wk_thread *m = &SS.thread[0];
	CHECK(m->tid == 1001 && (m->ss_flags & WK_TH_FLAG_MAIN) && m->pid == 362 && m->sched_prio == 47 && m->state == 2);
	CHECK(m->frames == 0);
	const struct wk_frames *fr = &SS.frame[m->frames];
	CHECK(fr->n_user == 5 && fr->user[0] == 0x180001000ull && fr->n_kern == 3 && fr->kern[2] == 0xfffffff007300000ull);
	CHECK(m->has_wait && m->wait_type == 0x02 && (m->wait_flags & WK_WI_FLAG_SPECIALREPLY) && m->wait_owner == 362);
	CHECK(m->has_ts && m->ts_context == 2001 && m->ts_hops == 2 && (m->ts_flags & WK_TSI_STATUS_THREAD));
	CHECK(wk_pid_of_tid(&SS, m->ts_context) == 368);
	CHECK(SS.thread[1].has_wait && SS.thread[1].wait_type == 0x04 && SS.thread[1].wait_owner == 368 && !SS.thread[1].has_ts);
	CHECK(!SS.thread[2].has_wait && SS.thread[2].pid == 368);
	/* robustness: every truncation and random corruption is rejected without out-of-bounds access */
	for (size_t cut = 0; cut < BL; cut += 1) { int r = wk_parse_stackshot(B, cut, &SS, keep, 2); CHECK(r == -1); }
	srand(463);
	for (int i = 0; i < 4000; i++) {
		static uint8_t C[1 << 16]; memcpy(C, B, BL);
		for (int j = 0; j < 4; j++) C[(size_t)rand() % BL] = (uint8_t)rand();
		(void)wk_parse_stackshot(C, BL, &SS, keep, 2);
	}
	printf("kcdata tests done bytes=%zu\n", BL);
}
int main(void)
{
	test_pcblist();
	test_kcdata();
	printf("%s fails=%d\n", fails ? "FAIL" : "PASS", fails);
	return fails != 0;
}
