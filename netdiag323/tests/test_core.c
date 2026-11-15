// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host tests of netdiag_core.c with synthetic fixtures (no captured traffic, no network data from any device).
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/netdiag_core.h"

static unsigned long checks;
#define CHECK(x) do {if (!(x)) {fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1);} ++checks;} while (0)
static const uint8_t MAC[6] = {0x02, 0x00, 0x5e, 0x10, 0x20, 0x30};

/* a server reply built independently of the client encoder */
static size_t reply(uint8_t *b, int type, uint32_t xid, const char *yi, int with_server, int with_mask, int overload_dns)
{
    memset(b, 0, 600);
    b[0] = 2; b[1] = 1; b[2] = 6;
    b[4] = (uint8_t)(xid >> 24); b[5] = (uint8_t)(xid >> 16); b[6] = (uint8_t)(xid >> 8); b[7] = (uint8_t)xid;
    inet_pton(AF_INET, yi, b + 16);
    memcpy(b + 28, MAC, 6);
    b[236] = 0x63; b[237] = 0x82; b[238] = 0x53; b[239] = 0x63;
    size_t at = 240;
    b[at++] = 53; b[at++] = 1; b[at++] = (uint8_t)type;
    if (with_server) {b[at++] = 54; b[at++] = 4; inet_pton(AF_INET, "192.0.2.1", b + at); at += 4;}
    if (with_mask) {b[at++] = 1; b[at++] = 4; inet_pton(AF_INET, "255.255.255.0", b + at); at += 4;}
    b[at++] = 3; b[at++] = 8; inet_pton(AF_INET, "192.0.2.1", b + at); inet_pton(AF_INET, "192.0.2.9", b + at + 4); at += 8;
    if (overload_dns) {
        b[at++] = 52; b[at++] = 1; b[at++] = 1;  /* the file field holds more options */
        size_t f = 108; b[f++] = 6; b[f++] = 4; inet_pton(AF_INET, "198.51.100.53", b + f); f += 4; b[f] = 255;
    } else {
        b[at++] = 6; b[at++] = 12; inet_pton(AF_INET, "198.51.100.53", b + at); inet_pton(AF_INET, "198.51.100.54", b + at + 4);
        inet_pton(AF_INET, "198.51.100.55", b + at + 8); at += 12;
    }
    b[at++] = 51; b[at++] = 4; b[at++] = 0; b[at++] = 0; b[at++] = 0x0e; b[at++] = 0x10;  /* 3600 s */
    b[at++] = 58; b[at++] = 4; b[at++] = 0; b[at++] = 0; b[at++] = 0x07; b[at++] = 0x08;  /* 1800 s */
    b[at++] = 26; b[at++] = 2; b[at++] = 0x05; b[at++] = 0xdc;
    b[at++] = 255;
    return at;
}

