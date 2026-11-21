/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * wk463 pure parsers (no syscalls): pcblist_n records and stackshot kcdata. Host-testable.
 * All reads are bounds-checked memcpy; malformed input stops parsing with an error count, never reads past the buffer. */
#ifndef WK463_PARSE_H
#define WK463_PARSE_H
#include "wk463_abi.h"
#include <string.h>

static inline uint64_t wk_u64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline uint32_t wk_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t wk_u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline int16_t  wk_i16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }

/* ---------------- pcblist_n ---------------- */
struct wk_pcb {
	uint64_t handle;           /* xso_so (permuted); identity inside this process only, never printed */
	int32_t last_pid, e_pid;
	uint16_t fport;            /* host order */
	uint16_t so_error;
	int32_t t_state;           /* -1 when no TCPCB record (UDP) */
	uint32_t rcv_cc, snd_cc;
	uint64_t rxbytes, txbytes, rxpackets, txpackets;
	uint32_t have;             /* bitmask of XSO kinds seen */
};
typedef void (*wk_pcb_cb)(const struct wk_pcb *pcb, void *ctx);

/* Returns number of PCB groups delivered; *errors counts malformed records (parsing stops at the first). */
static inline int wk_parse_pcblist(const uint8_t *buf, size_t len, wk_pcb_cb cb, void *ctx, int *errors)
{
	int groups = 0; *errors = 0;
	if (len < WK_XINPGEN_SIZE) { (*errors)++; return 0; }
	uint32_t xig_len = wk_u32(buf);
	if (xig_len < WK_XINPGEN_SIZE || xig_len > len) { (*errors)++; return 0; }
	size_t off = WK_ROUNDUP64(xig_len);
	struct wk_pcb cur; int open = 0;
	memset(&cur, 0, sizeof cur); cur.t_state = -1;
	while (off + 8 <= len) {
		uint32_t rlen = wk_u32(buf + off), kind = wk_u32(buf + off + 4);
		if (rlen == WK_XINPGEN_SIZE && kind != WK_XSO_INPCB && kind != WK_XSO_SOCKET) break;   /* trailing xinpgen */
		if (rlen < 8 || off + rlen > len) { (*errors)++; break; }
		const uint8_t *r = buf + off;
		switch (kind) {
		case WK_XSO_INPCB:
			if (open && (cur.have & WK_XSO_SOCKET)) { cb(&cur, ctx); groups++; }
			memset(&cur, 0, sizeof cur); cur.t_state = -1; open = 1;
			if (rlen >= WK_XI_MIN_LEN) { uint16_t p = wk_u16(r + WK_XI_OFF_FPORT); cur.fport = (uint16_t)((p >> 8) | (p << 8)); }
			break;
		case WK_XSO_SOCKET:
			if (rlen >= WK_XSO_MIN_LEN) {
				cur.handle = wk_u64(r + WK_XSO_OFF_SO); cur.so_error = wk_u16(r + WK_XSO_OFF_ERROR);
				cur.last_pid = (int32_t)wk_u32(r + WK_XSO_OFF_LASTPID); cur.e_pid = (int32_t)wk_u32(r + WK_XSO_OFF_EPID);
			}
			break;
		case WK_XSO_RCVBUF: if (rlen >= WK_XSB_MIN_LEN) cur.rcv_cc = wk_u32(r + WK_XSB_OFF_CC); break;
		case WK_XSO_SNDBUF: if (rlen >= WK_XSB_MIN_LEN) cur.snd_cc = wk_u32(r + WK_XSB_OFF_CC); break;
		case WK_XSO_STATS:
			if (rlen >= WK_XST_MIN_LEN)
				for (unsigned t = 0; t < WK_XST_TC_MAX; t++) {
					const uint8_t *s = r + WK_XST_OFF_STATS + t * 32u;
					cur.rxpackets += wk_u64(s); cur.rxbytes += wk_u64(s + 8);
					cur.txpackets += wk_u64(s + 16); cur.txbytes += wk_u64(s + 24);
				}
			break;
		case WK_XSO_TCPCB: if (rlen >= WK_XT_MIN_LEN) cur.t_state = (int32_t)wk_u32(r + WK_XT_OFF_STATE); break;
		default: break;
		}
		cur.have |= kind & 0x3fu;
		off += WK_ROUNDUP64(rlen);
	}
	if (open && (cur.have & WK_XSO_SOCKET)) { cb(&cur, ctx); groups++; }
	return groups;
}

