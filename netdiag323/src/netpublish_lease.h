/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The lease file unixnu-netdiag writes ("if=en0\nip=..\nmask=..\nrouter=..\ndns=a,b\nlease=N\n") and its strict parser. */
#ifndef NETPUBLISH_LEASE_H
#define NETPUBLISH_LEASE_H
#include <arpa/inet.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char ifname[16], ip[16], mask[16], router[16], dns[2][16]; unsigned dns_count; unsigned long lease;} NpLease;

static int np_ipv4(const char *s, size_t n, char out[16])
{
    if (!n || n > 15) return -1;
    char t[16]; memcpy(t, s, n); t[n] = 0;
    struct in_addr a; if (inet_pton(AF_INET, t, &a) != 1) return -1;
    memcpy(out, t, n + 1); return 0;
}
/* 0 accepted. Requires if, ip, mask; a usable unicast ip; a contiguous mask; router (if any) and dns (0..2) valid IPv4. */
static int np_lease_parse(const char *text, size_t len, NpLease *l)
{
    memset(l, 0, sizeof *l);
    int have_if = 0, have_ip = 0, have_mask = 0;
    const char *p = text, *end = text + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p)); if (!nl) return -1;
        const char *eq = memchr(p, '=', (size_t)(nl - p)); if (!eq) return -1;
        const size_t k = (size_t)(eq - p), vn = (size_t)(nl - eq - 1); const char *v = eq + 1;
        if (k == 2 && !memcmp(p, "if", 2)) {if (!vn || vn > 15) return -1; memcpy(l->ifname, v, vn); have_if = 1;}
        else if (k == 2 && !memcmp(p, "ip", 2)) {if (np_ipv4(v, vn, l->ip)) return -1; have_ip = 1;}
        else if (k == 4 && !memcmp(p, "mask", 4)) {if (np_ipv4(v, vn, l->mask)) return -1; have_mask = 1;}
        else if (k == 6 && !memcmp(p, "router", 6)) {if (vn && np_ipv4(v, vn, l->router)) return -1;}
        else if (k == 3 && !memcmp(p, "dns", 3)) {
            const char *q = v, *ve = v + vn;
            while (q < ve) {
                const char *c = memchr(q, ',', (size_t)(ve - q)); const char *e = c ? c : ve;
                if (c && c + 1 == ve) return -1;  /* a trailing comma */
                if (l->dns_count == 2 || np_ipv4(q, (size_t)(e - q), l->dns[l->dns_count])) return -1;
                l->dns_count++; q = c ? c + 1 : ve;
            }
        } else if (k == 5 && !memcmp(p, "lease", 5)) {char t[16]; if (!vn || vn > 10) return -1; memcpy(t, v, vn); t[vn] = 0; l->lease = strtoul(t, NULL, 10);}
        else return -1;
        p = nl + 1;
    }
    if (!have_if || !have_ip || !have_mask) return -1;
    struct in_addr a, m; inet_pton(AF_INET, l->ip, &a); inet_pton(AF_INET, l->mask, &m);
    const unsigned long ah = ntohl(a.s_addr), mh = ntohl(m.s_addr);
    if (!ah || ah == 0xffffffffUL || (ah >> 28) == 0xe || (ah >> 24) == 127 || !mh || ((mh | (mh - 1)) & 0xffffffffUL) != 0xffffffffUL) return -1;
    return 0;
}
#endif