static void test_dhcp(void)
{
    uint8_t m[600]; NdLease l; char s[32];
    /* client messages */
    size_t n = nd_dhcp_build(m, sizeof m, ND_DHCPDISCOVER, 0x11223344u, MAC, NULL, NULL, 7);
    CHECK(n == 300 && m[0] == 1 && m[1] == 1 && m[2] == 6 && m[4] == 0x11 && m[7] == 0x44 && m[8] == 0 && m[9] == 7);
    CHECK(m[10] == 0x80 && m[11] == 0 && !memcmp(m + 28, MAC, 6) && m[236] == 0x63 && m[239] == 0x63);
    CHECK(m[240] == 53 && m[241] == 1 && m[242] == ND_DHCPDISCOVER && m[243] == 61 && m[244] == 7 && m[245] == 1 && !memcmp(m + 246, MAC, 6));
    CHECK(!memchr(m + 240, 50, 0) && m[16] == 0);  /* no address yet */
    struct in_addr req, srv; inet_pton(AF_INET, "192.0.2.77", &req); inet_pton(AF_INET, "192.0.2.1", &srv);
    n = nd_dhcp_build(m, sizeof m, ND_DHCPREQUEST, 5, MAC, &req, &srv, 0);
    CHECK(n == 300 && m[242] == ND_DHCPREQUEST && m[252] == 50 && m[253] == 4 && !memcmp(m + 254, &req.s_addr, 4) && m[258] == 54 && !memcmp(m + 260, &srv.s_addr, 4));
    n = nd_dhcp_build(m, sizeof m, ND_DHCPREQUEST, 5, MAC, &req, NULL, 0);
    CHECK(m[252] == 50 && m[258] == 57);  /* renewal: no server identifier */
    CHECK(!nd_dhcp_build(m, 299, ND_DHCPDISCOVER, 1, MAC, NULL, NULL, 0) && !nd_dhcp_build(m, sizeof m, ND_DHCPOFFER, 1, MAC, NULL, NULL, 0));
    CHECK(!nd_dhcp_build(NULL, 600, 1, 1, MAC, NULL, NULL, 0) && !nd_dhcp_build(m, 600, 1, 1, NULL, NULL, NULL, 0));
    /* server replies */
    n = reply(m, ND_DHCPOFFER, 0xabcdef01u, "192.0.2.77", 1, 1, 0);
    CHECK(nd_dhcp_parse(m, n, 0xabcdef01u, MAC, &l) == ND_DHCPOFFER);
    inet_ntop(AF_INET, &l.yiaddr, s, 32); CHECK(!strcmp(s, "192.0.2.77"));
    inet_ntop(AF_INET, &l.router, s, 32); CHECK(!strcmp(s, "192.0.2.1"));
    inet_ntop(AF_INET, &l.mask, s, 32); CHECK(!strcmp(s, "255.255.255.0"));
    CHECK(l.dns_count == 2 && l.lease_s == 3600 && l.t1_s == 1800 && l.mtu == 1500);
    inet_ntop(AF_INET, &l.dns[1], s, 32); CHECK(!strcmp(s, "198.51.100.54"));
    n = reply(m, ND_DHCPACK, 9, "192.0.2.77", 1, 1, 1);
    CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == ND_DHCPACK && l.dns_count == 1);   /* option overload: DNS from the file field */
    CHECK(nd_dhcp_parse(m, n, 10, MAC, &l) == 0);                                /* another transaction */
    uint8_t other[6] = {2, 0, 0, 0, 0, 1};
    CHECK(nd_dhcp_parse(m, n, 9, other, &l) == 0);                               /* another client */
    m[0] = 1; CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == 0); m[0] = 2;            /* a request, not a reply */
    m[236] = 0; CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == 0); m[236] = 0x63;
    n = reply(m, ND_DHCPOFFER, 9, "192.0.2.77", 0, 1, 0); CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == 0);  /* no server id */
    n = reply(m, ND_DHCPOFFER, 9, "0.0.0.0", 1, 1, 0); CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == 0);
    n = reply(m, ND_DHCPOFFER, 9, "224.0.0.1", 1, 1, 0); CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == 0);
    n = reply(m, ND_DHCPOFFER, 9, "127.0.0.5", 1, 1, 0); CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == 0);
    n = reply(m, ND_DHCPOFFER, 9, "10.1.2.3", 1, 0, 0);                         /* no mask: class A default; router then off-link */
    CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == ND_DHCPOFFER && l.mask.s_addr == htonl(0xff000000u) && l.router.s_addr == 0);
    n = reply(m, ND_DHCPNAK, 9, "0.0.0.0", 1, 0, 0); CHECK(nd_dhcp_parse(m, n, 9, MAC, &l) == ND_DHCPNAK);
    /* truncation anywhere never reads past the end or yields a reply */
    n = reply(m, ND_DHCPACK, 9, "192.0.2.77", 1, 1, 0);
    for (size_t cut = 0; cut < 249; ++cut) CHECK(nd_dhcp_parse(m, cut, 9, MAC, &l) == 0);  /* until the server id is complete */
    uint8_t *heap = malloc(n);
    for (size_t cut = 250; cut < n; ++cut) {memcpy(heap, m, cut); (void)nd_dhcp_parse(heap, cut, 9, MAC, &l);}
    /* random corruption of the option area */
    uint32_t seed = 12345;
    for (int round = 0; round < 20000; ++round) {
        n = reply(m, ND_DHCPACK, 9, "192.0.2.77", 1, 1, round & 1);
        for (int k = 0; k < 4; ++k) {seed = seed * 1664525u + 1013904223u; m[240 + (seed >> 8) % (n - 240)] = (uint8_t)(seed >> 20);}
        heap = realloc(heap, n); memcpy(heap, m, n);
        const int t = nd_dhcp_parse(heap, n, 9, MAC, &l);
        CHECK(t == 0 || t == ND_DHCPOFFER || t == ND_DHCPACK || t == ND_DHCPNAK);
        if (t == ND_DHCPACK || t == ND_DHCPOFFER) CHECK(l.dns_count <= 2 && l.server.s_addr && l.yiaddr.s_addr);
    }
    free(heap);
}

static void test_frame(void)
{
    uint8_t p[300] = {1, 2, 3}, f[400];
    struct in_addr src = {0}, dst = {htonl(0xffffffffu)};
    size_t n = nd_udp_ip_frame(f, sizeof f, src, dst, 68, 67, p, 300, 0);
    CHECK(n == 328 && f[0] == 0x45 && f[9] == 17 && f[8] == 64 && f[2] == 0x01 && f[3] == 0x48);
    CHECK(nd_checksum(f, 20, 0) == 0);                                  /* header checksum verifies */
    CHECK(f[20] == 0 && f[21] == 68 && f[22] == 0 && f[23] == 67 && f[24] == 0x01 && f[25] == 0x34 && !memcmp(f + 28, p, 300));
    uint32_t seed = 17 + 308;
    for (int i = 12; i < 20; i += 2) seed += (uint32_t)(f[i] << 8 | f[i + 1]);
    CHECK(nd_checksum(f + 20, 308, seed) == 0);                         /* UDP checksum verifies over the pseudo header */
    n = nd_udp_ip_frame(f, sizeof f, src, dst, 68, 67, p, 300, 1);
    uint16_t host; memcpy(&host, f + 2, 2); CHECK(n == 328 && host == 328);
    CHECK(!nd_udp_ip_frame(f, 100, src, dst, 68, 67, p, 300, 0) && !nd_udp_ip_frame(NULL, 400, src, dst, 1, 2, p, 1, 0));
}

