/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-netstate425: finite, read-only snapshot of the SystemConfiguration dynamic-store keys that IPMonitor / the own netpublish
 * service produce, plus en0 interface flags, so a later real Safari / App Store load can be correlated with SC + ifnet state.
 *
 * Reads ONLY these fixed keys with SCDynamicStoreCopyValue (no key list, no pattern, no enumeration):
 *   State:/Network/Global/IPv4      State:/Network/Global/DNS      State:/Network/Global/Proxies
 *   State:/Network/Interface/en0/Link                              State:/Network/Interface/en0/IPv4
 *   State:/Network/Service/<own netpublish service>/IPv4 and /DNS   Plugin:InterfaceNamer (readiness keys only)
 * Never: SCDynamicStoreSet/Remove/notification calls, SCPreferences, SCNetworkInterfaceCopyAll, any publication or fake QUIET, any
 * socket/connect/DNS query, any nwi/private ABI (nwi is reported as unknown; root path state already comes from NETPATH376).
 * getifaddrs is used only to derive booleans for the interface named en0.
 * Output: numeric rows only (netstate425_out.h); no address, MAC, UUID text, pointer or dynamic string is ever printed.
 * Bounds: one frame every 5 s, at most 12 frames, optional start delay 0..600 s, hard watchdog 90 s after the delay
 * (alarm(delay + 90) armed at start), <= 160 rows, every write nonblocking and attempted once.
 * Usage: unixnu-netstate425 [-d SECONDS] [-n FRAMES] [-c]   (-c: optional /dev/console mirror; stdout always)
 */
#include "netstate425_sc.h"
#include "netstate425_out.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#define OWN_SERVICE "A7C30323-4E45-5444-4941-470000000323"   /* the own netpublish service ID (wifi-ci356 unixnu-netpublish.c) */

#define K_GLOBAL_IPV4   CFSTR("State:/Network/Global/IPv4")
#define K_GLOBAL_DNS    CFSTR("State:/Network/Global/DNS")
#define K_GLOBAL_PROXY  CFSTR("State:/Network/Global/Proxies")
#define K_EN0_LINK      CFSTR("State:/Network/Interface/en0/Link")
#define K_EN0_IPV4      CFSTR("State:/Network/Interface/en0/IPv4")
#define K_SERVICE_IPV4  CFSTR("State:/Network/Service/" OWN_SERVICE "/IPv4")
#define K_SERVICE_DNS   CFSTR("State:/Network/Service/" OWN_SERVICE "/DNS")
#define K_NAMER         CFSTR("Plugin:InterfaceNamer")

static volatile sig_atomic_t g_stop;
static void on_term(int s) { (void)s; g_stop = 1; }
static void on_watchdog(int s)
{
    (void)s;
    ns425_fixed_row_signal_safe(ns425_watchdog_row, sizeof ns425_watchdog_row - 1);
    _exit(124);
}

static long long mono_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int type_code(CFTypeRef v)
{
    if (!v) return 0;
    const CFTypeID t = CFGetTypeID(v);
    if (t == CFDictionaryGetTypeID()) return 1;
    if (t == CFArrayGetTypeID()) return 2;
    if (t == CFStringGetTypeID()) return 3;
    if (t == CFNumberGetTypeID()) return 4;
    if (t == CFBooleanGetTypeID()) return 5;
    if (t == CFDataGetTypeID()) return 6;
    if (t == CFDateGetTypeID()) return 7;
    return 9;
}

/* one fixed key; SCError captured immediately (absent key vs failed read are different) */
static CFPropertyListRef copy_key(SCDynamicStoreRef st, CFStringRef key, struct ns425_key *k)
{
    if (!st) { k->present = 0; k->type = 0; k->err = -1; return NULL; }
    CFPropertyListRef v = SCDynamicStoreCopyValue(st, key);
    k->err = v ? 0 : SCError();
    k->present = v != NULL;
    k->type = type_code(v);
    return v;
}

