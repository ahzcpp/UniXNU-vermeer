/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-wk469: finite, read-only WebKit navigation observer for the K70 20H392 port (root launchd job).
 * Successor of unixnu-wk463 (463.1 frozen; 463.2 review fixes folded in). Rows keep the "WK463 " prefix and every 463 row
 * keeps its fields, so existing readers continue to work; 469 only APPENDS fields and adds new row kinds.
 *   A. Per-socket transport progress for the role processes (1 Networking, 2 GPU, 3 WebContent, 4 MobileSafari,
 *      5 AppStore) from sysctl net.inet.tcp.pcblist_n / net.inet.udp.pcblist_n: TCP state, cumulative rx/tx bytes,
 *      unread rcvq / unsent sndq, so_error; printed only on new/changed/gone, with per-sample completeness accounting.
 *      Transport bytes prove only that the Networking process received/read data, not that a page rendered.
 *   B. At fixed samples, one system-wide kernel stackshot (stack_snapshot_with_config, root; THREAD_WAITINFO):
 *      - every role task: main thread (row ssth, now also tid/ti and resolved owner/turnstile thread) + bounded frames;
 *      - roles 2 GPU, 4 MobileSafari, 5 AppStore: ALL other threads, at most 24 per process (rows sswk/ssthw/ssfrw),
 *        each with its thread id, in-snapshot ordinal ti, wait type, the DIRECT wait owner (thread id + its pid/ti when the
 *        kernel reports a thread, e.g. the workloop owner from kern_event.c kdp_workloop_sync_wait_find_owner) and the
 *        turnstile FINAL inheritor (thread id + pid/ti when STACKSHOT_TURNSTILE_STATUS_THREAD); truncation is explicit.
 *      - roles 1, 3: as 463, up to 3 extra threads blocked in synchronous IPC.
 *      A self-deadlock can only be concluded offline from a closed chain of thread ids, never from "ts_pid == own pid".
 * Privacy/boundaries: no IP addresses, no local ports, no host/URL/thread names, no pointers (permuted kernel handles,
 * wait contexts and workqueue inheritors are used only internally or classified), no file paths except fixed role
 * basenames, no writes anywhere but stdout and the optional checked console mirror. No signals, no task ports, no network.
 * Thread ids are kernel sequence numbers (not addresses). Bounds: 16 samples x 2 s, stackshots at n = 2,5,8,11,14,
 * <= 4000 rows, rows < 512 bytes, worker dumps for <= 6 processes per stackshot, hard alarm 80 s.
 * Usage: unixnu-wk469 [-c]      (-c mirrors rows to /dev/console, nonblocking char device only)
 */
#include "wk463_parse.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

int proc_listallpids(void *buffer, int buffersize);
int proc_pidpath(int pid, void *buffer, uint32_t buffersize);
static mach_timebase_info_data_t g_tb;
static unsigned long long abs_to_ms(uint64_t t)
{
	if (!g_tb.denom) return 0;
	return (unsigned long long)((__uint128_t)t * g_tb.numer / g_tb.denom / 1000000u);
}

#define VERSION "469.0"
#define SAMPLES 16
#define INTERVAL_MS 2000
#define ALARM_S 80
#define MAX_ROWS 4000u
#define MAX_ROLE_PIDS 16
#define MAX_SOCKS 256
#define PCB_CAP (8u << 20)
#define WORKER_CAP 24          /* non-main threads reported per GPU/Safari/AppStore process */
#define WORKER_PROCS_MAX 6     /* processes with a worker dump per stackshot */
#define MAX_REFS 1024

