/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Host simulation of the public IOKit calls used by unixnu-iokitholder385, over synthetic registry trees.
 * Busy semantics follow XNU IOService::_adjustBusy (iokit/Kernel/IOService.cpp:5429-5510): an entry's busy count is its own
 * busy plus one for every child whose PROVIDER (first parent in the service plane) is that entry and whose count is > 0.
 * Synthetic data only (class names are generic IOKit / K70 class names, no device data). Every object handed out is
 * reference-counted; at exit the harness reports leaked references, so the probe's release discipline is tested too.
 * Scenario is chosen with MOCK_SCENARIO. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/iokitholder_abi.h"

#define MAXN 1400
#define MAXK 80
struct node { const char *cls; int own; int fail_busy; int flip; int reads; int nkids; int kids[MAXK]; int nparents; int parents[4]; int fail_class, child_scans, fail_second_scan, expand_second_scan; };
static struct node N[MAXN];
static char long_classes[6][128];
static int nn;
/* object handles: 1..MAXN nodes, iterators above 100000 */
struct iter { int used, valid, rec, root, pos, n, list[MAXN]; };
static struct iter IT[64];
static long live_refs;

static int add(const char *cls, int own) { int i = ++nn; N[i].cls = cls; N[i].own = own; return i; }
static void attach(int child, int parent) {
    N[parent].kids[N[parent].nkids++] = child;
    N[child].parents[N[child].nparents++] = parent;
}
static int busy_of(int i) {
    int b = N[i].own;
    for (int k = 0; k < N[i].nkids; k++) {
        int c = N[i].kids[k];
        if (N[c].parents[0] == i && busy_of(c) > 0) b++;
    }
    return b;
}

static void build(const char *s) {
    int root = add("IOPlatformExpertDevice", 0);
    int pe = add("ArmDTPlatform", 0);
    attach(pe, root);
    int ufs = add("NativeUfs", 0); attach(ufs, root);
    int nvram = add("IODTNVRAM", 0); attach(nvram, pe);
    int pm = add("IOPMrootDomain", 0); attach(pm, pe);
    for (int c = 0; c < 8; c++) { int cpu = add("IOPlatformDevice", 0); attach(cpu, pe); int d = add("ArmCpu", 0); attach(d, cpu); }
    int defaults = add("IOPlatformDevice", 0); attach(defaults, pe);
    int stack = add("IONetworkStack", 0); attach(stack, pe);
    int ctl = add("IO80211Controller", 0); attach(ctl, pe);
    int wl = add("IO80211Interface", 0); attach(wl, ctl);
    if (!strcmp(s, "k372")) {
        /* 372 shape: the /defaults nub's matching never finished: AppleImage4 attached during start, nub holds busy itself */
        N[defaults].own = 1;
        int img4 = add("AppleImage4", 0); attach(img4, defaults);
    } else if (!strcmp(s, "quiet")) {
        /* nothing busy */
    } else if (!strcmp(s, "nested")) {
        /* holder two levels down: a K70-style HID service under a nub, whose own matching is stuck (child driver attached) */
        int nub = add("IOPlatformDevice", 0); attach(nub, pe);
        int hid = add("NativeHidInput", 1); attach(hid, nub);
        int drv = add("IOHIDEventDriver", 0); attach(drv, hid);
    } else if (!strcmp(s, "multiparent")) {
        /* busy child whose provider is A but also attached under B: only A may count it */
        int a = add("IOPlatformDevice", 0); attach(a, pe);
        int b = add("IOPlatformDevice", 0); attach(b, pe);
        int c = add("IOService", 1); attach(c, a); attach(c, b);
    } else if (!strcmp(s, "race")) {
        N[defaults].own = 1; N[defaults].flip = 1;
    } else if (!strcmp(s, "readfail")) {
        N[defaults].own = 1; N[nvram].fail_busy = 1;
    } else if (!strcmp(s, "many")) {
        for (int k = 0; k < 14; k++) { int h = add("IOPlatformDevice", 1); attach(h, pe); }
    } else if (!strcmp(s, "badname")) {
        int h = add("Bad Name!", 1); attach(h, pe);
    } else if (!strcmp(s, "classfail")) {
        N[defaults].own = 1; N[defaults].fail_class = 1;
    } else if (!strcmp(s, "parentclassfail")) {
        N[defaults].own = 1; N[pe].fail_class = 1;
    } else if (!strcmp(s, "childclassfail")) {
        N[defaults].own = 1;
        int c = add("AppleImage4", 0); attach(c, defaults); N[c].fail_class = 1;
    } else if (!strcmp(s, "secondscanfail")) {
        N[defaults].own = 1; N[defaults].fail_second_scan = 1;
        int c = add("AppleImage4", 0); attach(c, defaults);
    } else if (!strcmp(s, "secondscanexpand")) {
        N[defaults].own = 1; N[defaults].expand_second_scan = 65;
        for (int i = 0; i < 65; ++i) { int c = add("AppleImage4", 0); attach(c, defaults); }
        N[defaults].nkids = 1;
    } else if (!strcmp(s, "classoverflow")) {
        N[defaults].own = 1;
        const char *names[] = {"DriverA", "DriverB", "DriverC", "DriverD", "DriverE"};
        for (int i = 0; i < 5; ++i) { int c = add(names[i], 0); attach(c, defaults); }
    } else if (!strcmp(s, "longclasses")) {
        N[defaults].own = 1;
        for (int i = 0; i < 6; ++i) { memset(long_classes[i], 'A' + i, 127); long_classes[i][127] = 0; }
        N[defaults].cls = long_classes[0]; N[pe].cls = long_classes[1];
        for (int i = 2; i < 6; ++i) { int c = add(long_classes[i], 0); attach(c, defaults); }
    } else if (!strcmp(s, "huge")) {
        N[defaults].own = 1;
        /* a 1200-entry chain exceeds the 1024-entry walk cap without exceeding per-node child limits */
        int parent = ufs;
        while (nn < 1200) { int x = add("IOService", 0); attach(x, parent); parent = x; }
    } else { fprintf(stderr, "unknown MOCK_SCENARIO %s\n", s); exit(3); }
}

