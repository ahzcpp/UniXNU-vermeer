/* Host semantic test of wk463_parse.h (469) with buffers built from the REAL xnu-10063 struct definitions and the kernel's
 * own encoding rules (get_pcblist_n ROUNDUP64 records; kcdata_get_memory_addr_with_flavor 16-byte padding with the
 * padded size stored in the header; arrays as KCDATA_TYPE_ARRAY_PAD0|pad with flags = el_type<<32 | count; container
 * markers carrying the container type in BEGIN and END).
 * Synthetic data only; proves parser logic/ABI agreement, not device behaviour. Run under ASan/UBSan. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "real_structs.inc"
#include "../src/wk463_parse.h"

static uint8_t B[1 << 20]; static size_t BL;
static void put(const void *p, size_t n) { memcpy(B + BL, p, n); BL += n; }
static void pad8(void) { while (BL % 8) B[BL++] = 0; }
static void rec(uint32_t kind, void *s, size_t sz) { memcpy(s, &(uint32_t){(uint32_t)sz}, 4); memcpy((uint8_t *)s + 4, &kind, 4); put(s, sz); pad8(); }

static int fails;
#define CHECK(e) do { if (!(e)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #e); fails++; } } while (0)

struct seen { int n; struct wk_pcb p[8]; };
static void cb(const struct wk_pcb *p, void *c) { struct seen *s = c; if (s->n < 8) s->p[s->n++] = *p; }

static void pcb_group(int k, int with_tcpcb, size_t socket_len)
{
	struct xinpcb_n xi = {0}; xi.inp_fport = k ? 0x5000 /* 80 */ : 0xbb01 /* 443 */; rec(WK_XSO_INPCB, &xi, sizeof xi);
	struct xsocket_n xs = {0}; xs.xso_so = 0x1111u + (unsigned)k; xs.so_last_pid = 370 + k; xs.so_e_pid = 900; xs.so_error = (u_short)(k ? 54 : 0);
	rec(WK_XSO_SOCKET, &xs, socket_len);
	struct xsockbuf_n rb = {0}; rb.sb_cc = 1000u + (unsigned)k; rec(WK_XSO_RCVBUF, &rb, sizeof rb);
	struct xsockbuf_n sb = {0}; sb.sb_cc = 7; rec(WK_XSO_SNDBUF, &sb, sizeof sb);
	struct xsockstat_n st = {0}; st.xst_tc_stats[0].rxbytes = 100; st.xst_tc_stats[2].rxbytes = 23; st.xst_tc_stats[1].txbytes = 50;
	st.xst_tc_stats[0].rxpackets = 3; rec(WK_XSO_STATS, &st, sizeof st);
	if (with_tcpcb) { struct xtcpcb_n xt = {0}; xt.t_state = k ? 5 : 4; rec(WK_XSO_TCPCB, &xt, sizeof xt); }
}
static void test_pcblist(void)
{
	BL = 0;
	struct xinpgen g = {0}; g.xig_len = sizeof g; g.xig_count = 2; put(&g, sizeof g); pad8();
	for (int k = 0; k < 2; k++) pcb_group(k, 1, sizeof(struct xsocket_n));
	put(&g, sizeof g);
	struct seen s = {0}; struct wk_pcb_stats st;
	int n = wk_parse_pcblist(B, BL, 1, cb, &s, &st);
	CHECK(n == 2 && st.errors == 0 && s.n == 2 && st.groups == 2 && st.incomplete == 0 && st.short_records == 0 && st.trailer_seen == 1);
	CHECK(s.p[0].fport == 443 && s.p[1].fport == 80);
	CHECK(s.p[0].last_pid == 370 && s.p[1].last_pid == 371 && s.p[0].e_pid == 900);
	CHECK(s.p[0].rxbytes == 123 && s.p[0].txbytes == 50 && s.p[0].rxpackets == 3);
	CHECK(s.p[0].rcv_cc == 1000 && s.p[1].rcv_cc == 1001 && s.p[0].snd_cc == 7);
	CHECK(s.p[0].t_state == 4 && s.p[1].t_state == 5 && s.p[1].so_error == 54);
	CHECK(s.p[0].handle == 0x1111 && s.p[1].handle == 0x1112);
	/* truncation at every length must never crash, never over-deliver, and never claim a trailer */
	for (size_t cut = 0; cut < BL; cut++) {
		struct seen t = {0}; struct wk_pcb_stats e; int m = wk_parse_pcblist(B, cut, 1, cb, &t, &e);
		CHECK(m <= 2 && e.trailer_seen == 0);
	}
	/* corrupt record length */
	uint8_t save[4]; memcpy(save, B + WK_ROUNDUP64(sizeof g), 4); memcpy(B + WK_ROUNDUP64(sizeof g), &(uint32_t){0xfffffff0u}, 4);
	{ struct seen t = {0}; struct wk_pcb_stats e; int m = wk_parse_pcblist(B, BL, 1, cb, &t, &e); CHECK(m == 0 && e.errors == 1 && !e.trailer_seen); }
	memcpy(B + WK_ROUNDUP64(sizeof g), save, 4);
	/* trailer whose xig_count equals a record kind (1 = SOCKET, 16 = INPCB) is still the trailer at the exact end */
	for (unsigned cnt = 0; cnt < 2; cnt++) {
		struct xinpgen t2 = g; t2.xig_count = cnt ? WK_XSO_INPCB : WK_XSO_SOCKET;
		memcpy(B + BL - sizeof g, &t2, sizeof t2);
		struct seen t = {0}; struct wk_pcb_stats e; int m = wk_parse_pcblist(B, BL, 1, cb, &t, &e);
		CHECK(m == 2 && e.trailer_seen == 1 && e.errors == 0 && e.incomplete == 0);
	}
	memcpy(B + BL - sizeof g, &g, sizeof g);
	/* short SOCKET record (S2): fields unknown -> group NOT delivered, counted short + incomplete */
	BL = 0; put(&g, sizeof g); pad8();
	pcb_group(0, 1, 40); pcb_group(1, 1, sizeof(struct xsocket_n)); put(&g, sizeof g);
	{ struct seen t = {0}; struct wk_pcb_stats e; int m = wk_parse_pcblist(B, BL, 1, cb, &t, &e);
	  CHECK(m == 1 && t.n == 1 && t.p[0].last_pid == 371 && e.short_records == 1 && e.incomplete == 1 && e.trailer_seen == 1 && e.errors == 0); }
	/* missing TCPCB for TCP: delivered (socket identity is known) but counted incomplete; for UDP not required */
	BL = 0; put(&g, sizeof g); pad8();
	pcb_group(0, 0, sizeof(struct xsocket_n)); put(&g, sizeof g);
	{ struct seen t = {0}; struct wk_pcb_stats e; int m = wk_parse_pcblist(B, BL, 1, cb, &t, &e);
	  CHECK(m == 1 && t.p[0].t_state == -1 && e.incomplete == 1 && e.short_records == 0); }
	{ struct seen t = {0}; struct wk_pcb_stats e; int m = wk_parse_pcblist(B, BL, 0, cb, &t, &e);
	  CHECK(m == 1 && e.incomplete == 0); }
	printf("pcblist tests done\n");
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
/* kern_cdata.c kcdata_add_container_marker: END carries no payload (data_size 0), identifier in flags */
static void kc_end(uint32_t ctype, uint64_t id) { (void)ctype; kc_item(WK_KC_CONTAINER_END, NULL, 0, id); }

static struct wk_ss SS;
static void add_thread(uint64_t tid, uint64_t flags, int nuser, uint64_t first_lr)
{
	kc_begin(WK_KC_CONT_THREAD, tid);
	struct thread_snapshot_v4 th = {0}; th.ths_thread_id = tid; th.ths_ss_flags = flags; th.ths_state = 0x2; th.ths_sched_priority = 47;
	th.ths_user_time = 240000; th.ths_sys_time = 24000;
	kc_item(WK_KC_THREAD_SNAPSHOT, &th, sizeof th, 0);
	if (nuser) {
		uint64_t lr[6]; for (int i = 0; i < 6; i++) lr[i] = first_lr + 0x10u * (unsigned)i;
		kc_array(WK_KC_USER_STACKLR64, lr, 8, (uint32_t)nuser);
		uint64_t klr[3] = {0xfffffff007100000ull, 0xfffffff007200000ull, 0xfffffff007300000ull};
		kc_array(WK_KC_KERN_STACKLR64, klr, 8, 3);
	}
	kc_end(WK_KC_CONT_THREAD, tid);
}
static void add_task(int pid, uint64_t main_tid, uint64_t other_tid, int with_waits, uint64_t server_tid, int server_pid, int nimg)
{
	kc_begin(WK_KC_CONT_TASK, (uint64_t)pid);
	struct task_snapshot_v2 ts; memset(&ts, 0, sizeof ts); ts.ts_pid = pid; kc_item(WK_KC_TASK_SNAPSHOT, &ts, sizeof ts, 0);
	struct dyld_uuid_info_64 img[12]; memset(img, 0, sizeof img);
	for (int i = 0; i < nimg; i++) { img[i].imageLoadAddress = 0x100000000ull + 0x800000ull * (unsigned)i; img[i].imageUUID[0] = (uint8_t)(0xa0 + i); }
	kc_array(WK_KC_LIBRARY_LOADINFO64, img, sizeof img[0], (uint32_t)nimg);
	add_thread(main_tid, WK_TH_FLAG_MAIN, 5, 0x180001000ull);   /* shared cache frames */
	add_thread(other_tid, 0, 2, 0x100004000ull);                /* main image frames */
	if (with_waits) {
		/* main: workloop sync wait owned by the worker thread (owner = thread id, kern_event.c 2607..2621) and a
		 * thread-kind turnstile ending at the server thread in another task; worker: port send to server pid */
		struct stackshot_thread_waitinfo_v2 wi[2]; memset(wi, 0, sizeof wi);
		wi[0].waiter = main_tid; wi[0].owner = other_tid; wi[0].wait_type = 0x10;
		wi[1].waiter = other_tid; wi[1].owner = (uint64_t)server_pid; wi[1].wait_type = 0x04; wi[1].wait_flags = 1;
		kc_array(WK_KC_THREAD_WAITINFO, wi, sizeof wi[0], 2);
		struct stackshot_thread_turnstileinfo_v2 tsi; memset(&tsi, 0, sizeof tsi);
		tsi.waiter = main_tid; tsi.turnstile_context = server_tid; tsi.number_of_hops = 2; tsi.turnstile_flags = WK_TSI_STATUS_THREAD;
		kc_array(WK_KC_THREAD_TURNSTILE, &tsi, sizeof tsi, 1);
	}
	kc_end(WK_KC_CONT_TASK, (uint64_t)pid);
}
static void build_good(void)
{
	BL = 0;
	kc_item(WK_KCDATA_BEGIN_STACKSHOT, NULL, 0, 0);
	struct dyld_shared_cache_loadinfo sc; memset(&sc, 0, sizeof sc); sc.sharedCacheSlidFirstMapping = 0x180000000ull; sc.sharedCacheUUID[0] = 0x5c;
	kc_item(WK_KC_SHAREDCACHE_LOADINFO, &sc, sizeof sc, 0);
	add_task(362, 1001, 1002, 1, 2001, 368, 2);     /* MobileSafari: main waits on its own worker 1002, inheritor 2001 */
	add_task(368, 2001, 2002, 0, 0, 0, 10);         /* WebContent with 10 images (8 kept) */
	add_task(500, 5001, 5002, 0, 0, 0, 1);          /* unrelated task: compact records only, no frames kept */
	kc_item(WK_KCDATA_BUFFER_END, NULL, 0, 0);
}
static void test_kcdata(void)
{
	build_good();
	static const int keep[2] = {362, 368};
	int rc = wk_parse_stackshot(B, BL, &SS, keep, 2);
	CHECK(rc == 0 && SS.errors == 0 && SS.n_tasks == 3 && SS.n_threads == 6 && SS.n_frames == 4 && !SS.frames_truncated);
	CHECK(SS.has_sc && SS.sc_base == 0x180000000ull && SS.sc_uuid[0] == 0x5c);
	CHECK(SS.task[0].pid == 362 && SS.task[1].pid == 368 && SS.task[0].n_images == 2 && SS.task[0].img_uuid[0][0] == 0xa0);
	CHECK(SS.task[0].n_images_total == 2 && SS.task[1].n_images == WK_MAX_IMAGES && SS.task[1].n_images_total == 10 &&
	      SS.task[1].img_uuid[7][0] == 0xa7);
	CHECK(SS.thread[4].pid == 500 && SS.thread[4].frames == -1 && SS.thread[4].tid == 5001);
	struct wk_thread *m = &SS.thread[0];
	CHECK(m->tid == 1001 && (m->ss_flags & WK_TH_FLAG_MAIN) && m->pid == 362 && m->sched_prio == 47 && m->state == 2);
	CHECK(m->frames == 0);
	const struct wk_frames *fr = &SS.frame[m->frames];
	CHECK(fr->n_user == 5 && fr->user[0] == 0x180001000ull && fr->n_kern == 3 && fr->kern[2] == 0xfffffff007300000ull);
	CHECK(m->has_wait && m->wait_type == 0x10 && m->wait_owner == 1002 && !(m->wait_flags & WK_WI_FLAG_SPECIALREPLY));
	CHECK(m->has_ts && m->ts_context == 2001 && m->ts_hops == 2 && (m->ts_flags & WK_TSI_STATUS_THREAD));
	CHECK(wk_pid_of_tid(&SS, m->ts_context) == 368);
	CHECK(SS.thread[1].has_wait && SS.thread[1].wait_type == 0x04 && SS.thread[1].wait_owner == 368 && !SS.thread[1].has_ts &&
	      (SS.thread[1].wait_flags & WK_WI_FLAG_SPECIALREPLY));
	CHECK(!SS.thread[2].has_wait && SS.thread[2].pid == 368);
	/* 469 links: direct owner thread 1002 = Safari ti 1; final inheritor 2001 = WebContent ti 0 */
	int pid, ti;
	CHECK(wk_locate_tid(&SS, 1002, &pid, &ti) == 1 && pid == 362 && ti == 1);
	CHECK(wk_locate_tid(&SS, 2001, &pid, &ti) == 2 && pid == 368 && ti == 0);
	CHECK(wk_locate_tid(&SS, 5002, &pid, &ti) == 5 && pid == 500 && ti == 1);
	CHECK(wk_locate_tid(&SS, 777, &pid, &ti) == -1 && pid == -1 && ti == -1);
	CHECK(wk_locate_tid(&SS, 0, &pid, &ti) == -1);
	CHECK(wk_owner_kind(0x10, 1002) == WK_OWN_TID && !strcmp(wk_owner_kind_name(0x10, 1002), "tid"));
	CHECK(wk_owner_kind(0x02, 2563) == WK_OWN_PORTNAME && !strcmp(wk_owner_kind_name(0x02, 2563), "portname"));
	CHECK(wk_owner_kind(0x04, 368) == WK_OWN_PID && wk_owner_kind(0x04, 0xfffffff012345678ull) == WK_OWN_OPAQUE);
	CHECK(!strcmp(wk_owner_kind_name(0x10, UINT64_MAX - 6), "threq") && !strcmp(wk_owner_kind_name(0x10, UINT64_MAX - 7), "suspended"));
	CHECK(wk_owner_kind(0x03, 0) == WK_OWN_NONE && wk_owner_kind(0x0a, 55) == WK_OWN_TID && wk_owner_kind(0x06, 99) == WK_OWN_PID);
	CHECK(!strcmp(wk_ts_kind_name(m), "thread") && !strcmp(wk_ts_kind_name(&SS.thread[1]), "none"));
	/* robustness: every truncation and random corruption is rejected without out-of-bounds access */
	for (size_t cut = 0; cut < BL; cut += 1) { int r = wk_parse_stackshot(B, cut, &SS, keep, 2); CHECK(r == -1); }
	srand(469);
	static uint8_t C[1 << 16]; size_t GL = BL; assert(GL <= sizeof C);
	for (int i = 0; i < 6000; i++) {
		memcpy(C, B, GL);
		for (int j = 0; j < 4; j++) C[(size_t)rand() % GL] = (uint8_t)rand();
		(void)wk_parse_stackshot(C, GL, &SS, keep, 2);
	}
	printf("kcdata tests done bytes=%zu\n", GL);
}
/* S3 malformed nesting: each must end with rc -1 / errors and never index task[-1] (ASan/UBSan watch) */
static int parse_now(void) { static const int keep[1] = {362}; return wk_parse_stackshot(B, BL, &SS, keep, 1); }
static void begin_buf(void) { BL = 0; kc_item(WK_KCDATA_BEGIN_STACKSHOT, NULL, 0, 0); }
static void end_buf(void) { kc_item(WK_KCDATA_BUFFER_END, NULL, 0, 0); }
static void test_nesting(void)
{
	/* thread container at top level, with waitinfo/turnstile/frames inside */
	begin_buf(); add_thread(9, WK_TH_FLAG_MAIN, 3, 0x180000000ull);
	{ struct stackshot_thread_waitinfo_v2 wi; memset(&wi, 0, sizeof wi); wi.waiter = 9; wi.wait_type = 2; kc_array(WK_KC_THREAD_WAITINFO, &wi, sizeof wi, 1); }
	end_buf(); CHECK(parse_now() == -1 && SS.errors > 0 && SS.n_threads == 0);
	/* task inside task */
	begin_buf(); kc_begin(WK_KC_CONT_TASK, 1); add_task(362, 1, 2, 1, 3, 4, 1); kc_end(WK_KC_CONT_TASK, 1); end_buf();
	CHECK(parse_now() == -1);
	/* thread inside thread */
	begin_buf(); kc_begin(WK_KC_CONT_TASK, 1); kc_begin(WK_KC_CONT_THREAD, 1); add_thread(2, 0, 1, 0x1000); kc_end(WK_KC_CONT_THREAD, 1);
	kc_end(WK_KC_CONT_TASK, 1); end_buf(); CHECK(parse_now() == -1);
	/* END identifier mismatch: the task's END (id 362) arrives while thread 9 is still open */
	begin_buf(); kc_begin(WK_KC_CONT_TASK, 362); kc_begin(WK_KC_CONT_THREAD, 9); kc_end(WK_KC_CONT_TASK, 362); kc_end(WK_KC_CONT_TASK, 362);
	end_buf(); CHECK(parse_now() == -1);
	/* terminated task: TRANSITIONING_TASK container holding a thread is well formed (kern_stackshot.c) */
	begin_buf(); kc_begin(WK_KC_CONT_TRANSITIONING_TASK, 77); add_thread(78, WK_TH_FLAG_MAIN, 1, 0x1000); kc_end(WK_KC_CONT_TRANSITIONING_TASK, 77);
	add_task(362, 11, 12, 0, 0, 0, 1); end_buf();
	CHECK(parse_now() == 0 && SS.n_tasks == 2 && SS.task[0].pid == -1 && SS.task[0].n_threads == 1 && SS.task[1].pid == 362 && SS.n_threads == 3);
	/* a thread directly inside a non-task container (e.g. a port-label container) is rejected */
	begin_buf(); kc_begin(0x934u, 1); add_thread(5, 0, 1, 0x1000); kc_end(0x934u, 1); end_buf(); CHECK(parse_now() == -1);
	/* other containers (shared cache 0x942) nest and close freely at top level */
	begin_buf(); kc_begin(0x942u, 3); kc_end(0x942u, 3); add_task(362, 11, 12, 0, 0, 0, 1); end_buf(); CHECK(parse_now() == 0 && SS.n_tasks == 1);
	/* END with no open container */
	begin_buf(); kc_end(WK_KC_CONT_TASK, 1); end_buf(); CHECK(parse_now() == -1);
	/* unclosed task before BUFFER_END: data kept for inspection, but the snapshot is malformed -> rc -1 */
	begin_buf(); kc_begin(WK_KC_CONT_TASK, 362);
	{ struct task_snapshot_v2 ts; memset(&ts, 0, sizeof ts); ts.ts_pid = 362; kc_item(WK_KC_TASK_SNAPSHOT, &ts, sizeof ts, 0); }
	end_buf(); CHECK(parse_now() == -1 && SS.n_tasks == 1 && SS.task[0].pid == 362 && SS.errors == 1);
	/* waitinfo for a thread of ANOTHER task is not applied */
	begin_buf(); add_task(362, 11, 12, 0, 0, 0, 1);
	kc_begin(WK_KC_CONT_TASK, 363);
	{ struct task_snapshot_v2 ts; memset(&ts, 0, sizeof ts); ts.ts_pid = 363; kc_item(WK_KC_TASK_SNAPSHOT, &ts, sizeof ts, 0); }
	{ struct stackshot_thread_waitinfo_v2 wi; memset(&wi, 0, sizeof wi); wi.waiter = 11; wi.owner = 12; wi.wait_type = 0x10; kc_array(WK_KC_THREAD_WAITINFO, &wi, sizeof wi, 1); }
	kc_end(WK_KC_CONT_TASK, 363); end_buf();
	CHECK(parse_now() == 0 && !SS.thread[0].has_wait);
	/* capacity: more tasks than WK_MAX_TASKS -> truncated, still validated, no error */
	begin_buf();
	for (int i = 0; i < WK_MAX_TASKS + 6; i++) {
		kc_begin(WK_KC_CONT_TASK, (uint64_t)i);
		struct task_snapshot_v2 ts; memset(&ts, 0, sizeof ts); ts.ts_pid = 1000 + i; kc_item(WK_KC_TASK_SNAPSHOT, &ts, sizeof ts, 0);
		add_thread(100000u + (unsigned)i, WK_TH_FLAG_MAIN, 0, 0);
		kc_end(WK_KC_CONT_TASK, (uint64_t)i);
	}
	end_buf();
	CHECK(BL < sizeof B && parse_now() == 0 && SS.truncated == 1 && SS.n_tasks == WK_MAX_TASKS && SS.n_threads == WK_MAX_TASKS && SS.errors == 0);
	/* a task opened inside a capacity-skipped task is still rejected */
	BL -= 16;   /* drop BUFFER_END (16-byte header, no data) */
	kc_begin(WK_KC_CONT_TASK, 1); kc_begin(WK_KC_CONT_TASK, 2); kc_end(WK_KC_CONT_TASK, 2); kc_end(WK_KC_CONT_TASK, 1); end_buf();
	CHECK(parse_now() == -1);
	printf("nesting tests done\n");
}
int main(void)
{
	test_pcblist();
	test_kcdata();
	test_nesting();
	printf("%s fails=%d\n", fails ? "FAIL" : "PASS", fails);
	return fails != 0;
}
