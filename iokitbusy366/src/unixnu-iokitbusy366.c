/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-iokitbusy366: own, read-only, bounded IOKit busy-state sampler for the K70 iOS 16.7 port.
 *
 * Question it answers: does the IOKit root ("IOService:/", the object configd's InterfaceNamer watches with IOBusyInterest) ever
 * reach busy 0, and are the fixed network service classes themselves busy? configd publishes "*QUIET*" only on root busy == 0;
 * otherwise SCNetworkInterfaceCopyAll callers wait for "*TIMEOUT*" (IONetworkStack found + 240 s in 20H392).
 *
 * Reads only: IORegistryEntryFromPath("IOService:/"), IOServiceGetMatchingServices(IOServiceMatching(<fixed class>)),
 * IOServiceGetBusyStateAndTime. Prints counts, busy counts, accumulated busy ms and state-bit tallies. Never prints names,
 * registry IDs, paths, properties, addresses or any identity. No writes, no user clients, no properties set.
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
#include "iokitbusy_abi.h"

#define TAG "IOKITBUSY366"
#define LINE_MAX_BYTES 400
#define CONSOLE_MAX_ROWS 360 /* finite; must hold every row of the largest run, see the assert below */
#define MAX_PER_CLASS 64     /* at most 64 objects are taken from each iterator, plus one look-ahead */
#define MAX_SAMPLES 40
/* -w class-only busy walk (explicit flag): only when the root read says busy > 0, at most once per 60 s, at most 6 per run */
#define WALK_MAX_ENTRIES 512
#define WALK_MAX_CLASSES 16
#define WALK_MAX_RUNS 6
#define WALK_MIN_GAP_MS 60000LL

static const char *const k_classes[] = {"IONetworkStack", "IO80211Controller", "IONetworkInterface", "IO80211Interface"};
#define NCLASSES (sizeof k_classes / sizeof k_classes[0])
/* rows per sample: root + one per class + one summary; plus start and RESULT */
#define ROWS_PER_SAMPLE (1 + 4 + 1)
_Static_assert(sizeof k_classes / sizeof k_classes[0] == 4, "ROWS_PER_SAMPLE assumes 4 classes");
_Static_assert(MAX_SAMPLES * ROWS_PER_SAMPLE + 2 + WALK_MAX_RUNS * (1 + WALK_MAX_CLASSES) <= CONSOLE_MAX_ROWS,
               "console budget must keep every row (late samples and walks matter)");

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

static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *fmt, ...) {
    char buf[LINE_MAX_BYTES];
    int p = snprintf(buf, sizeof buf, TAG " ");
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + p, sizeof buf - (size_t)p - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = (size_t)p + (size_t)n;
    if (len > sizeof buf - 2) len = sizeof buf - 2;
    buf[len++] = '\n';
    ssize_t w = write(STDOUT_FILENO, buf, len);
    (void)w;
    if (g_console_fd >= 0 && g_console_rows < CONSOLE_MAX_ROWS) {
        g_console_rows++;
        w = write(g_console_fd, buf, len);
        (void)w;
    }
}

static void on_alarm(int sig) {
    (void)sig;
    static const char m[] = TAG " RESULT result=stopped step=alarm (time limit reached)\n";
    ssize_t w = write(STDOUT_FILENO, m, sizeof m - 1);
    (void)w;
    _exit(124);
}

