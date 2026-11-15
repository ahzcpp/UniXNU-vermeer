/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-iokitholder385: own, read-only, bounded "who holds the IOKit root busy" probe for the K70 iOS 16.7 port.
 *
 * Why a new walk (366 -w could not answer it): 366 walked IOServiceGetMatchingServices(IOServiceMatching("IOService")). XNU
 * IOService::getMatchingServices() returns only services in kIOServiceMatchedState (iokit/Kernel/IOService.cpp:6182-6190), and that
 * state is set only at the END of a service's own doServiceMatch (copyNotifiers(gIOMatchedNotification, kIOServiceMatchedState) at
 * :5403, after probe/start of its drivers returned). A nub whose driver is still inside probe()/start(), or whose matching is
 * stalled, is busy but invisible to that walk. The early372 root/ArmDTPlatform-only readings suggest an unmatched descendant;
 * those non-atomic early readings do not prove a particular holder or persistence during the later UI wait.
 *
 * This walk: IORegistryEntryCreateIterator(IOService:/ , "IOService", recursive) visits every attached entry whatever its state.
 * XNU _adjustBusy() (IOService.cpp:5429-5510) adds +1 to the PROVIDER (first parent) when a child goes 0->busy, so for each busy
 * entry: self_busy = busy - (busy children whose first parent is this entry). An entry with self_busy > 0 is a CANDIDATE holder
 * (its own matching job, a driver in probe/start, a module stall, or termination). For such holders it prints only CLASS names:
 * the holder's class, its parent's class, and the distinct classes of its direct children (a driver stuck in start() is attached
 * as a child of its provider during probe/start, so e.g. "child_classes=AppleImage4" names the stuck driver).
 *
 * Reads only: busy counts (public IOServiceGetBusyState) and class names (IOObjectGetClass, validated [A-Za-z0-9_]).
 * Never reads entry names, registry IDs, paths, properties, addresses or identities; no user clients, no writes, nothing changed.
 * Bounded: <= WALK_CAP entries (+1 look-ahead), <= CHILD_CAP children per busy entry (+1 look-ahead), <= HOLDER_ROWS holder rows,
 * <= CHILD_CLASSES child class names per holder; console rows capped by CONSOLE_MAX_ROWS; alarm() time limit.
 * Any truncation, failed read or invalid name makes complete=no; a partial walk never means "nothing else holds busy".
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "iokitholder_abi.h"

#define TAG "IOKITHOLDER385"
#define VERSION "385.2"
#define LINE_MAX_BYTES 1536
#define WALK_CAP 1024        /* registry entries visited per walk (+1 look-ahead) */
#define CHILD_CAP 64         /* direct children read per busy entry (+1 look-ahead) */
#define HOLDER_ROWS 12       /* holder rows printed per walk; more -> holders_unprinted */
#define CHILD_CLASSES 4      /* distinct child class names per holder; more -> child_class_overflow */
#define MAX_SAMPLES 12
#define ROWS_PER_SAMPLE (1 + 1 + HOLDER_ROWS) /* root row, walk summary, holder rows */
#define CONSOLE_MAX_ROWS 200
_Static_assert(MAX_SAMPLES * ROWS_PER_SAMPLE + 2 <= CONSOLE_MAX_ROWS, "console budget must keep every row of the largest run");

static const char k_plane[128] = "IOService";
static const io_string_t k_root_path = "IOService:/"; /* full io_string_t buffer, as the prototype declares */
static int g_console_fd = -1;
static unsigned g_console_rows;

static void console_enable(void) {
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    int fl = fcntl(fd, F_GETFL);
    if (fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode) || fl < 0 || !(fl & O_NONBLOCK)) { close(fd); return; }
    g_console_fd = fd;
}

/* Return format completeness, not delivery acknowledgement. A bounded fallback
 * replaces an overlong row; no syntactically valid-looking partial row is sent. */
static int emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int emit(const char *fmt, ...) {
    char buf[LINE_MAX_BYTES];
    int p = snprintf(buf, sizeof buf, TAG " ");
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + p, sizeof buf - (size_t)p - 1, fmt, ap);
    va_end(ap);
    static const char unknown[] = TAG " output result=unknown reason=format_limit\n";
    int complete = n >= 0 && (size_t)n < sizeof buf - (size_t)p - 1;
    size_t len;
    const char *row;
    if (complete) { len = (size_t)p + (size_t)n; buf[len++] = '\n'; row = buf; }
    else { row = unknown; len = sizeof unknown - 1; }
    ssize_t w = write(STDOUT_FILENO, row, len);
    (void)w;
    if (g_console_fd >= 0 && g_console_rows < CONSOLE_MAX_ROWS) {
        g_console_rows++;
        w = write(g_console_fd, row, len);
        (void)w;
    }
    return complete;
}