/* ---------------- stackshot kcdata ---------------- */
#define WK_MAX_TASKS 1024
#define WK_MAX_THREADS 8192
#define WK_MAX_FRAMES 32
#define WK_MAX_FRAMED 384          /* threads whose frames are kept (only tasks in the caller's keep list) */
#define WK_MAX_IMAGES 8
#define WK_MAX_KEEP 16
struct wk_frames { unsigned n_user, n_kern; uint64_t user[WK_MAX_FRAMES], kern[WK_MAX_FRAMES]; };
struct wk_thread {
	uint64_t tid, user_time, sys_time, ss_flags;
	uint32_t state; int16_t sched_prio;
	int pid, task_index, frames;   /* frames: index into wk_ss.frame or -1 */
	uint8_t has_wait, wait_type; uint64_t wait_owner; uint32_t wait_flags;
	uint8_t has_ts; uint8_t ts_hops; uint64_t ts_context, ts_flags;
};
struct wk_task {
	int pid; int first_thread, n_threads;
	unsigned n_images; uint64_t img_addr[WK_MAX_IMAGES]; uint8_t img_uuid[WK_MAX_IMAGES][16];
	int has_sc; uint64_t sc_base; uint8_t sc_uuid[16];
};
struct wk_ss {
	int n_tasks, n_threads, n_frames, errors, truncated, frames_truncated;
	int has_sc; uint64_t sc_base; uint8_t sc_uuid[16];   /* global shared cache (top-level item) */
	struct wk_task task[WK_MAX_TASKS];
	struct wk_thread thread[WK_MAX_THREADS];
	struct wk_frames frame[WK_MAX_FRAMED];
};
static inline int wk_keep(const int *keep, int nkeep, int pid)
{
	for (int i = 0; i < nkeep; i++) if (keep[i] == pid) return 1;
	return 0;
}

static inline struct wk_thread *wk_find_thread(struct wk_ss *ss, int task_index, uint64_t tid)
{
	const struct wk_task *t = &ss->task[task_index];
	for (int i = t->first_thread; i < t->first_thread + t->n_threads; i++) if (ss->thread[i].tid == tid) return &ss->thread[i];
	return NULL;
}
static inline int wk_pid_of_tid(const struct wk_ss *ss, uint64_t tid)
{
	for (int i = 0; i < ss->n_threads; i++) if (ss->thread[i].tid == tid) return ss->thread[i].pid;
	return -1;
}