static int g_console = -1;
static unsigned g_rows, g_rows_dropped, g_out_short, g_con_short;   /* 463.2: write accounting (review: partial writes) */
static void raw(const char *s, size_t n)
{
	if (g_rows >= MAX_ROWS || n == 0 || n >= 512) { g_rows_dropped++; return; }
	g_rows++;
	ssize_t r = write(STDOUT_FILENO, s, n); if (r != (ssize_t)n) g_out_short++;
	if (g_console >= 0) { r = write(g_console, s, n); if (r != (ssize_t)n) g_con_short++; }
}
static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *fmt, ...)
{
	char b[512]; int o = snprintf(b, sizeof b, "WK463 ");
	va_list ap; va_start(ap, fmt); int n = vsnprintf(b + o, sizeof b - (size_t)o - 1, fmt, ap); va_end(ap);
	if (o < 0 || n < 0) return;
	size_t len = (size_t)o + (size_t)n; if (len > sizeof b - 2) len = sizeof b - 2;
	b[len++] = '\n'; raw(b, len);
}
static void on_alarm(int s)
{
	(void)s; static const char m[] = "WK463 result=incomplete reason=alarm version=" VERSION "\n";
	ssize_t r = write(STDOUT_FILENO, m, sizeof m - 1); (void)r;
	if (g_console >= 0) { r = write(g_console, m, sizeof m - 1); (void)r; }
	_exit(124);
}
static long long mono_ms(void)
{
	struct timespec t; if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int console_open(void)
{
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return -1;
	struct stat st; if (fstat(fd, &st) || !S_ISCHR(st.st_mode)) { close(fd); return -1; }
	return fd;
}

/* ---------------- roles ---------------- */
struct role_pid { int pid, role; };
static struct role_pid g_roles[MAX_ROLE_PIDS]; static int g_nroles;
static int role_of_basename(const char *b)
{
	if (!strcmp(b, "com.apple.WebKit.Networking")) return 1;
	if (!strcmp(b, "com.apple.WebKit.GPU")) return 2;
	if (!strcmp(b, "com.apple.WebKit.WebContent")) return 3;
	if (!strcmp(b, "MobileSafari")) return 4;
	if (!strcmp(b, "AppStore")) return 5;          /* 469: /Applications/AppStore.app/AppStore */
	return 0;
}
static int role_of_pid(int pid)
{
	for (int i = 0; i < g_nroles; i++) if (g_roles[i].pid == pid) return g_roles[i].role;
	return 0;
}
static int role_has_workers(int role) { return role == 2 || role == 4 || role == 5; }
static int g_roles_ok;   /* 463.2: this sample's role discovery listed pids and was not truncated */
static void discover_roles(int n)
{
	static int pids[4096]; static struct role_pid prev[MAX_ROLE_PIDS]; static int nprev;
	int bytes = proc_listallpids(pids, (int)sizeof pids);
	int count = bytes > 0 ? bytes : 0;   /* libproc returns the number of pids */
	if (count > 4096) count = 4096;
	g_nroles = 0; int trunc = count >= 4096;
	g_roles_ok = 0;
	for (int i = 0; i < count; i++) {
		char path[1024]; if (pids[i] <= 0 || proc_pidpath(pids[i], path, sizeof path) <= 0) continue;
		const char *b = strrchr(path, '/'); b = b ? b + 1 : path;
		int r = role_of_basename(b); if (!r) continue;
		if (g_nroles >= MAX_ROLE_PIDS) { trunc = 1; break; }
		g_roles[g_nroles].pid = pids[i]; g_roles[g_nroles].role = r; g_nroles++;
	}
	g_roles_ok = count > 0 && !trunc;
	int changed = g_nroles != nprev;
	for (int i = 0; !changed && i < g_nroles; i++) if (g_roles[i].pid != prev[i].pid) changed = 1;
	if (changed || n == 0 || !g_roles_ok) {
		for (int i = 0; i < g_nroles; i++) emit("role n=%d pid=%d role=%d", n, g_roles[i].pid, g_roles[i].role);
		emit("roles n=%d count=%d listed=%d trunc=%d", n, g_nroles, count, trunc);
		memcpy(prev, g_roles, sizeof prev); nprev = g_nroles;
	}
}

/* ---------------- A: sockets ---------------- */
struct sock_state { uint64_t handle; int sid, pid, role, proto, seen, unseen_reported; int32_t st; uint32_t rcv, snd; uint64_t rx, tx; uint16_t err; };
static struct sock_state g_socks[MAX_SOCKS]; static int g_nsocks, g_next_sid;
struct pcb_ctx { int n, proto, matched; };
static int port_class(uint16_t p) { return p == 443 ? 443 : p == 80 ? 80 : p == 53 ? 53 : p == 0 ? 0 : 1; }
static void pcb_seen(const struct wk_pcb *p, void *vctx)
{
	struct pcb_ctx *c = vctx;
	int pid = role_of_pid(p->last_pid) ? p->last_pid : role_of_pid(p->e_pid) ? p->e_pid : 0;
	if (!pid) return;
	c->matched++;
	struct sock_state *s = NULL;
	for (int i = 0; i < g_nsocks; i++) if (g_socks[i].handle == p->handle && g_socks[i].proto == c->proto) { s = &g_socks[i]; break; }
	int fresh = 0;
	if (!s) {
		if (g_nsocks >= MAX_SOCKS) return;
		s = &g_socks[g_nsocks++]; memset(s, 0, sizeof *s);
		s->handle = p->handle; s->sid = ++g_next_sid; s->proto = c->proto; fresh = 1;
	}
	s->seen = c->n; s->unseen_reported = 0; s->pid = pid; s->role = role_of_pid(pid);
	if (fresh || s->st != p->t_state || s->rx != p->rxbytes || s->tx != p->txbytes || s->rcv != p->rcv_cc ||
	    s->snd != p->snd_cc || s->err != p->so_error)
		emit("sock n=%d role=%d pid=%d sid=%d proto=%s new=%d st=%d rport=%d rx=%llu tx=%llu rxp=%llu txp=%llu rcvq=%u sndq=%u err=%u",
		     c->n, s->role, pid, s->sid, c->proto == 6 ? "tcp" : "udp", fresh, (int)p->t_state, port_class(p->fport),
		     (unsigned long long)p->rxbytes, (unsigned long long)p->txbytes, (unsigned long long)p->rxpackets,
		     (unsigned long long)p->txpackets, p->rcv_cc, p->snd_cc, (unsigned)p->so_error);
	s->st = p->t_state; s->rx = p->rxbytes; s->tx = p->txbytes; s->rcv = p->rcv_cc; s->snd = p->snd_cc; s->err = p->so_error;
}
static void sample_sockets(int n)
{
	static const struct { const char *name; int proto; } lists[2] = {{"net.inet.tcp.pcblist_n", 6}, {"net.inet.udp.pcblist_n", 17}};
	int rc[2] = {0, 0}, matched = 0, cov_ok[2] = {0, 0};
	struct wk_pcb_stats st[2]; memset(st, 0, sizeof st);
	for (int i = 0; i < 2; i++) {
		size_t len = 0;
		if (sysctlbyname(lists[i].name, NULL, &len, NULL, 0) || len == 0) { rc[i] = errno ? errno : -1; continue; }
		len += len / 4 + 4096; if (len > PCB_CAP) len = PCB_CAP;
		uint8_t *buf = malloc(len); if (!buf) { rc[i] = ENOMEM; continue; }
		if (sysctlbyname(lists[i].name, buf, &len, NULL, 0)) rc[i] = errno ? errno : -1;
		else {
			struct pcb_ctx c = {n, lists[i].proto, 0};
			(void)wk_parse_pcblist(buf, len, lists[i].proto == 6, pcb_seen, &c, &st[i]); matched += c.matched;
			cov_ok[i] = st[i].errors == 0 && st[i].trailer_seen && st[i].short_records == 0;
		}
		free(buf);
	}
	/* 463.2 (review S1): a tracked socket missing from this sample is reported as
	 *   sock_gone reason=absent     only when this protocol's list was fully parsed and role discovery was complete;
	 *   sock_gone reason=owner_gone when the owning role pid is no longer listed (discovery complete);
	 *   sock_unseen reason=coverage otherwise (once per gap; the socket stays tracked and may reappear). */
	for (int i = 0; i < g_nsocks; i++) {
		struct sock_state *s = &g_socks[i];
		if (s->seen == n || s->seen < 0) continue;
		int pi = s->proto == 6 ? 0 : 1;
		if (g_roles_ok && !role_of_pid(s->pid)) {
			emit("sock_gone n=%d role=%d pid=%d sid=%d reason=owner_gone last_st=%d last_rx=%llu last_tx=%llu last_rcvq=%u",
			     n, s->role, s->pid, s->sid, (int)s->st, (unsigned long long)s->rx, (unsigned long long)s->tx, s->rcv);
			s->seen = -1;
		} else if (g_roles_ok && cov_ok[pi]) {
			emit("sock_gone n=%d role=%d pid=%d sid=%d reason=absent last_st=%d last_rx=%llu last_tx=%llu last_rcvq=%u",
			     n, s->role, s->pid, s->sid, (int)s->st, (unsigned long long)s->rx, (unsigned long long)s->tx, s->rcv);
			s->seen = -1;
		} else if (!s->unseen_reported) {
			emit("sock_unseen n=%d role=%d pid=%d sid=%d reason=coverage", n, s->role, s->pid, s->sid);
			s->unseen_reported = 1;
		}
	}
	emit("socksum n=%d roles_ok=%d tcp_groups=%d tcp_rc=%d tcp_err=%d tcp_short=%d tcp_incomplete=%d tcp_trailer=%d "
	     "udp_groups=%d udp_rc=%d udp_err=%d udp_short=%d udp_incomplete=%d udp_trailer=%d matched=%d tracked=%d",
	     n, g_roles_ok, st[0].groups, rc[0], st[0].errors, st[0].short_records, st[0].incomplete, st[0].trailer_seen,
	     st[1].groups, rc[1], st[1].errors, st[1].short_records, st[1].incomplete, st[1].trailer_seen, matched, g_nsocks);
}

/* ---------------- B: stackshot ---------------- */
static struct wk_ss g_ss;
static const char *frame_text(char *b, size_t bl, const struct wk_task *t, uint64_t sc_base, uint64_t lr, int kern)
{
	if (kern) { snprintf(b, bl, "k:%llx", (unsigned long long)lr); return b; }        /* unslid kernel address */
	if (sc_base && lr >= sc_base && lr - sc_base < (8ull << 30)) { snprintf(b, bl, "c:%llx", (unsigned long long)(lr - sc_base)); return b; }
	int best = -1; uint64_t ba = 0;
	for (unsigned i = 0; i < t->n_images; i++) if (t->img_addr[i] <= lr && t->img_addr[i] >= ba) { best = (int)i; ba = t->img_addr[i]; }
	/* 469: when the kernel listed more images than were kept, a nearest-lower kept image may be the wrong one */
	if (best >= 0 && lr - ba < (256ull << 20)) {
		snprintf(b, bl, "%si%d:%llx", t->n_images_total > t->n_images ? "~" : "", best, (unsigned long long)(lr - ba)); return b;
	}
	snprintf(b, bl, "?"); return b;
}
/* row kinds: "ssfr" (463 rows, main / 463 extras) or "ssfrw" (469 worker rows, carry ti) */
static void print_frames(const char *kind, int n, int k, int role, int pid, int main_thread, int ti, const struct wk_task *t,
			 uint64_t sc_base, const uint64_t *f, unsigned nf, unsigned cap, int kern)
{
	char line[400]; size_t o = 0; unsigned shown = nf < cap ? nf : cap, done = 0;
	line[0] = 0;
	for (unsigned i = 0; i < shown && o < sizeof line - 32; i++, done++) {
		char fb[40]; o += (size_t)snprintf(line + o, sizeof line - o, " %s", frame_text(fb, sizeof fb, t, sc_base, f[i], kern));
	}
	if (ti >= 0)
		emit("%s n=%d k=%d role=%d pid=%d ti=%d main=%d %s=%u/%u%s", kind, n, k, role, pid, ti, main_thread, kern ? "kern" : "user", done, nf, line);
	else
		emit("%s n=%d k=%d role=%d pid=%d main=%d %s=%u/%u%s", kind, n, k, role, pid, main_thread, kern ? "kern" : "user", done, nf, line);
}
static const char *owner_class(uint64_t o)
{
	if (o >= WK_STACKSHOT_WAITOWNER_FIRST_SPECIAL) return "special";
	if (o == 0) return "kernel_or_none";
	return o < 100000 ? "pid" : "other";
}
/* 469: resolved owner / final-inheritor fields shared by ssth and ssthw rows */
struct thread_links { int okind; uint64_t owner_tid; int owner_pid_t, owner_ti; uint64_t ts_tid; int ts_pid, ts_ti; };
static void resolve_links(const struct wk_thread *th, struct thread_links *L)
{
	memset(L, 0, sizeof *L); L->owner_pid_t = L->owner_ti = L->ts_pid = L->ts_ti = -1;
	L->okind = th->has_wait ? wk_owner_kind(th->wait_type, th->wait_owner) : WK_OWN_NONE;
	if (L->okind == WK_OWN_TID) { L->owner_tid = th->wait_owner; (void)wk_locate_tid(&g_ss, L->owner_tid, &L->owner_pid_t, &L->owner_ti); }
	if (th->has_ts && (th->ts_flags & WK_TSI_STATUS_THREAD)) { L->ts_tid = th->ts_context; (void)wk_locate_tid(&g_ss, L->ts_tid, &L->ts_pid, &L->ts_ti); }
	else if (th->has_ts && (th->ts_flags & WK_TSI_STATUS_ON_TASK) && th->ts_context < 100000) L->ts_pid = (int)th->ts_context;
}
static void report_thread(const char *rowk, const char *frk, int n, int k, int role, int pid, const struct wk_task *t,
			  uint64_t sc_base, const struct wk_thread *th, int is_main, int ti, unsigned ucap, int kern_if_empty)
{
	/* 463 field owner_pid is kept but, per 469 source review, filled only when the kernel's owner really is a pid
	 * (port send, semaphore, wait-on-process). For port receive it is a port NAME in the waiter's space (CORRECTIONS C4). */
	int owner_pid = -1;
	struct thread_links L; resolve_links(th, &L);
	if (L.okind == WK_OWN_PID) owner_pid = (int)th->wait_owner;
	int ts_pid = L.ts_pid;
	emit("%s n=%d k=%d role=%d pid=%d main=%d oncore=%d state=0x%x prio=%d user_ms=%llu sys_ms=%llu wait=%d wait_type=%u "
	     "owner_class=%s owner_pid=%d owner_role=%d special_reply=%d ts=%d ts_flags=0x%llx ts_hops=%u ts_pid=%d ts_role=%d "
	     "ti=%d of=%d tid=%llu owner_kind=%s owner_tid=%llu owner_tpid=%d owner_ti=%d ts_kind=%s ts_tid=%llu ts_ti=%d",
	     rowk, n, k, role, pid, is_main, (th->ss_flags & WK_TH_FLAG_ONCORE) != 0, th->state, th->sched_prio,
	     abs_to_ms(th->user_time), abs_to_ms(th->sys_time),
	     th->has_wait, th->has_wait ? th->wait_type : 0u, th->has_wait ? owner_class(th->wait_owner) : "-",
	     owner_pid, owner_pid > 0 ? role_of_pid(owner_pid) : 0, (th->wait_flags & WK_WI_FLAG_SPECIALREPLY) != 0,
	     th->has_ts, (unsigned long long)th->ts_flags, th->ts_hops, ts_pid, ts_pid > 0 ? role_of_pid(ts_pid) : 0,
	     ti, t->n_threads, (unsigned long long)th->tid, th->has_wait ? wk_owner_kind_name(th->wait_type, th->wait_owner) : "-",
	     (unsigned long long)L.owner_tid, L.owner_pid_t, L.owner_ti, wk_ts_kind_name(th), (unsigned long long)L.ts_tid, L.ts_ti);
	int fti = strcmp(frk, "ssfrw") ? -1 : ti;
	if (th->frames < 0) {
		if (fti >= 0) emit("%s n=%d k=%d role=%d pid=%d ti=%d main=%d frames=not_kept", frk, n, k, role, pid, ti, is_main);
		else emit("%s n=%d k=%d role=%d pid=%d main=%d frames=not_kept", frk, n, k, role, pid, is_main);
		return;
	}
	const struct wk_frames *fr = &g_ss.frame[th->frames];
	print_frames(frk, n, k, role, pid, is_main, fti, t, sc_base, fr->user, fr->n_user, ucap, 0);
	if (fr->n_kern || kern_if_empty) print_frames(frk, n, k, role, pid, is_main, fti, t, sc_base, fr->kern, fr->n_kern, is_main ? 12u : 8u, 1);
}
/* 469: thread ids referenced (direct owner or final inheritor) by any thread of any role task in this snapshot */
static uint64_t g_refs[MAX_REFS]; static int g_nrefs, g_refs_trunc;
static void add_ref(uint64_t tid)
{
	if (!tid) return;
	for (int i = 0; i < g_nrefs; i++) if (g_refs[i] == tid) return;
	if (g_nrefs < MAX_REFS) g_refs[g_nrefs++] = tid; else g_refs_trunc = 1;
}
static int is_ref(uint64_t tid) { for (int i = 0; i < g_nrefs; i++) if (g_refs[i] == tid) return 1; return 0; }
static void collect_refs(void)
{
	g_nrefs = 0; g_refs_trunc = 0;
	for (int ti = 0; ti < g_ss.n_tasks; ti++) {
		const struct wk_task *t = &g_ss.task[ti]; if (!role_of_pid(t->pid)) continue;
		for (int i = t->first_thread; i < t->first_thread + t->n_threads; i++) {
			const struct wk_thread *th = &g_ss.thread[i];
			if (th->has_wait && wk_owner_kind(th->wait_type, th->wait_owner) == WK_OWN_TID) add_ref(th->wait_owner);
			if (th->has_ts && (th->ts_flags & WK_TSI_STATUS_THREAD)) add_ref(th->ts_context);
		}
	}
}
/* worker priority: 0 = referenced by a role thread's owner/inheritor; 1 = running/runnable or in a wait that has an owner
 * or is synchronous IPC; 2 = other (condvar/portset/parked/none). Within a rank, kcdata order (ti). */
static int worker_rank(const struct wk_thread *th)
{
	if (is_ref(th->tid)) return 0;
	if (!(th->state & WK_TH_STATE_WAIT) && (th->state & WK_TH_STATE_RUN)) return 1;
	if (th->wait_flags & WK_WI_FLAG_SPECIALREPLY) return 1;
	if (th->has_wait) {
		int ok = wk_owner_kind(th->wait_type, th->wait_owner);
		if (ok == WK_OWN_TID || ok == WK_OWN_PID || th->wait_type == 0x04 || th->wait_type == 0x06 || th->wait_type == 0x10) return 1;
	}
	return 2;
}
static void report_workers(int n, int k, int role, const struct wk_task *t, uint64_t sc_base)
{
	int main_ti = -1, others = 0, shown = 0, by_rank[3] = {0, 0, 0};
	for (int i = t->first_thread; i < t->first_thread + t->n_threads; i++) {
		const struct wk_thread *th = &g_ss.thread[i];
		if ((th->ss_flags & WK_TH_FLAG_MAIN) && main_ti < 0) { main_ti = i - t->first_thread; continue; }
		others++; by_rank[worker_rank(th)]++;
	}
	int cap = others < WORKER_CAP ? others : WORKER_CAP;
	emit("sswk n=%d k=%d role=%d pid=%d threads=%d main_ti=%d workers=%d shown=%d omitted=%d trunc=%d cap=%d rank0=%d rank1=%d rank2=%d refs_trunc=%d",
	     n, k, role, t->pid, t->n_threads, main_ti, others, cap, others - cap, others > cap, WORKER_CAP,
	     by_rank[0], by_rank[1], by_rank[2], g_refs_trunc);
	for (int rank = 0; rank < 3 && shown < cap; rank++)
		for (int i = t->first_thread; i < t->first_thread + t->n_threads && shown < cap; i++) {
			const struct wk_thread *th = &g_ss.thread[i];
			if (i - t->first_thread == main_ti || worker_rank(th) != rank) continue;
			report_thread("ssthw", "ssfrw", n, k, role, t->pid, t, sc_base, th, 0, i - t->first_thread, 24u, 0);
			shown++;
		}
}
static void take_stackshot(int n, int k)
{
	wk_stackshot_config_t cfg; memset(&cfg, 0, sizeof cfg);
	cfg.sc_pid = -1;
	cfg.sc_flags = WK_SS_KCDATA_FORMAT | WK_SS_THREAD_WAITINFO | WK_SS_SAVE_LOADINFO | WK_SS_NO_IO_STATS;
	cfg.sc_out_buffer_addr = (uint64_t)(uintptr_t)&cfg.sc_buffer;
	cfg.sc_out_size_addr = (uint64_t)(uintptr_t)&cfg.sc_size;
	long long t0 = mono_ms();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	int rc = syscall(WK_SYS_STACK_SNAPSHOT_WITH_CONFIG, WK_STACKSHOT_CONFIG_TYPE, &cfg, sizeof cfg);
#pragma clang diagnostic pop
	int e = rc ? errno : 0;
	long long t1 = mono_ms();
	if (rc || !cfg.sc_buffer || !cfg.sc_size) { emit("ss n=%d k=%d rc=%d errno=%d bytes=0 ms=%lld", n, k, rc, e, t1 - t0); return; }
	int keep[WK_MAX_KEEP], nkeep = 0;
	for (int i = 0; i < g_nroles && nkeep < WK_MAX_KEEP; i++) keep[nkeep++] = g_roles[i].pid;
	int prc = wk_parse_stackshot((const uint8_t *)(uintptr_t)cfg.sc_buffer, cfg.sc_size, &g_ss, keep, nkeep);
	emit("ss n=%d k=%d rc=0 bytes=%u ms=%lld parse=%d tasks=%d threads=%d errors=%d truncated=%d frames_kept=%d frames_truncated=%d sc=%d",
	     n, k, cfg.sc_size, t1 - t0, prc, g_ss.n_tasks, g_ss.n_threads, g_ss.errors, g_ss.truncated, g_ss.n_frames,
	     g_ss.frames_truncated, g_ss.has_sc);
	if (g_ss.has_sc) {
		char u[40]; for (int i = 0; i < 16; i++) snprintf(u + 2 * i, 3, "%02x", g_ss.sc_uuid[i]);
		emit("sscache n=%d k=%d uuid=%s", n, k, u);
	}
	collect_refs();
	int worker_procs = 0;
	for (int ti = 0; ti < g_ss.n_tasks; ti++) {
		const struct wk_task *t = &g_ss.task[ti]; int role = role_of_pid(t->pid); if (!role) continue;
		uint64_t sc_base = t->has_sc ? t->sc_base : g_ss.sc_base;
		unsigned waits[0x16]; memset(waits, 0, sizeof waits); int nsync = 0, nmain = 0;
		for (int i = t->first_thread; i < t->first_thread + t->n_threads; i++) {
			const struct wk_thread *th = &g_ss.thread[i];
			if (th->has_wait && th->wait_type < 0x16) waits[th->wait_type]++;
			if (th->wait_flags & WK_WI_FLAG_SPECIALREPLY) nsync++;
		}
		emit("sstask n=%d k=%d role=%d pid=%d threads=%d images=%u sync_ipc_waiters=%d w_recv=%u w_pset=%u w_send=%u w_sema=%u "
		     "w_ulock=%u w_mutex=%u w_cond=%u w_wl=%u w_kmtx=%u w_other=%u images_total=%u images_trunc=%d",
		     n, k, role, t->pid, t->n_threads, t->n_images, nsync, waits[0x02], waits[0x03], waits[0x04], waits[0x06],
		     waits[0x0a], waits[0x0b], waits[0x0e], waits[0x10], waits[0x01],
		     waits[0x05] + waits[0x07] + waits[0x08] + waits[0x09] + waits[0x0c] + waits[0x0d] + waits[0x11] + waits[0x12] + waits[0x13] + waits[0x14],
		     t->n_images_total, t->n_images_total > t->n_images);
		/* 469 (review S4): every kept image (i0..i7) gets its UUID row */
		for (unsigned i = 0; i < t->n_images; i++) {
			char u[40]; for (int j = 0; j < 16; j++) snprintf(u + 2 * j, 3, "%02x", t->img_uuid[i][j]);
			emit("ssimg n=%d k=%d role=%d pid=%d idx=%u uuid=%s", n, k, role, t->pid, i, u);
		}
		/* main thread (463 row kinds, 469 fields appended) */
		for (int i = t->first_thread; i < t->first_thread + t->n_threads; i++) {
			const struct wk_thread *th = &g_ss.thread[i];
			if (th->ss_flags & WK_TH_FLAG_MAIN) { report_thread("ssth", "ssfr", n, k, role, t->pid, t, sc_base, th, 1, i - t->first_thread, 24u, 1); nmain++; }
		}
		if (!nmain) emit("ssnomain n=%d k=%d role=%d pid=%d", n, k, role, t->pid);
		if (role_has_workers(role)) {
			if (worker_procs < WORKER_PROCS_MAX) { report_workers(n, k, role, t, sc_base); worker_procs++; }
			else emit("sswk n=%d k=%d role=%d pid=%d threads=%d skipped=procs_cap", n, k, role, t->pid, t->n_threads);
		} else {
			/* roles 1, 3: as 463, up to 3 threads blocked in synchronous IPC */
			int extra = 0;
			for (int i = t->first_thread; i < t->first_thread + t->n_threads && extra < 3; i++) {
				const struct wk_thread *th = &g_ss.thread[i];
				if (!(th->ss_flags & WK_TH_FLAG_MAIN) && (th->wait_flags & WK_WI_FLAG_SPECIALREPLY)) {
					report_thread("ssth", "ssfr", n, k, role, t->pid, t, sc_base, th, 0, i - t->first_thread, 12u, 1); extra++;
				}
			}
		}
	}
	vm_deallocate(mach_task_self(), (vm_address_t)cfg.sc_buffer, cfg.sc_size);
}

int main(int argc, char **argv)
{
	int mirror = 0;
	for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-c") && !mirror) mirror = 1; else { emit("result=usage version=%s", VERSION); return 64; } }
	int fl = fcntl(STDOUT_FILENO, F_GETFL); if (fl >= 0) (void)fcntl(STDOUT_FILENO, F_SETFL, fl | O_NONBLOCK);
	if (mirror) g_console = console_open();
	struct sigaction a; memset(&a, 0, sizeof a); a.sa_handler = on_alarm; sigemptyset(&a.sa_mask);
	if (sigaction(SIGALRM, &a, NULL)) return 3;
	signal(SIGPIPE, SIG_IGN);
	alarm(ALARM_S);
	(void)mach_timebase_info(&g_tb);
	struct timespec w; int wok = clock_gettime(CLOCK_REALTIME, &w) == 0;
	emit("start version=%s uid=%u euid=%u samples=%d interval_ms=%d stackshot_at=2,5,8,11,14 alarm_s=%d console=%d "
	     "roles=1Networking,2GPU,3WebContent,4MobileSafari,5AppStore worker_roles=2,4,5 worker_cap=%d max_rows=%u",
	     VERSION, (unsigned)getuid(), (unsigned)geteuid(), SAMPLES, INTERVAL_MS, ALARM_S, g_console >= 0, WORKER_CAP, MAX_ROWS);
	emit("clock when=start known=%d epoch=%lld", wok, wok ? (long long)w.tv_sec : 0LL);
	long long base = mono_ms();
	for (int n = 0; n < SAMPLES; n++) {
		if (n) {
			long long due = base + (long long)n * INTERVAL_MS, now = mono_ms();
			if (now >= 0 && due > now) { struct timespec ts = {(time_t)((due - now) / 1000), (long)((due - now) % 1000) * 1000000L}; nanosleep(&ts, NULL); }
		}
		emit("sample n=%d t_ms=%lld", n, mono_ms() - base);
		discover_roles(n);
		sample_sockets(n);
		if (n == 2 || n == 5 || n == 8 || n == 11 || n == 14) take_stackshot(n, n == 2 ? 0 : n == 5 ? 1 : n == 8 ? 2 : n == 11 ? 3 : 4);
	}
	wok = clock_gettime(CLOCK_REALTIME, &w) == 0;
	emit("clock when=end known=%d epoch=%lld", wok, wok ? (long long)w.tv_sec : 0LL);
	/* rows counts this row; dropped/short counters cover all rows before it */
	emit("result=done samples=%d rows=%u version=%s rows_dropped=%u out_short=%u con_short=%u",
	     SAMPLES, g_rows + 1, VERSION, g_rows_dropped, g_out_short, g_con_short);
	alarm(0);
	return 0;
}