static void on_alarm(int sig) {
    (void)sig;
    static const char m[] = TAG " RESULT result=stopped step=alarm (time limit reached)\n";
    ssize_t w = write(STDOUT_FILENO, m, sizeof m - 1);
    (void)w;
    _exit(124);
}

/* Darwin CLOCK_MONOTONIC counts from boot; -1 = unknown (never an uninitialized value). */
static long long uptime_ms(void) {
    struct timespec ts = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int valid_class(const char *s, size_t cap) {
    size_t i = 0;
    for (; i < cap && s[i]; i++) {
        char ch = s[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return 0;
    }
    return i > 0 && i < cap;
}

/* class name into out (validated) or "class_unknown"; returns 1 when valid */
static int class_of(io_object_t o, char out[128]) {
    io_name_t cls;
    memset(cls, 0, sizeof cls);
    if (IOObjectGetClass(o, cls) == KERN_SUCCESS && valid_class(cls, sizeof cls)) { memcpy(out, cls, sizeof cls); return 1; }
    strcpy(out, "class_unknown");
    return 0;
}

struct child_scan {
    int seen, truncated, unknown_reads, busy_children, foreign_busy, iterator_valid;
    int nclasses, class_overflow, class_unknown;
    char classes[CHILD_CLASSES][128];
    int counts[CHILD_CLASSES];
};

/* One bounded pass over the direct children of `e`. Counts busy children whose FIRST parent (= XNU provider) is `e`;
 * a busy child attached under another provider does not add to e's count (XNU propagates only to getProvider()).
 * want_classes: also aggregate child class names (only done for holders). */
static void scan_children(io_registry_entry_t e, int want_classes, struct child_scan *cs) {
    memset(cs, 0, sizeof *cs);
    io_iterator_t it = 0;
    if (IORegistryEntryGetChildIterator(e, k_plane, &it) != KERN_SUCCESS) { cs->unknown_reads++; return; }
    io_object_t c;
    while (cs->seen < CHILD_CAP && (c = IOIteratorNext(it)) != 0) {
        cs->seen++;
        uint32_t b = 0;
        if (IOServiceGetBusyState(c, &b) != KERN_SUCCESS) cs->unknown_reads++;
        else if (b) {
            io_registry_entry_t parent = 0;
            if (IORegistryEntryGetParentEntry(c, k_plane, &parent) != KERN_SUCCESS || !parent) cs->unknown_reads++;
            else {
                if (IOObjectIsEqualTo(parent, e)) cs->busy_children++;
                else cs->foreign_busy++;
                IOObjectRelease(parent);
            }
        }
        if (want_classes) {
            char name[128];
            if (!class_of(c, name)) cs->class_unknown++;
            else {
                int k = 0;
                for (; k < cs->nclasses; k++)
                    if (!strcmp(cs->classes[k], name)) break;
                if (k < cs->nclasses) cs->counts[k]++;
                else if (cs->nclasses < CHILD_CLASSES) { memcpy(cs->classes[cs->nclasses], name, 128); cs->counts[cs->nclasses++] = 1; }
                else cs->class_overflow++;
            }
        }
        IOObjectRelease(c);
    }
    if (cs->seen == CHILD_CAP) {
        io_object_t extra = IOIteratorNext(it);
        if (extra) { cs->truncated = 1; IOObjectRelease(extra); }
    }
    cs->iterator_valid = IOIteratorIsValid(it) != 0;
    IOObjectRelease(it);
}

struct walk_totals { int holders, printed, incomplete, metadata_incomplete, format_incomplete; };

/* Evaluate one busy entry. The reads are NOT an atomic snapshot (each IOServiceGetBusyState is its own kernel call while matching
 * threads keep running), so a row is only a CANDIDATE clue, never proof of a deadlock:
 *   reads_ok=no      -> a child read failed, the child list was truncated or the iterator was invalidated: self_busy=unknown
 *   stable=no        -> the entry's own busy changed between the read before and after the child scan: self_busy=unknown
 *   self_busy < 0    -> children changed during the scan (impossible under XNU's rule for one instant): self_busy=unknown
 * XNU rule used (IOService.cpp _adjustBusy 5429-5510): only a 0<->nonzero transition of an entry adds/removes 1 at its
 * getProvider() (first service-plane parent), recursively; detach/terminate move that 1 (960-1005, 2636). A busy child counts
 * for `e` only when IORegistryEntryGetParentEntry(child) == e; the cached getProvider() may lag a concurrent detach -> clue only. */
static void consider(int n, io_registry_entry_t e, uint32_t busy, int is_root, struct walk_totals *t) {
    struct child_scan cs;
    scan_children(e, 0, &cs);
    uint32_t busy_after = 0;
    int stable = IOServiceGetBusyState(e, &busy_after) == KERN_SUCCESS && busy_after == busy;
    int reads_ok = !cs.truncated && !cs.unknown_reads && cs.iterator_valid;
    long self = (long)busy - cs.busy_children;
    int known = reads_ok && stable && self >= 0;
    if (!known) t->incomplete++;
    if (known && self == 0) return; /* busy only through its busy children: not a candidate */
    t->holders++;
    if (t->printed >= HOLDER_ROWS) return;
    t->printed++;
    struct child_scan cc;
    scan_children(e, 1, &cc); /* second bounded pass for class names only; its counts are not used for self_busy */
    char cls[128], pcls[128] = "none";
    int entry_class_ok = class_of(e, cls);
    int parent_class_ok = 1;
    if (!is_root) {
        io_registry_entry_t parent = 0;
        if (IORegistryEntryGetParentEntry(e, k_plane, &parent) == KERN_SUCCESS && parent) { parent_class_ok = class_of(parent, pcls); IOObjectRelease(parent); }
        else { strcpy(pcls, "parent_unknown"); parent_class_ok = 0; }
    }
    int class_reads_ok = entry_class_ok && parent_class_ok && !cc.truncated && !cc.unknown_reads &&
        cc.iterator_valid && !cc.class_unknown && !cc.class_overflow;
    char kids[CHILD_CLASSES * 136 + 8];
    size_t used = 0;
    kids[0] = 0;
    for (int k = 0; k < cc.nclasses; k++) {
        int w = snprintf(kids + used, sizeof kids - used, "%s%s*%d", k ? "," : "", cc.classes[k], cc.counts[k]);
        if (w < 0 || (size_t)w >= sizeof kids - used) { class_reads_ok = 0; break; }
        used += (size_t)w;
    }
    if (!cc.nclasses) strcpy(kids, class_reads_ok ? "none" : "unknown");
    if (!class_reads_ok) t->metadata_incomplete++;
    char selfs[24];
    if (known) snprintf(selfs, sizeof selfs, "%ld", self);
    else strcpy(selfs, "unknown");
    int formatted = emit("candidate n=%d class=%s parent_class=%s busy=%u busy_after=%u busy_children=%d foreign_busy_children=%d self_busy=%s children_seen=%d children_truncated=%s child_unknown_reads=%d reads_ok=%s stable=%s child_classes=%s child_class_unknown=%d child_class_overflow=%d class_reads_ok=%s class_children_seen=%d class_children_truncated=%s class_unknown_reads=%d class_iterator_valid=%d",
         n, cls, pcls, busy, busy_after, cs.busy_children, cs.foreign_busy, selfs, cs.seen, cs.truncated ? "yes" : "no", cs.unknown_reads,
         reads_ok ? "yes" : "no", stable ? "yes" : "no", kids, cc.class_unknown, cc.class_overflow,
         class_reads_ok ? "yes" : "no", cc.seen, cc.truncated ? "yes" : "no", cc.unknown_reads, cc.iterator_valid);
    if (!formatted) t->format_incomplete++;
}
static void walk(int n, io_registry_entry_t root, uint32_t root_busy) {
    struct walk_totals t = {0};
    int seen = 0, busy_entries = 0, unknown_reads = 0, truncated = 0;
    consider(n, root, root_busy, 1, &t);
    io_iterator_t it = 0;
    kern_return_t kr = IORegistryEntryCreateIterator(root, k_plane, IH_ITERATE_RECURSIVELY, &it);
    if (kr != KERN_SUCCESS) { emit("walk n=%d result=unknown kr=0x%x complete=no uptime_ms=%lld", n, kr, uptime_ms()); return; }
    io_registry_entry_t e;
    while (seen < WALK_CAP && (e = IOIteratorNext(it)) != 0) {
        seen++;
        uint32_t b = 0;
        if (IOServiceGetBusyState(e, &b) != KERN_SUCCESS) unknown_reads++;
        else if (b) { busy_entries++; consider(n, e, b, 0, &t); }
        IOObjectRelease(e);
    }
    if (seen == WALK_CAP) {
        io_object_t extra = IOIteratorNext(it);
        if (extra) { truncated = 1; IOObjectRelease(extra); }
    }
    int iterator_valid = IOIteratorIsValid(it) != 0;
    IOObjectRelease(it);
    int complete = iterator_valid && !truncated && !unknown_reads && !t.incomplete && !t.metadata_incomplete &&
        !t.format_incomplete && t.printed == t.holders;
    emit("walk n=%d snapshot=non_atomic seen=%d cap=%d truncated=%s iterator_valid=%d busy_entries=%d unknown_reads=%d candidates=%d candidates_unprinted=%d unknown_self_entries=%d complete=%s uptime_ms=%lld unknown_class_entries=%d format_limit_rows=%d",
         n, seen, WALK_CAP, truncated ? "yes" : "no", iterator_valid, busy_entries, unknown_reads, t.holders, t.holders - t.printed,
         t.incomplete, complete ? "yes" : "no", uptime_ms(), t.metadata_incomplete, t.format_incomplete);
}

/* One sample: root busy row; when the root is busy (or -f), a full holder walk. */
static void sample(int n, int force) {
    io_registry_entry_t root = IORegistryEntryFromPath(MACH_PORT_NULL, k_root_path);
    if (!root) { emit("root n=%d result=unknown reason=IORegistryEntryFromPath_failed quiet=unknown uptime_ms=%lld", n, uptime_ms()); return; }
    uint32_t busy = 0;
    kern_return_t kr = IOServiceGetBusyState(root, &busy);
    if (kr != KERN_SUCCESS) emit("root n=%d result=unknown kr=0x%x quiet=unknown uptime_ms=%lld", n, kr, uptime_ms());
    else {
        emit("root n=%d busy=%u quiet=%s uptime_ms=%lld", n, busy, busy ? "no" : "yes", uptime_ms());
        if (busy || force) walk(n, root, busy);
    }
    IOObjectRelease(root);
}

static int parse_int(const char *s, int lo, int hi, int *out) {
    char *e = NULL;
    errno = 0;
    long v = strtol(s, &e, 10);
    if (errno || !e || *e || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

int main(int argc, char **argv) {
    int count = 1, interval = 30, delay = 0, force = 0, win_lo = -1, win_hi = -1, want_console = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) want_console = 1;
        else if (!strcmp(argv[i], "-f")) force = 1; /* walk even when the root is quiet (CI smoke only) */
        else if (!strcmp(argv[i], "-n") && i + 1 < argc && !parse_int(argv[++i], 1, MAX_SAMPLES, &count)) {}
        else if (!strcmp(argv[i], "-i") && i + 1 < argc && !parse_int(argv[++i], 10, 120, &interval)) {}
        else if (!strcmp(argv[i], "-d") && i + 1 < argc && !parse_int(argv[++i], 0, 600, &delay)) {}
        else if (!strcmp(argv[i], "-u") && i + 2 < argc && !parse_int(argv[i + 1], 0, 3600, &win_lo) &&
                 !parse_int(argv[i + 2], 0, 3600, &win_hi) && win_lo < win_hi) { i += 2; }
        else {
            static const char u[] = "usage: unixnu-iokitholder385 [-c] [-f] [-n samples 1..12] [-i seconds 10..120] [-d delay 0..600]"
                                    " [-u lo_s hi_s (emit only while boot uptime is inside [lo,hi])]\n";
            ssize_t w = write(STDERR_FILENO, u, sizeof u - 1);
            (void)w;
            return 2;
        }
    }
    /* -u: for a periodic launchd job each short run exits silently outside the boot-uptime window, so total output stays bounded
     * even though launchd restarts it; an unknown clock also exits silently (no guessing). */
    if (win_lo >= 0) {
        long long now = uptime_ms();
        if (now < 0 || now < (long long)win_lo * 1000 || now > (long long)win_hi * 1000) return 0;
    }
    if (want_console) console_enable();
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    unsigned limit = (unsigned)(delay + (count - 1) * interval + 30);
    alarm(limit);
    emit("start version=" VERSION " samples=%d interval_s=%d delay_s=%d alarm_s=%u console=%s window=%d..%d uptime_ms=%lld", count, interval,
         delay, limit, g_console_fd >= 0 ? "on" : "off", win_lo, win_hi, uptime_ms());
    if (delay) sleep((unsigned)delay);
    for (int n = 0; n < count; n++) {
        sample(n, force);
        if (n + 1 < count) sleep((unsigned)interval);
    }
    emit("RESULT result=done samples=%d uptime_ms=%lld", count, uptime_ms());
    alarm(0);
    return 0;
}