/* Parse one stackshot kcdata buffer. Returns 0 on a well-formed buffer, -1 otherwise (partial results kept). */
static inline int wk_parse_stackshot(const uint8_t *buf, size_t len, struct wk_ss *ss, const int *keep, int nkeep)
{
	ss->n_tasks = ss->n_threads = ss->n_frames = ss->errors = ss->truncated = ss->frames_truncated = 0;
	ss->has_sc = 0; ss->sc_base = 0; memset(ss->sc_uuid, 0, sizeof ss->sc_uuid);
	if (len < WK_KC_ITEM_HDR || wk_u32(buf) != WK_KCDATA_BEGIN_STACKSHOT) { ss->errors++; return -1; }
	size_t off = WK_KC_ITEM_HDR + WK_ROUNDUP64(wk_u32(buf + 4)) ; /* begin item data (usually 0) */
	off = (off + 15u) & ~(size_t)15u;
	int task = -1, thread = -1, depth = 0;
	int cont_type[16]; memset(cont_type, 0, sizeof cont_type);
	/* waitinfo/turnstile arrays come at task level after the threads: buffer them until task end */
	while (off + WK_KC_ITEM_HDR <= len) {
		uint32_t type = wk_u32(buf + off), size = wk_u32(buf + off + 4);
		uint64_t flags = wk_u64(buf + off + 8);
		const uint8_t *d = buf + off + WK_KC_ITEM_HDR;
		if (type == WK_KCDATA_BUFFER_END) return ss->errors ? -1 : 0;
		if ((size_t)size > len - off - WK_KC_ITEM_HDR) { ss->errors++; return -1; }
		uint32_t base_type = type, count = 0, el_type = 0, el_size = 0;
		if ((type & ~0xfu) == WK_KC_ARRAY_PAD0) {
			el_type = (uint32_t)(flags >> 32); count = (uint32_t)flags;
			uint32_t pad = type & 0xfu;
			if (count && size >= pad) el_size = (size - pad) / count;
			base_type = el_type;
		}
		switch (base_type) {
		case WK_KC_CONTAINER_BEGIN: {
			uint32_t ct = size >= 4 ? wk_u32(d) : 0;
			if (depth < 15) cont_type[++depth] = (int)ct; else { ss->errors++; return -1; }
			if (ct == WK_KC_CONT_TASK) {
				if (ss->n_tasks >= WK_MAX_TASKS) { ss->truncated = 1; task = -1; break; }
				task = ss->n_tasks++; memset(&ss->task[task], 0, sizeof ss->task[task]);
				ss->task[task].pid = -1; ss->task[task].first_thread = ss->n_threads;
			} else if (ct == WK_KC_CONT_THREAD && task >= 0) {
				if (ss->n_threads >= WK_MAX_THREADS) { ss->truncated = 1; thread = -1; break; }
				thread = ss->n_threads++; memset(&ss->thread[thread], 0, sizeof ss->thread[thread]);
				ss->thread[thread].pid = ss->task[task].pid; ss->thread[thread].task_index = task;
				ss->thread[thread].frames = -1;
				if (wk_keep(keep, nkeep, ss->task[task].pid)) {
					if (ss->n_frames < WK_MAX_FRAMED) {
						ss->thread[thread].frames = ss->n_frames;
						memset(&ss->frame[ss->n_frames], 0, sizeof ss->frame[0]); ss->n_frames++;
					} else ss->frames_truncated = 1;
				}
				ss->task[task].n_threads++;
			}
			break;
		}
		case WK_KC_CONTAINER_END:
			if (depth <= 0) { ss->errors++; return -1; }
			if (cont_type[depth] == (int)WK_KC_CONT_THREAD) thread = -1;
			else if (cont_type[depth] == (int)WK_KC_CONT_TASK) task = -1;
			depth--;
			break;
		case WK_KC_TASK_SNAPSHOT:
			if (task >= 0 && size >= WK_TS_MIN_LEN) ss->task[task].pid = (int32_t)wk_u32(d + WK_TS_OFF_PID);
			break;
		case WK_KC_THREAD_SNAPSHOT:
			if (thread >= 0 && size >= WK_TH_MIN_LEN) {
				struct wk_thread *th = &ss->thread[thread];
				th->tid = wk_u64(d + WK_TH_OFF_ID); th->user_time = wk_u64(d + WK_TH_OFF_USER_TIME);
				th->sys_time = wk_u64(d + WK_TH_OFF_SYS_TIME); th->ss_flags = wk_u64(d + WK_TH_OFF_SS_FLAGS);
				th->state = wk_u32(d + WK_TH_OFF_STATE); th->sched_prio = wk_i16(d + WK_TH_OFF_SCHED_PRIO);
				th->pid = ss->task[task].pid;
			}
			break;
		case WK_KC_USER_STACKLR64:
		case WK_KC_KERN_STACKLR64:
			if (thread >= 0 && el_size == 8 && ss->thread[thread].frames >= 0) {
				struct wk_frames *fr = &ss->frame[ss->thread[thread].frames];
				unsigned *n = base_type == WK_KC_USER_STACKLR64 ? &fr->n_user : &fr->n_kern;
				uint64_t *dst = base_type == WK_KC_USER_STACKLR64 ? fr->user : fr->kern;
				for (uint32_t i = 0; i < count && *n < WK_MAX_FRAMES; i++) dst[(*n)++] = wk_u64(d + 8u * i);
			}
			break;
		case WK_KC_LIBRARY_LOADINFO64:
			if (task >= 0 && thread < 0 && el_size >= 24) {
				struct wk_task *t = &ss->task[task];
				for (uint32_t i = 0; i < count && t->n_images < WK_MAX_IMAGES; i++) {
					t->img_addr[t->n_images] = wk_u64(d + (size_t)el_size * i);
					memcpy(t->img_uuid[t->n_images], d + (size_t)el_size * i + 8, 16); t->n_images++;
				}
			}
			break;
		case WK_KC_SHAREDCACHE_LOADINFO:
			if (size >= 40) {
				uint64_t first = wk_u64(d + WK_SC_OFF_FIRSTMAP), base = wk_u64(d + WK_SC_OFF_BASE);
				uint64_t b = first ? first : base;
				if (task >= 0) { ss->task[task].has_sc = 1; ss->task[task].sc_base = b; memcpy(ss->task[task].sc_uuid, d + 8, 16); }
				else { ss->has_sc = 1; ss->sc_base = b; memcpy(ss->sc_uuid, d + 8, 16); }
			}
			break;
		case WK_KC_THREAD_WAITINFO:
			if (task >= 0 && el_size >= WK_WI_MIN_LEN)
				for (uint32_t i = 0; i < count; i++) {
					const uint8_t *w = d + (size_t)el_size * i;
					struct wk_thread *th = wk_find_thread(ss, task, wk_u64(w + WK_WI_OFF_WAITER));
					if (!th) continue;
					th->has_wait = 1; th->wait_owner = wk_u64(w + WK_WI_OFF_OWNER); th->wait_type = w[WK_WI_OFF_TYPE];
					th->wait_flags = el_size >= WK_WI_OFF_FLAGS + 4 ? wk_u32(w + WK_WI_OFF_FLAGS) : 0;
				}
			break;
		case WK_KC_THREAD_TURNSTILE:
			if (task >= 0 && el_size >= WK_TSI_MIN_LEN)
				for (uint32_t i = 0; i < count; i++) {
					const uint8_t *w = d + (size_t)el_size * i;
					struct wk_thread *th = wk_find_thread(ss, task, wk_u64(w + WK_TSI_OFF_WAITER));
					if (!th) continue;
					th->has_ts = 1; th->ts_context = wk_u64(w + WK_TSI_OFF_CONTEXT);
					th->ts_hops = w[WK_TSI_OFF_HOPS]; th->ts_flags = wk_u64(w + WK_TSI_OFF_FLAGS);
				}
			break;
		default: break;
		}
		off += WK_KC_ITEM_HDR + size;
		off = (off + 15u) & ~(size_t)15u;   /* KCDATA_ALIGNMENT_SIZE */
	}
	ss->errors++;   /* no BUFFER_END seen */
	return -1;
}
#endif
