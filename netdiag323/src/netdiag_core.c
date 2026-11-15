/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "netdiag_core.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) {p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;}
static void put32(uint8_t *p, uint32_t v) {p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;}
static uint16_t get16(const uint8_t *p) {return (uint16_t)(p[0] << 8 | p[1]);}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];}

#define MAGIC 0x63825363u
#define FIXED 236u

size_t nd_dhcp_build(uint8_t *out, size_t cap, int type, uint32_t xid, const uint8_t mac[6], const struct in_addr *request,
                     const struct in_addr *server, uint16_t secs)
{
    if (!out || !mac || cap < 300 || (type != ND_DHCPDISCOVER && type != ND_DHCPREQUEST)) return 0;
    memset(out, 0, cap < ND_DHCP_MAX ? cap : ND_DHCP_MAX);
    out[0] = 1; out[1] = 1; out[2] = 6;           /* BOOTREQUEST, Ethernet, 6-byte address */
    put32(out + 4, xid); put16(out + 8, secs);
    put16(out + 10, 0x8000);                       /* broadcast: we have no address to receive a unicast reply */
    memcpy(out + 28, mac, 6);
    put32(out + FIXED, MAGIC);
    size_t at = FIXED + 4;
    out[at++] = 53; out[at++] = 1; out[at++] = (uint8_t)type;
    out[at++] = 61; out[at++] = 7; out[at++] = 1; memcpy(out + at, mac, 6); at += 6;
    if (request) {out[at++] = 50; out[at++] = 4; memcpy(out + at, &request->s_addr, 4); at += 4;}
    if (server) {out[at++] = 54; out[at++] = 4; memcpy(out + at, &server->s_addr, 4); at += 4;}
    out[at++] = 57; out[at++] = 2; put16(out + at, 1500); at += 2;   /* max message size */
    static const uint8_t params[] = {1, 3, 6, 15, 26, 51, 58, 59};
    out[at++] = 55; out[at++] = sizeof params; memcpy(out + at, params, sizeof params); at += sizeof params;
    out[at++] = 255;
    return at < 300 ? 300 : at;                    /* BOOTP minimum */
}

int nd_dhcp_parse(const uint8_t *in, size_t len, uint32_t xid, const uint8_t mac[6], NdLease *l)
{
    if (!in || !l || len < FIXED + 4 + 3) return 0;
    if (in[0] != 2 || in[1] != 1 || in[2] != 6 || get32(in + 4) != xid || memcmp(in + 28, mac, 6) || get32(in + FIXED) != MAGIC) return 0;
    memset(l, 0, sizeof *l);
    memcpy(&l->yiaddr.s_addr, in + 16, 4);
    int type = 0, overload = 0;
    /* options, then (option 52) the file and sname fields */
    struct {size_t from, to;} areas[3] = {{FIXED + 4, len}, {108, 236}, {44, 108}};
    for (int area = 0; area < 3; ++area) {
        if (area == 1 && !(overload & 1)) continue;
        if (area == 2 && !(overload & 2)) continue;
        size_t at = areas[area].from;
        const size_t end = areas[area].to;
        while (at < end) {
            const uint8_t code = in[at++];
            if (code == 0) continue;
            if (code == 255) break;
            if (at >= end) return 0;
            const uint8_t n = in[at++];
            if (at + n > end) return 0;
            const uint8_t *v = in + at;
            switch (code) {
            case 53: if (n == 1) type = v[0]; break;
            case 52: if (n == 1 && area == 0) overload = v[0]; break;
            case 1: if (n == 4) memcpy(&l->mask.s_addr, v, 4); break;
            case 3: if (n >= 4) memcpy(&l->router.s_addr, v, 4); break;
            case 6: for (unsigned i = 0; i + 4 <= n && l->dns_count < 2; i += 4) memcpy(&l->dns[l->dns_count++].s_addr, v + i, 4); break;
            case 54: if (n == 4) memcpy(&l->server.s_addr, v, 4); break;
            case 51: if (n == 4) l->lease_s = get32(v); break;
            case 58: if (n == 4) l->t1_s = get32(v); break;
            case 26: if (n == 2) l->mtu = get16(v); break;
            default: break;
            }
            at += n;
        }
    }
    if (type != ND_DHCPOFFER && type != ND_DHCPACK && type != ND_DHCPNAK) return 0;
    if (type != ND_DHCPNAK) {
        /* a usable address: not 0, not broadcast/multicast/loopback; a server identifier; a netmask (or default by class) */
        const uint32_t a = ntohl(l->yiaddr.s_addr);
        if (!a || a == 0xffffffffu || (a >> 28) == 0xe || (a >> 24) == 127 || !l->server.s_addr) return 0;
        if (!l->mask.s_addr) l->mask.s_addr = htonl((a >> 30) == 3 ? 0xffffff00u : (a >> 31) ? 0xffff0000u : 0xff000000u);
        const uint32_t m = ntohl(l->mask.s_addr);
        if ((m | (m - 1)) != 0xffffffffu && m != 0xffffffffu) return 0;  /* not contiguous */
        if (l->router.s_addr && ((l->router.s_addr ^ l->yiaddr.s_addr) & l->mask.s_addr)) l->router.s_addr = 0;  /* off-link router: ignored */
    }
    return type;
}