static CFTypeRef prop(CFPropertyListRef d, CFStringRef name)
{
    if (!d || CFGetTypeID(d) != CFDictionaryGetTypeID()) return NULL;
    return CFDictionaryGetValue((CFDictionaryRef)d, name);
}
static int is_dict(CFPropertyListRef d) { return d && CFGetTypeID(d) == CFDictionaryGetTypeID(); }
static int prop_has(CFPropertyListRef d, CFStringRef name) { return is_dict(d) ? (prop(d, name) != NULL) : -1; }
static int prop_str_is(CFPropertyListRef d, CFStringRef name, CFStringRef want)
{
    CFTypeRef v = prop(d, name);
    if (!v) return -1;
    if (CFGetTypeID(v) != CFStringGetTypeID()) return -2;
    return CFEqual(v, want) ? 1 : 0;
}
static int prop_count(CFPropertyListRef d, CFStringRef name)
{
    CFTypeRef v = prop(d, name);
    if (!v) return -1;
    if (CFGetTypeID(v) != CFArrayGetTypeID()) return -2;
    const CFIndex n = CFArrayGetCount((CFArrayRef)v);
    return n > 999 ? 999 : (int)n;
}
static int prop_bool(CFPropertyListRef d, CFStringRef name)
{
    CFTypeRef v = prop(d, name);
    if (!v) return -1;
    if (CFGetTypeID(v) == CFBooleanGetTypeID()) return CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
    if (CFGetTypeID(v) == CFNumberGetTypeID()) { int x = 0; return CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &x) ? (x != 0) : -2; }
    return -2;
}
static void cf_release(CFPropertyListRef v) { if (v) CFRelease(v); }

static void en0_flags(struct ns425_ifa *r)
{
    struct ifaddrs *list = NULL;
    r->ok = 0; r->err = 0; r->found = 0; r->up = -1; r->running = -1; r->has_inet4 = -1;
    if (getifaddrs(&list)) { r->err = errno; return; }
    r->ok = 1; r->has_inet4 = 0;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_name || strcmp(a->ifa_name, "en0")) continue;
        r->found = 1;
        r->up = (a->ifa_flags & IFF_UP) ? 1 : 0;
        r->running = (a->ifa_flags & IFF_RUNNING) ? 1 : 0;
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET) r->has_inet4 = 1;
    }
    if (!r->found) { r->up = -1; r->running = -1; r->has_inet4 = -1; }
    freeifaddrs(list);
}

static void frame(SCDynamicStoreRef st, int store_err, unsigned n)
{
    struct timespec wall; const int clock_ok = clock_gettime(CLOCK_REALTIME, &wall) == 0;
    ns425_row_frame(n, clock_ok, clock_ok ? (long long)wall.tv_sec : 0, mono_ms(), st != NULL, st ? 0 : store_err);

    struct ns425_gv4 g = {0}; CFPropertyListRef v = copy_key(st, K_GLOBAL_IPV4, &g.k);
    g.primary_if_en0 = prop_str_is(v, CFSTR("PrimaryInterface"), CFSTR("en0"));
    g.primary_service_present = prop_has(v, CFSTR("PrimaryService"));
    g.primary_service_own = prop_str_is(v, CFSTR("PrimaryService"), CFSTR(OWN_SERVICE));
    g.router_present = prop_has(v, CFSTR("Router"));
    cf_release(v); ns425_row_gv4(n, &g);

    struct ns425_dns d = {0}; v = copy_key(st, K_GLOBAL_DNS, &d.k);
    d.servers = prop_count(v, CFSTR("ServerAddresses")); d.ifname_en0 = -1;
    cf_release(v); ns425_row_dns(n, 1, &d);

    struct ns425_link l = {0}; v = copy_key(st, K_EN0_LINK, &l.k);
    l.active = prop_bool(v, CFSTR("Active"));
    cf_release(v); ns425_row_link(n, &l);

    struct ns425_if4 i4 = {0}; v = copy_key(st, K_EN0_IPV4, &i4.k);
    i4.addresses = prop_count(v, CFSTR("Addresses"));
    cf_release(v); ns425_row_if4(n, &i4);

    struct ns425_sv4 s = {0}; v = copy_key(st, K_SERVICE_IPV4, &s.k);
    s.addresses = prop_count(v, CFSTR("Addresses")); s.masks = prop_count(v, CFSTR("SubnetMasks"));
    s.router_present = prop_has(v, CFSTR("Router")); s.ifname_en0 = prop_str_is(v, CFSTR("InterfaceName"), CFSTR("en0"));
    s.method_present = prop_has(v, CFSTR("ConfigMethod"));
    cf_release(v); ns425_row_sv4(n, &s);

    struct ns425_dns sd = {0}; v = copy_key(st, K_SERVICE_DNS, &sd.k);
    sd.servers = prop_count(v, CFSTR("ServerAddresses")); sd.ifname_en0 = prop_str_is(v, CFSTR("InterfaceName"), CFSTR("en0"));
    cf_release(v); ns425_row_dns(n, 0, &sd);

    struct ns425_proxy p = {0}; v = copy_key(st, K_GLOBAL_PROXY, &p.k);
    p.http = prop_bool(v, CFSTR("HTTPEnable")); p.https = prop_bool(v, CFSTR("HTTPSEnable"));
    p.pac = prop_bool(v, CFSTR("ProxyAutoConfigEnable"));
    cf_release(v); ns425_row_proxy(n, &p);

    struct ns425_namer nm = {0}; v = copy_key(st, K_NAMER, &nm.k);
    nm.quiet = prop_has(v, CFSTR("*QUIET*")); nm.complete = prop_has(v, CFSTR("*COMPLETE*")); nm.timeout = prop_has(v, CFSTR("*TIMEOUT*"));
    cf_release(v); ns425_row_namer(n, &nm);

    struct ns425_ifa f; en0_flags(&f); ns425_row_ifa(n, &f);
}

