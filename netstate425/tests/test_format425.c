/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Portable test of netstate425_out.h only: argument bounds, row schema/charset/length, row cap, console-sink rules.
 * Does NOT link or imitate CoreFoundation/SystemConfiguration; says nothing about native behaviour or 20H392 exports. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../src/netstate425_out.h"
#include <stdlib.h>
#include <limits.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int args(const char *a, const char *b, const char *c, const char *d, struct ns425_opts *o)
{
    char *v[6] = {(char *)"t", (char *)a, (char *)b, (char *)c, (char *)d, NULL};
    int n = 1; while (n < 5 && v[n]) n++;
    return ns425_parse_args(n, v, o);
}

/* allowed row bytes: [A-Za-z0-9_=.- ] and the final newline; must start with the fixed label */
static int row_ok(const char *s, size_t n)
{
    if (n < 13 || n >= NS425_LINE_MAX || s[n - 1] != '\n' || memcmp(s, "NETSTATE425 ", 12)) return 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '=' || c == '.' || c == '-' || c == ' ')) return 0;
    }
    return 1;
}

static char cap[65536]; static size_t capn;
static void drain(int fd) { ssize_t r; while ((r = read(fd, cap + capn, sizeof cap - 1 - capn)) > 0) capn += (size_t)r; cap[capn] = 0; }