uint16_t nd_checksum(const void *data, size_t len, uint32_t sum)
{
    const uint8_t *p = data;
    while (len > 1) {sum += (uint32_t)(p[0] << 8 | p[1]); p += 2; len -= 2;}
    if (len) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

size_t nd_udp_ip_frame(uint8_t *out, size_t cap, struct in_addr src, struct in_addr dst, uint16_t sport, uint16_t dport,
                       const uint8_t *payload, size_t len, int host_order)
{
    const size_t total = 20 + 8 + len;
    if (!out || !payload || total > cap || total > 1500) return 0;
    uint8_t *ip = out, *udp = out + 20;
    memset(out, 0, 28);
    ip[0] = 0x45; ip[8] = 64; ip[9] = 17;
    memcpy(ip + 12, &src.s_addr, 4); memcpy(ip + 16, &dst.s_addr, 4);
    put16(ip + 2, (uint16_t)total);
    const uint16_t sum = nd_checksum(ip, 20, 0);
    put16(ip + 10, sum);
    put16(udp, sport); put16(udp + 2, dport); put16(udp + 4, (uint16_t)(8 + len));
    memcpy(udp + 8, payload, len);
    /* pseudo header: src, dst, protocol, UDP length */
    uint32_t seed = 17 + (uint32_t)(8 + len);
    const uint8_t *s = ip + 12;
    for (int i = 0; i < 8; i += 2) seed += (uint32_t)(s[i] << 8 | s[i + 1]);
    uint16_t us = nd_checksum(udp, 8 + len, seed);
    put16(udp + 6, us ? us : 0xffff);
    if (host_order) {const uint16_t t = (uint16_t)total; memcpy(ip + 2, &t, 2);}  /* ip_off is 0 either way */
    return total;
}

size_t nd_dns_query(uint8_t *out, size_t cap, uint16_t id, const char *name)
{
    if (!out || !name || cap < 18) return 0;
    put16(out, id); put16(out + 2, 0x0100); put16(out + 4, 1); memset(out + 6, 0, 6);
    size_t at = 12;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        const size_t n = dot ? (size_t)(dot - p) : strlen(p);
        if (!n || n > 63 || at + 1 + n + 5 > cap) return 0;
        out[at++] = (uint8_t)n; memcpy(out + at, p, n); at += n;
        p += n; if (*p == '.') ++p;
    }
    if (at == 12 || at + 5 > cap || at - 12 > 255) return 0;
    out[at++] = 0; put16(out + at, 1); put16(out + at + 2, 1);
    return at + 4;
}

/* skips a (possibly compressed) name; returns the offset after it, 0 when malformed */
static size_t skip_name(const uint8_t *in, size_t len, size_t at)
{
    for (unsigned labels = 0; labels < 128; ++labels) {
        if (at >= len) return 0;
        const uint8_t c = in[at];
        if (c == 0) return at + 1;
        if ((c & 0xc0) == 0xc0) return at + 2 <= len ? at + 2 : 0;
        if (c & 0xc0) return 0;
        at += 1u + c;
    }
    return 0;
}

int nd_dns_answer(const uint8_t *in, size_t len, uint16_t id, struct in_addr *answer)
{
    if (!in || !answer || len < 12) return -1;
    if (get16(in) != id || !(in[2] & 0x80)) return -2;
    if (in[3] & 0x0f) return -3;
    const unsigned qd = get16(in + 4), an = get16(in + 6);
    size_t at = 12;
    for (unsigned i = 0; i < qd; ++i) {at = skip_name(in, len, at); if (!at || at + 4 > len) return -1; at += 4;}
    for (unsigned i = 0; i < an; ++i) {
        at = skip_name(in, len, at);
        if (!at || at + 10 > len) return -1;
        const uint16_t type = get16(in + at), klass = get16(in + at + 2), rdlen = get16(in + at + 8);
        at += 10;
        if (at + rdlen > len) return -1;
        if (type == 1 && klass == 1 && rdlen == 4) {memcpy(&answer->s_addr, in + at, 4); return 1;}
        at += rdlen;
    }
    return 0;
}

int nd_http_parse(const char *buf, size_t len, NdHttp *out)
{
    if (!buf || !out || len < 12 || memcmp(buf, "HTTP/1.", 7) || (buf[7] != '0' && buf[7] != '1') || buf[8] != ' ') return 0;
    if (buf[9] < '1' || buf[9] > '5' || buf[10] < '0' || buf[10] > '9' || buf[11] < '0' || buf[11] > '9') return 0;
    out->status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
    out->apple_success = 0;
    static const char want[] = "<TITLE>Success</TITLE>";
    for (size_t i = 0; i + sizeof want - 1 <= len; ++i)
        if (!memcmp(buf + i, want, sizeof want - 1)) {out->apple_success = 1; break;}
    return 1;
}
