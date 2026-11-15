/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * netstate425 portable part: fixed numeric row schema, bounded argument parsing, bounded nonblocking output sinks.
 * No CoreFoundation / SystemConfiguration here (host-testable). Every row is printf'd from integer fields only: no %s, no
 * address, MAC, UUID, pointer or dynamic string can reach the output. Values are clamped to fixed ranges, so every row has a
 * bounded length (< NS425_LINE_MAX).
 *
 * Value codes used by all rows:
 *   tri-state  1 = true, 0 = false, -1 = key/property missing (or parent not a dictionary), -2 = present with wrong CF type
 *   count      0..999 = CFArray element count (clamped), -1 = missing, -2 = wrong CF type
 *   type       0 absent, 1 dictionary, 2 array, 3 string, 4 number, 5 boolean, 6 data, 7 date, 9 other
 *   err        SCError() captured immediately after a NULL SCDynamicStoreCopyValue (1004 kSCStatusNoKey = key absent),
 *              0 when the value was returned, -1 when no store exists
 */
#ifndef NETSTATE425_OUT_H
#define NETSTATE425_OUT_H
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NS425_VERSION_MAJOR 425
#define NS425_VERSION_MINOR 1
#define NS425_MAX_FRAMES 12u
#define NS425_PERIOD_S 5u
#define NS425_WATCHDOG_S 90u
#define NS425_MAX_DELAY_S 600u
#define NS425_MAX_ROWS 160u
#define NS425_LINE_MAX 320

struct ns425_opts { unsigned delay_s, frames; int console; };
struct ns425_key { int present, type, err; };
struct ns425_gv4 { struct ns425_key k; int primary_if_en0, primary_service_present, primary_service_own, router_present; };
struct ns425_dns { struct ns425_key k; int servers, ifname_en0; };
struct ns425_link { struct ns425_key k; int active; };
struct ns425_sv4 { struct ns425_key k; int addresses, masks, router_present, ifname_en0, method_present; };
struct ns425_if4 { struct ns425_key k; int addresses; };
struct ns425_namer { struct ns425_key k; int quiet, complete, timeout; };
struct ns425_proxy { struct ns425_key k; int http, https, pac; };
struct ns425_ifa { int ok, err, found, up, running, has_inet4; };

static int ns425_stdout_fd = STDOUT_FILENO;   /* host test may point this at a pipe */
static int ns425_console_fd = -1;
static unsigned ns425_rows;

static inline int ns425_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int ns425_tri(int v) { return ns425_clamp(v, -2, 1); }
static inline int ns425_cnt(int v) { return ns425_clamp(v, -2, 999); }
static inline int ns425_typ(int v) { return (v >= 0 && v <= 7) ? v : 9; }
static inline int ns425_err(int v) { return ns425_clamp(v, -1, 99999); }

/* digits only, at most 3 characters, value <= max */
static inline int ns425_parse_uint(const char *s, unsigned max, unsigned *out)
{
    if (!s || !*s || strlen(s) > 3) return -1;
    unsigned v = 0;
    for (const char *p = s; *p; ++p) { if (*p < '0' || *p > '9') return -1; v = v * 10u + (unsigned)(*p - '0'); }
    if (v > max) return -1;
    *out = v; return 0;
}

/* accepted: [-d 0..600] [-n 1..12] [-c], each at most once; anything else -> 64 */
static inline int ns425_parse_args(int argc, char **argv, struct ns425_opts *o)
{
    int seen_d = 0, seen_n = 0;
    o->delay_s = 0; o->frames = NS425_MAX_FRAMES; o->console = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") && !o->console) o->console = 1;
        else if (!strcmp(argv[i], "-d") && !seen_d && i + 1 < argc) {
            if (ns425_parse_uint(argv[++i], NS425_MAX_DELAY_S, &o->delay_s)) return 64;
            seen_d = 1;
        } else if (!strcmp(argv[i], "-n") && !seen_n && i + 1 < argc) {
            unsigned n;
            if (ns425_parse_uint(argv[++i], NS425_MAX_FRAMES, &n) || n == 0) return 64;
            o->frames = n; seen_n = 1;
        } else return 64;
    }
    return 0;
}