/* Monotonic ms (Darwin CLOCK_MONOTONIC counts from boot); -1 = unknown when clock_gettime fails (never an uninitialized value). */
static long long uptime_ms(void) {
    struct timespec ts = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* accumulated busy time is in nanoseconds (IOService::getBusyStateAndTime uses absolute time converted to ns) */
static long long ns_to_ms(uint64_t ns) { return (long long)(ns / 1000000ull); }

/* ---- -w: class-only busy walk ----
 * Reads, for at most WALK_MAX_ENTRIES registered services (IOServiceMatching("IOService")), only the busy count; for busy ones only
 * the class name (IOObjectGetClass, 128-byte io_name_t, accepted only if NUL-terminated and [A-Za-z0-9_]). Aggregates at most
 * WALK_MAX_CLASSES class names with counts. Never reads entry names, IDs, paths, properties or addresses; changes nothing.
 * Partial walks (truncated / unknown reads / overflowed class table) are reported as such and never mean "nothing else busy". */
struct walk_class { char name[128]; int count; };

static int valid_class(const char *s, size_t cap) {
    size_t i = 0;
    for (; i < cap && s[i]; i++) {
        char ch = s[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return 0;
    }
    return i > 0 && i < cap;
}

static void busy_walk(int n) {
    static struct walk_class tab[WALK_MAX_CLASSES];
    memset(tab, 0, sizeof tab);
    int distinct = 0, overflow = 0, seen = 0, busy_entries = 0, unknown_reads = 0, name_unknown = 0;
    const char *truncated = "no";
    io_iterator_t it = 0;
    CFMutableDictionaryRef match = IOServiceMatching("IOService");
    if (!match) { emit("walk n=%d result=unknown reason=matching_dict uptime_ms=%lld", n, uptime_ms()); return; }
    kern_return_t kr = IOServiceGetMatchingServices(MACH_PORT_NULL, match, &it); /* consumes match */
    if (kr != KERN_SUCCESS) { emit("walk n=%d result=unknown kr=0x%x uptime_ms=%lld", n, kr, uptime_ms()); return; }
    io_service_t s;
    while (seen < WALK_MAX_ENTRIES && (s = IOIteratorNext(it)) != 0) {
        uint64_t st = 0, a = 0;
        uint32_t b = 0;
        seen++;
        if (IOServiceGetBusyStateAndTime(s, &st, &b, &a) != KERN_SUCCESS) { unknown_reads++; IOObjectRelease(s); continue; }
        if (b == 0) { IOObjectRelease(s); continue; }
        busy_entries++;
        io_name_t cls;
        memset(cls, 0, sizeof cls);
        if (IOObjectGetClass(s, cls) != KERN_SUCCESS || !valid_class(cls, sizeof cls)) { name_unknown++; IOObjectRelease(s); continue; }
        IOObjectRelease(s);
        int k = 0;
        for (; k < distinct; k++)
            if (!strcmp(tab[k].name, cls)) break;
        if (k < distinct) tab[k].count++;
        else if (distinct < WALK_MAX_CLASSES) { memcpy(tab[distinct].name, cls, sizeof cls); tab[distinct].count = 1; distinct++; }
        else overflow++;
    }
    if (seen == WALK_MAX_ENTRIES) {
        io_service_t extra = IOIteratorNext(it);
        if (extra) { truncated = "yes"; IOObjectRelease(extra); }
    }
    int iterator_valid = IOIteratorIsValid(it) != 0;
    IOObjectRelease(it);
    int complete = iterator_valid && truncated[0] == 'n' && !unknown_reads && !name_unknown && !overflow;
    emit("walk n=%d seen=%d cap=%d truncated=%s iterator_valid=%d busy_entries=%d unknown_reads=%d class_name_unknown=%d distinct=%d overflow_entries=%d complete=%s uptime_ms=%lld",
         n, seen, WALK_MAX_ENTRIES, truncated, iterator_valid, busy_entries, unknown_reads, name_unknown, distinct, overflow, complete ? "yes" : "no", uptime_ms());
    for (int k = 0; k < distinct; k++) emit("walkclass n=%d class=%s busy_entries=%d", n, tab[k].name, tab[k].count);
}

static int g_walk_enabled, g_walk_runs;
static long long g_walk_last_ms = -1;

/* One sample = 6 rows: root, 4 classes, summary. "complete" is yes only when every read succeeded and no class was truncated;
 * only then may a reader treat class busy counts as covering the class. Root quiet is reported from the root read alone. */
static void sample(int n) {
    uint64_t state = 0, acc = 0;
    uint32_t busy = 0;
    const char *root_quiet = "unknown";
    int complete = 1, classes_busy = 0;
    io_registry_entry_t root = IORegistryEntryFromPath(MACH_PORT_NULL, "IOService:/");
    if (!root) {
        complete = 0;
        emit("root n=%d result=unknown reason=IORegistryEntryFromPath_failed quiet=unknown uptime_ms=%lld", n, uptime_ms());
    } else {
        kern_return_t kr = IOServiceGetBusyStateAndTime(root, &state, &busy, &acc);
        if (kr != KERN_SUCCESS) {
            complete = 0;
            emit("root n=%d result=unknown kr=0x%x quiet=unknown uptime_ms=%lld", n, kr, uptime_ms());
        } else {
            root_quiet = busy == 0 ? "yes" : "no";
            emit("root n=%d busy=%u acc_busy_ms=%lld quiet=%s uptime_ms=%lld", n, busy, ns_to_ms(acc), root_quiet, uptime_ms());
        }
        IOObjectRelease(root);
    }
    for (size_t c = 0; c < NCLASSES; c++) {
        io_iterator_t it = 0;
        CFMutableDictionaryRef match = IOServiceMatching(k_classes[c]);
        if (!match) { complete = 0; emit("class n=%d name=%s result=unknown reason=matching_dict", n, k_classes[c]); continue; }
        kern_return_t kr = IOServiceGetMatchingServices(MACH_PORT_NULL, match, &it); /* consumes match */
        if (kr != KERN_SUCCESS) { complete = 0; emit("class n=%d name=%s result=unknown kr=0x%x", n, k_classes[c], kr); continue; }
        int seen = 0, busy_objs = 0, unknown = 0, registered = 0, matched = 0, inactive = 0;
        uint32_t busy_max = 0;
        uint64_t acc_max = 0;
        io_service_t s;
        /* bounded: at most MAX_PER_CLASS objects are taken from the iterator */
        while (seen < MAX_PER_CLASS && (s = IOIteratorNext(it)) != 0) {
            uint64_t st = 0, a = 0;
            uint32_t b = 0;
            if (IOServiceGetBusyStateAndTime(s, &st, &b, &a) == KERN_SUCCESS) {
                if (b) busy_objs++;
                if (b > busy_max) busy_max = b;
                if (a > acc_max) acc_max = a;
                if (st & IB_STATE_REGISTERED) registered++;
                if (st & IB_STATE_MATCHED) matched++;
                if (st & IB_STATE_INACTIVE) inactive++;
            } else {
                unknown++;
            }
            seen++;
            IOObjectRelease(s);
        }
        /* one bounded look-ahead at the cap: truncated=yes means "more objects exist", never a total population */
        const char *truncated = "no";
        if (seen == MAX_PER_CLASS) {
            io_service_t extra = IOIteratorNext(it);
            if (extra) { truncated = "yes"; IOObjectRelease(extra); }
        }
        int iterator_valid = IOIteratorIsValid(it) != 0;
        IOObjectRelease(it);
        if (!iterator_valid) unknown++;
        if (unknown || truncated[0] == 'y') complete = 0;
        if (busy_objs) classes_busy++;
        emit("class n=%d name=%s seen=%d cap=%d truncated=%s iterator_valid=%d busy_objects=%d busy_max=%u acc_busy_ms_max=%lld registered=%d matched=%d inactive=%d unknown=%d",
             n, k_classes[c], seen, MAX_PER_CLASS, truncated, iterator_valid, busy_objs, busy_max, ns_to_ms(acc_max), registered, matched, inactive, unknown);
    }
    emit("sample n=%d root_quiet=%s classes_with_busy=%d complete=%s uptime_ms=%lld", n, root_quiet, classes_busy, complete ? "yes" : "no", uptime_ms());
    /* -w: only on an actual root busy > 0 reading, >= 60 s apart, at most WALK_MAX_RUNS times, needs a working clock */
    if (g_walk_enabled && root_quiet[0] == 'n' && g_walk_runs < WALK_MAX_RUNS) {
        long long now = uptime_ms();
        if (now < 0) emit("walk n=%d result=skipped reason=clock_unknown", n);
        else if (g_walk_last_ms < 0 || now - g_walk_last_ms >= WALK_MIN_GAP_MS) {
            g_walk_last_ms = now;
            g_walk_runs++;
            busy_walk(n);
        }
    }
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
    int count = 16, interval = 20, delay = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) console_enable();
        else if (!strcmp(argv[i], "-w")) g_walk_enabled = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc && !parse_int(argv[++i], 1, MAX_SAMPLES, &count)) {}
        else if (!strcmp(argv[i], "-i") && i + 1 < argc && !parse_int(argv[++i], 1, 60, &interval)) {}
        else if (!strcmp(argv[i], "-d") && i + 1 < argc && !parse_int(argv[++i], 0, 600, &delay)) {}
        else {
            static const char u[] = "usage: unixnu-iokitbusy366 [-c] [-w] [-n samples 1..40] [-i seconds 1..60] [-d delay 0..600]\n";
            ssize_t w = write(STDERR_FILENO, u, sizeof u - 1);
            (void)w;
            return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    unsigned limit = (unsigned)(delay + count * interval + 30);
    alarm(limit);
    emit("start version=366.4 samples=%d interval_s=%d delay_s=%d alarm_s=%u console=%s walk=%s uptime_ms=%lld", count, interval, delay,
         limit, g_console_fd >= 0 ? "on" : "off", g_walk_enabled ? "on" : "off", uptime_ms());
    if (delay) sleep((unsigned)delay);
    for (int n = 0; n < count; n++) {
        sample(n);
        if (n + 1 < count) sleep((unsigned)interval);
    }
    emit("RESULT result=done samples=%d uptime_ms=%lld", count, uptime_ms());
    alarm(0);
    return 0;
}
