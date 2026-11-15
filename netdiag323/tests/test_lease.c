// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/netpublish_lease.h"
static unsigned long checks;
#define CHECK(x) do {if (!(x)) {fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1);} ++checks;} while (0)
static int parse(const char *s, NpLease *l) {return np_lease_parse(s, strlen(s), l);}
int main(void)
{
    NpLease l;
    CHECK(!parse("if=en0\nip=192.0.2.77\nmask=255.255.255.0\nrouter=192.0.2.1\ndns=198.51.100.53,198.51.100.54\nlease=3600\n", &l));
    CHECK(!strcmp(l.ifname, "en0") && !strcmp(l.ip, "192.0.2.77") && !strcmp(l.router, "192.0.2.1") && l.dns_count == 2 && !strcmp(l.dns[1], "198.51.100.54") && l.lease == 3600);
    CHECK(!parse("if=en0\nip=10.0.0.2\nmask=255.0.0.0\nrouter=\ndns=\nlease=0\n", &l) && !l.router[0] && l.dns_count == 0);
    static const char *bad[] = {"", "if=en0\nip=10.0.0.2\n", "if=en0\nip=10.0.0.2\nmask=255.0.0.0", "if=en0\nip=10.0.0.256\nmask=255.0.0.0\n",
        "if=en0\nip=0.0.0.0\nmask=255.0.0.0\n", "if=en0\nip=224.1.1.1\nmask=255.0.0.0\n", "if=en0\nip=127.0.0.2\nmask=255.0.0.0\n",
        "if=en0\nip=10.0.0.2\nmask=255.0.255.0\n", "if=en0\nip=10.0.0.2\nmask=0.0.0.0\n", "if=en0\nip=10.0.0.2\nmask=255.0.0.0\ndns=1.1.1.1,2.2.2.2,3.3.3.3\n",
        "if=en0\nip=10.0.0.2\nmask=255.0.0.0\nrouter=x\n", "if=en0\nip=10.0.0.2\nmask=255.0.0.0\nevil=1\n", "ip=10.0.0.2\nmask=255.0.0.0\n",
        "if=en0\nip=10.0.0.2 \nmask=255.0.0.0\n", "if=en0123456789abcdef\nip=10.0.0.2\nmask=255.0.0.0\n", "if=en0\nip=10.0.0.2\nmask=255.0.0.0\ndns=1.1.1.1,\n"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) CHECK(parse(bad[i], &l) != 0);
    char buf[200]; const char *good = "if=en0\nip=192.0.2.77\nmask=255.255.255.0\nrouter=192.0.2.1\ndns=198.51.100.53\nlease=60\n";
    for (size_t cut = 0; cut < strlen(good); ++cut) {memcpy(buf, good, cut); char *h = malloc(cut + 1); memcpy(h, buf, cut); (void)np_lease_parse(h, cut, &l); free(h);}
    uint32_t seed = 7;
    for (int r = 0; r < 20000; ++r) {strcpy(buf, good); size_t n = strlen(buf); for (int k = 0; k < 3; ++k) {seed = seed * 1664525u + 1013904223u; buf[(seed >> 8) % n] = (char)(seed >> 24);}
        char *h = malloc(n); memcpy(h, buf, n); if (!np_lease_parse(h, n, &l)) CHECK(l.ip[0] && l.mask[0] && l.dns_count <= 2); free(h);}
    printf("test_lease: %lu checks passed\n", checks);
    return 0;
}