/* Optional mirror, same rules as the existing own netpublish sink: O_NOFOLLOW, nonblocking, close-on-exec, character device
 * only; one write attempt per row, no retry. The path is a parameter only for the host test; the tool passes "/dev/console". */
static inline int ns425_console_enable(const char *path)
{
    int saved = errno;
    if (ns425_console_fd >= 0) return 0;
    int fd = open(path, O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st)) { int e = errno; close(fd); errno = e; return -1; }
    if (!S_ISCHR(st.st_mode)) { close(fd); errno = ENODEV; return -1; }
    int fl = fcntl(fd, F_GETFL), fdfl = fcntl(fd, F_GETFD);
    if (fl < 0 || fdfl < 0) { int e = errno; close(fd); errno = e; return -1; }
    if (!(fl & O_NONBLOCK) || !(fdfl & FD_CLOEXEC)) { close(fd); errno = ENOTSUP; return -1; }
    ns425_console_fd = fd; errno = saved; return 0;
}

/* stdout is set O_NONBLOCK by the tool; one write attempt per sink, row cap shared by both */
static inline void ns425_raw(const char *s, size_t n)
{
    int saved = errno;
    if (s && n > 0 && n < NS425_LINE_MAX && ns425_rows < NS425_MAX_ROWS) {
        ns425_rows++;
        ssize_t ignored = write(ns425_stdout_fd, s, n); (void)ignored;
        if (ns425_console_fd >= 0) { ignored = write(ns425_console_fd, s, n); (void)ignored; }
    }
    errno = saved;
}

static void ns425_emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ns425_emit(const char *fmt, ...)
{
    char b[NS425_LINE_MAX];
    int off = snprintf(b, sizeof b, "NETSTATE425 ");
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b + off, sizeof b - (size_t)off - 1, fmt, ap);
    va_end(ap);
    if (off < 0 || n < 0) return;
    size_t len = (size_t)off + (size_t)n;
    if (len > sizeof b - 2) len = sizeof b - 2;
    b[len++] = '\n';
    ns425_raw(b, len);
}

/* async-signal-safe fixed rows (watchdog) */
static const char ns425_watchdog_row[] = "NETSTATE425 result=incomplete reason=watchdog version=425.1\n";
static inline void ns425_fixed_row_signal_safe(const char *s, size_t n)
{
    ssize_t ignored = write(ns425_stdout_fd, s, n); (void)ignored;
    if (ns425_console_fd >= 0) { ignored = write(ns425_console_fd, s, n); (void)ignored; }
}