static void init_once(void) {
    if (nn) return;
    const char *s = getenv("MOCK_SCENARIO");
    build(s ? s : "k372");
}
static int node_of(io_object_t o) { return (o >= 1 && (int)o <= nn) ? (int)o : 0; }
static struct iter *iter_of(io_object_t o) { return (o >= 100000 && o < 100064 && IT[o - 100000].used) ? &IT[o - 100000] : NULL; }

static void preorder(struct iter *t, int i) {
    for (int k = 0; k < N[i].nkids; k++) {
        int c = N[i].kids[k];
        int dup = 0; /* XNU IORegistryIterator keeps a "done" set: a multi-parent entry is returned once */
        for (int j = 0; j < t->n; j++) if (t->list[j] == c) { dup = 1; break; }
        if (dup) continue;
        if (t->n < MAXN) t->list[t->n++] = c;
        preorder(t, c);
    }
}
static io_iterator_t new_iter(int root, int rec) {
    for (int k = 0; k < 64; k++)
        if (!IT[k].used) {
            struct iter *t = &IT[k];
            memset(t, 0, sizeof *t);
            t->used = 1; t->valid = 1; t->rec = rec; t->root = root;
            if (rec) preorder(t, root);
            else for (int j = 0; j < N[root].nkids; j++) t->list[t->n++] = N[root].kids[j];
            live_refs++;
            return (io_iterator_t)(100000 + k);
        }
    fprintf(stderr, "mock: out of iterators\n"); exit(4);
}

io_registry_entry_t IORegistryEntryFromPath(mach_port_t m, const io_string_t path) {
    (void)m; init_once();
    if (strcmp(path, "IOService:/")) return 0;
    live_refs++;
    return 1;
}
kern_return_t IORegistryEntryCreateIterator(io_registry_entry_t e, const io_name_t plane, ih_option_bits o, io_iterator_t *it) {
    init_once();
    if (!node_of(e) || strcmp(plane, "IOService") || o != IH_ITERATE_RECURSIVELY) return 5;
    *it = new_iter(node_of(e), 1);
    return KERN_SUCCESS;
}
kern_return_t IORegistryEntryGetChildIterator(io_registry_entry_t e, const io_name_t plane, io_iterator_t *it) {
    if (!node_of(e) || strcmp(plane, "IOService")) return 5;
    struct node *n = &N[node_of(e)]; n->child_scans++;
    if (n->child_scans == 2 && n->fail_second_scan) return 5;
    if (n->child_scans == 2 && n->expand_second_scan) n->nkids = n->expand_second_scan;
    *it = new_iter(node_of(e), 0);
    return KERN_SUCCESS;
}
kern_return_t IORegistryEntryGetParentEntry(io_registry_entry_t e, const io_name_t plane, io_registry_entry_t *p) {
    int i = node_of(e);
    if (!i || strcmp(plane, "IOService") || !N[i].nparents) return 5;
    *p = (io_registry_entry_t)N[i].parents[0];
    live_refs++;
    return KERN_SUCCESS;
}
io_object_t IOIteratorNext(io_iterator_t it) {
    struct iter *t = iter_of(it);
    if (!t || t->pos >= t->n) return 0;
    live_refs++;
    return (io_object_t)t->list[t->pos++];
}
boolean_t IOIteratorIsValid(io_iterator_t it) { struct iter *t = iter_of(it); return t && t->valid; }
boolean_t IOObjectIsEqualTo(io_object_t a, io_object_t b) { return a == b; }
kern_return_t IOObjectRelease(io_object_t o) {
    struct iter *t = iter_of(o);
    if (t) t->used = 0;
    else if (!node_of(o)) { fprintf(stderr, "mock: release of bad object %u\n", o); exit(6); }
    live_refs--;
    return KERN_SUCCESS;
}
kern_return_t IOServiceGetBusyState(io_service_t s, uint32_t *b) {
    int i = node_of(s);
    if (!i || N[i].fail_busy) return 0x2c2; /* kIOReturnBadArgument-like */
    if (N[i].flip) N[i].own = 1 + (N[i].reads++ % 2); /* a concurrently changing busy count (race) */
    *b = (uint32_t)busy_of(i);
    return KERN_SUCCESS;
}
kern_return_t IOObjectGetClass(io_object_t o, io_name_t cls) {
    int i = node_of(o);
    if (!i || N[i].fail_class) return 5;
    snprintf(cls, 128, "%s", N[i].cls);
    return KERN_SUCCESS;
}

__attribute__((destructor)) static void report_leaks(void) {
    if (getenv("MOCK_SCENARIO")) fprintf(stderr, "MOCK live_refs=%ld\n", live_refs);
}