/* sleep until mono deadline, 1 s slices, returns early on stop */
static void wait_until(long long deadline)
{
    for (;;) {
        if (g_stop) return;
        long long now = mono_ms();
        if (now < 0 || now >= deadline) return;
        long long left = deadline - now; if (left > 1000) left = 1000;
        struct timespec ts = {(time_t)(left / 1000), (long)(left % 1000) * 1000000L};
        (void)nanosleep(&ts, NULL);
    }
}

int main(int argc, char **argv)
{
    struct ns425_opts o;
    signal(SIGPIPE, SIG_IGN);
    const int fl = fcntl(STDOUT_FILENO, F_GETFL);
    if (fl < 0 || fcntl(STDOUT_FILENO, F_SETFL, fl | O_NONBLOCK)) return 3;
    if (ns425_parse_args(argc, argv, &o)) { ns425_row_usage(); return 64; }
    int console_errno = 0;
    if (o.console && ns425_console_enable("/dev/console")) console_errno = errno ? errno : EIO;

    struct sigaction a; memset(&a, 0, sizeof a); sigemptyset(&a.sa_mask);
    a.sa_handler = on_watchdog; if (sigaction(SIGALRM, &a, NULL)) return 3;
    a.sa_handler = on_term; if (sigaction(SIGTERM, &a, NULL) || sigaction(SIGINT, &a, NULL)) return 3;
    alarm(o.delay_s + NS425_WATCHDOG_S);

    ns425_row_start(&o, (unsigned)getuid(), (unsigned)geteuid(), (unsigned)getgid(), (unsigned)getegid(), ns425_console_fd >= 0, console_errno);
    long long t = mono_ms();
    if (o.delay_s) wait_until(t < 0 ? 0 : t + (long long)o.delay_s * 1000);

    SCDynamicStoreRef st = NULL; int store_err = 0;
    if (!g_stop) { st = SCDynamicStoreCreate(NULL, CFSTR("org.unixnu.diag.netstate425"), NULL, NULL); store_err = st ? 0 : SCError(); }
    unsigned done = 0;
    const long long base = mono_ms();
    for (unsigned n = 0; n < o.frames && !g_stop; ++n) {
        if (n) wait_until(base < 0 ? 0 : base + (long long)n * NS425_PERIOD_S * 1000);
        if (g_stop) break;
        frame(st, store_err, n);
        done++;
    }
    ns425_row_result(done == o.frames && !g_stop, done, g_stop != 0);
    alarm(0);
    if (st) CFRelease(st);
    if (ns425_console_fd >= 0) close(ns425_console_fd);
    return done == o.frames ? 0 : 5;
}