int main(void)
{
    struct ns425_opts o;
    /* 1. arguments */
    CHECK(args(NULL, NULL, NULL, NULL, &o) == 0 && o.delay_s == 0 && o.frames == 12 && !o.console);
    CHECK(args("-d", "600", "-n", "12", &o) == 0 && o.delay_s == 600 && o.frames == 12);
    CHECK(args("-d", "0", "-c", NULL, &o) == 0 && o.console == 1);
    CHECK(args("-n", "1", NULL, NULL, &o) == 0 && o.frames == 1);
    const char *bad[][4] = {{"-d", "601"}, {"-d", "-1"}, {"-d", "1x"}, {"-d", ""}, {"-d", "0600"}, {"-n", "0"}, {"-n", "13"},
                            {"-x"}, {"-d"}, {"-c", "-c"}, {"-d", "1", "-d", "2"}, {"-n", "2", "-n", "3"}, {"12"}, {"-d", "+5"}};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) CHECK(args(bad[i][0], bad[i][1], bad[i][2], bad[i][3], &o) == 64);

    /* 2. rows: extreme values through every formatter */
    int p[2]; CHECK(pipe(p) == 0); fcntl(p[0], F_SETFL, O_NONBLOCK); fcntl(p[1], F_SETFL, O_NONBLOCK);
    ns425_stdout_fd = p[1];
    o.delay_s = 600; o.frames = 12; o.console = 1;
    ns425_row_start(&o, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, 1, INT_MAX);
    ns425_row_frame(11, 1, LLONG_MAX, LLONG_MIN, 0, INT_MIN);
    const int ext[] = {INT_MIN, -3, -2, -1, 0, 1, 2, 999, 1000, INT_MAX};
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; ++i) {
        int x = ext[i]; struct ns425_key k = {x, x, x};
        struct ns425_gv4 g = {k, x, x, x, x}; struct ns425_dns d = {k, x, x}; struct ns425_link l = {k, x};
        struct ns425_sv4 s = {k, x, x, x, x, x}; struct ns425_if4 f4 = {k, x}; struct ns425_namer nm = {k, x, x, x};
        struct ns425_proxy pr = {k, x, x, x}; struct ns425_ifa fa = {x, x, x, x, x, x};
        ns425_row_gv4(0, &g); ns425_row_dns(0, 1, &d); ns425_row_dns(0, 0, &d); ns425_row_link(0, &l); ns425_row_sv4(0, &s);
        ns425_row_if4(0, &f4); ns425_row_namer(0, &nm); ns425_row_proxy(0, &pr); ns425_row_ifa(0, &fa);
        drain(p[0]);
    }
    ns425_row_result(1, 12, 0); ns425_row_result(0, 3, 1); ns425_row_usage();
    drain(p[0]);
    unsigned rows = 0; size_t maxlen = 0;
    for (char *s = cap; *s;) {
        char *e = strchr(s, '\n'); CHECK(e != NULL); if (!e) break;
        size_t n = (size_t)(e - s) + 1; CHECK(row_ok(s, n)); if (n > maxlen) maxlen = n;
        if (!row_ok(s, n)) fprintf(stderr, "bad row: %.*s", (int)n, s);
        rows++; s = e + 1;
    }
    CHECK(rows == 2 + 10 * 9 + 3);
    CHECK(strstr(cap, "present=1 type=9 err=-1 primary_if_en0=-2") != NULL);   /* INT_MIN clamps */
    CHECK(strstr(cap, "servers=999") != NULL && strstr(cap, "servers=1000") == NULL);
    CHECK(strstr(cap, "active=1") && strstr(cap, "active=2") == NULL);
    CHECK(strstr(cap, "err=99999") != NULL);
    CHECK(strstr(cap, "start source=425 label=425 version=425.1 ") != NULL);
    CHECK(strstr(cap, "nwi_probe=0 nwi=unknown") != NULL);
    CHECK(row_ok(ns425_watchdog_row, sizeof ns425_watchdog_row - 1));
    printf("rows=%u max_row_bytes=%zu (limit %d)\n", rows, maxlen, NS425_LINE_MAX);

    /* 3. row cap: never more than NS425_MAX_ROWS rows in total */
    capn = 0; cap[0] = 0;
    for (int i = 0; i < 400; ++i) { ns425_emit("cap n=%d", i); drain(p[0]); }
    unsigned c = 0; for (char *s = cap; (s = strchr(s, '\n')); ++s) c++;
    CHECK(ns425_rows == NS425_MAX_ROWS);
    CHECK(c == NS425_MAX_ROWS - rows);
    printf("cap_rows_total=%u\n", ns425_rows);

    /* 4. nonblocking stdout: full pipe -> write fails EAGAIN, emit returns, errno preserved */
    {
        int q[2]; CHECK(pipe(q) == 0); fcntl(q[1], F_SETFL, O_NONBLOCK);
        char junk[4096]; memset(junk, 'x', sizeof junk); while (write(q[1], junk, sizeof junk) > 0) {}
        ns425_rows = 0; ns425_stdout_fd = q[1]; errno = 1234; ns425_emit("blocked n=%d", 1);
        CHECK(errno == 1234); CHECK(ns425_rows == 1);
        close(q[0]); close(q[1]);
    }

    /* 5. console sink rules */
    char dir[] = "/tmp/ns425XXXXXX"; CHECK(mkdtemp(dir) != NULL);
    char reg[64], lnk[64], fifo[64];
    snprintf(reg, sizeof reg, "%s/reg", dir); snprintf(lnk, sizeof lnk, "%s/lnk", dir); snprintf(fifo, sizeof fifo, "%s/fifo", dir);
    int fd = open(reg, O_CREAT | O_WRONLY, 0600); CHECK(fd >= 0); close(fd);
    CHECK(symlink("/dev/null", lnk) == 0); CHECK(mkfifo(fifo, 0600) == 0);
    int rfd = open(fifo, O_RDONLY | O_NONBLOCK);
    errno = 0; CHECK(ns425_console_enable(reg) == -1 && errno == ENODEV && ns425_console_fd < 0);
    errno = 0; CHECK(ns425_console_enable(lnk) == -1 && errno == ELOOP && ns425_console_fd < 0);
    errno = 0; CHECK(ns425_console_enable(fifo) == -1 && errno == ENODEV && ns425_console_fd < 0);
    CHECK(ns425_console_enable("/dev/null") == 0 && ns425_console_fd >= 0);
    CHECK((fcntl(ns425_console_fd, F_GETFL) & O_NONBLOCK) && (fcntl(ns425_console_fd, F_GETFD) & FD_CLOEXEC));
    close(rfd); unlink(reg); unlink(lnk); unlink(fifo); rmdir(dir);
    printf("console: regular=ENODEV symlink=ELOOP fifo=ENODEV chardev=accepted(nonblock,cloexec)\n");

    printf("%s fails=%d\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