static void test_dns(void)
{
    uint8_t q[512], a[512]; struct in_addr ip; char s[32];
    size_t n = nd_dns_query(q, sizeof q, 0x1234, "captive.apple.com");
    CHECK(n == 12 + 19 + 4 && q[0] == 0x12 && q[1] == 0x34 && q[2] == 1 && q[5] == 1 && q[12] == 7 && !memcmp(q + 13, "captive", 7) && q[20] == 5);
    CHECK(q[30] == 0 && q[31] == 0 && q[32] == 1 && q[34] == 1);
    CHECK(!nd_dns_query(q, sizeof q, 1, "") && !nd_dns_query(q, sizeof q, 1, "a..b") && !nd_dns_query(q, 20, 1, "captive.apple.com"));
    char longlabel[80]; memset(longlabel, 'x', 64); longlabel[64] = 0; CHECK(!nd_dns_query(q, sizeof q, 1, longlabel));
    /* a response: the query echoed, a CNAME, then an A record via a compressed name (q rebuilt: the refused calls wrote into it) */
    n = nd_dns_query(q, sizeof q, 0x1234, "captive.apple.com");
    memcpy(a, q, n); a[2] = 0x81; a[3] = 0x80; a[7] = 2;
    size_t at = n;
    a[at++] = 0xc0; a[at++] = 12; a[at++] = 0; a[at++] = 5; a[at++] = 0; a[at++] = 1; memset(a + at, 0, 4); at += 4;
    a[at++] = 0; a[at++] = 4; a[at++] = 3; a[at++] = 'c'; a[at++] = 'd'; a[at++] = 'n';   /* rdata: a short name, not followed */
    a[at++] = 0xc0; a[at++] = 12; a[at++] = 0; a[at++] = 1; a[at++] = 0; a[at++] = 1; memset(a + at, 0, 4); at += 4;
    a[at++] = 0; a[at++] = 4; a[at++] = 203; a[at++] = 0; a[at++] = 113; a[at++] = 7;
    CHECK(nd_dns_answer(a, at, 0x1234, &ip) == 1); inet_ntop(AF_INET, &ip, s, 32); CHECK(!strcmp(s, "203.0.113.7"));
    CHECK(nd_dns_answer(a, at, 0x1235, &ip) == -2);
    a[3] = 0x83; CHECK(nd_dns_answer(a, at, 0x1234, &ip) == -3); a[3] = 0x80;              /* NXDOMAIN */
    a[2] = 0x01; CHECK(nd_dns_answer(a, at, 0x1234, &ip) == -2); a[2] = 0x81;              /* not a response */
    for (size_t cut = 0; cut < at; ++cut) CHECK(nd_dns_answer(a, cut, 0x1234, &ip) <= 0);  /* truncated: never an answer */
    a[7] = 1; CHECK(nd_dns_answer(a, at, 0x1234, &ip) == 0);                                 /* only the CNAME */
    uint8_t loop[64] = {0x12, 0x34, 0x81, 0x80, 0, 1, 0, 0}; loop[12] = 0x3f;                 /* label running off the end */
    CHECK(nd_dns_answer(loop, 20, 0x1234, &ip) == -1);
}

static void test_http(void)
{
    NdHttp h;
    const char ok[] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    CHECK(nd_http_parse(ok, sizeof ok - 1, &h) && h.status == 200 && h.apple_success);
    const char portal[] = "HTTP/1.1 200 OK\r\n\r\n<html>login</html>";
    CHECK(nd_http_parse(portal, sizeof portal - 1, &h) && h.status == 200 && !h.apple_success);
    const char moved[] = "HTTP/1.1 302 Found\r\nLocation: http://portal/\r\n\r\n";
    CHECK(nd_http_parse(moved, sizeof moved - 1, &h) && h.status == 302);
    CHECK(!nd_http_parse("HTTP/2 200", 10, &h) && !nd_http_parse("HTTP/1.1 2x0 OK", 15, &h) && !nd_http_parse("garbage here", 12, &h));
    CHECK(!nd_http_parse("HTTP/1.1 600 X", 14, &h) && !nd_http_parse(ok, 11, &h) && !nd_http_parse(NULL, 50, &h));
}

int main(void)
{
    test_dhcp();
    test_frame();
    test_dns();
    test_http();
    printf("test_core: %lu checks passed\n", checks);
    return 0;
}