/* ---- fixed schema rows (field order is the schema) ---- */
static inline void ns425_row_start(const struct ns425_opts *o, unsigned uid, unsigned euid, unsigned gid, unsigned egid, int console, int console_errno)
{
    ns425_emit("start source=425 label=425 version=%d.%d uid=%u euid=%u gid=%u egid=%u delay_s=%u frames_max=%u period_s=%u "
               "watchdog_s=%u console=%d console_errno=%d sc_copyvalue_only=1 nwi_probe=0 nwi=unknown",
               NS425_VERSION_MAJOR, NS425_VERSION_MINOR, uid, euid, gid, egid, o->delay_s, o->frames, NS425_PERIOD_S,
               NS425_WATCHDOG_S, console ? 1 : 0, ns425_clamp(console_errno, 0, 999));
}
static inline void ns425_row_frame(unsigned n, int clock_ok, long long epoch, long long mono_ms, int store, int store_err)
{
    ns425_emit("frame n=%u clock_known=%d epoch=%lld mono_ms=%lld store=%d store_err=%d", n, clock_ok ? 1 : 0,
               clock_ok ? epoch : 0LL, mono_ms, store ? 1 : 0, ns425_err(store_err));
}
#define NS425_KEYFMT "present=%d type=%d err=%d"
#define NS425_KEYARGS(k) ((k).present ? 1 : 0), ns425_typ((k).type), ns425_err((k).err)
static inline void ns425_row_gv4(unsigned n, const struct ns425_gv4 *v)
{
    ns425_emit("global_ipv4 n=%u " NS425_KEYFMT " primary_if_en0=%d primary_service_present=%d primary_service_own=%d router_present=%d",
               n, NS425_KEYARGS(v->k), ns425_tri(v->primary_if_en0), ns425_tri(v->primary_service_present),
               ns425_tri(v->primary_service_own), ns425_tri(v->router_present));
}
static inline void ns425_row_dns(unsigned n, int global, const struct ns425_dns *v)
{
    if (global) ns425_emit("global_dns n=%u " NS425_KEYFMT " servers=%d", n, NS425_KEYARGS(v->k), ns425_cnt(v->servers));
    else ns425_emit("service_dns n=%u " NS425_KEYFMT " servers=%d ifname_en0=%d", n, NS425_KEYARGS(v->k), ns425_cnt(v->servers),
                    ns425_tri(v->ifname_en0));
}
static inline void ns425_row_link(unsigned n, const struct ns425_link *v)
{
    ns425_emit("en0_link n=%u " NS425_KEYFMT " active=%d", n, NS425_KEYARGS(v->k), ns425_tri(v->active));
}
static inline void ns425_row_sv4(unsigned n, const struct ns425_sv4 *v)
{
    ns425_emit("service_ipv4 n=%u " NS425_KEYFMT " addresses=%d masks=%d router_present=%d ifname_en0=%d method_present=%d",
               n, NS425_KEYARGS(v->k), ns425_cnt(v->addresses), ns425_cnt(v->masks), ns425_tri(v->router_present),
               ns425_tri(v->ifname_en0), ns425_tri(v->method_present));
}
static inline void ns425_row_if4(unsigned n, const struct ns425_if4 *v)
{
    ns425_emit("en0_ipv4 n=%u " NS425_KEYFMT " addresses=%d", n, NS425_KEYARGS(v->k), ns425_cnt(v->addresses));
}
static inline void ns425_row_namer(unsigned n, const struct ns425_namer *v)
{
    ns425_emit("namer n=%u " NS425_KEYFMT " quiet=%d complete=%d timeout=%d", n, NS425_KEYARGS(v->k), ns425_tri(v->quiet),
               ns425_tri(v->complete), ns425_tri(v->timeout));
}
static inline void ns425_row_proxy(unsigned n, const struct ns425_proxy *v)
{
    ns425_emit("global_proxies n=%u " NS425_KEYFMT " http_enable=%d https_enable=%d pac_enable=%d", n, NS425_KEYARGS(v->k),
               ns425_tri(v->http), ns425_tri(v->https), ns425_tri(v->pac));
}
static inline void ns425_row_ifa(unsigned n, const struct ns425_ifa *v)
{
    ns425_emit("en0_ifaddrs n=%u ok=%d errno=%d found=%d up=%d running=%d has_inet4=%d", n, v->ok ? 1 : 0,
               ns425_clamp(v->err, 0, 999), ns425_tri(v->found), ns425_tri(v->up), ns425_tri(v->running), ns425_tri(v->has_inet4));
}
static inline void ns425_row_result(int complete, unsigned frames, int stopped)
{
    if (complete) ns425_emit("result=complete frames=%u stopped=%d version=%d.%d", frames, stopped ? 1 : 0, NS425_VERSION_MAJOR, NS425_VERSION_MINOR);
    else ns425_emit("result=incomplete frames=%u stopped=%d version=%d.%d", frames, stopped ? 1 : 0, NS425_VERSION_MAJOR, NS425_VERSION_MINOR);
}
static inline void ns425_row_usage(void) { ns425_emit("result=usage version=%d.%d", NS425_VERSION_MAJOR, NS425_VERSION_MINOR); }
#endif
