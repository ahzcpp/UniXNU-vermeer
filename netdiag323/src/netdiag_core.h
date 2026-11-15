/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure encoders/decoders of unixnu-netdiag (no I/O): DHCP (RFC 2131/2132), IPv4/UDP framing for a raw socket, DNS A query/answer,
 * HTTP status line. Host-tested with fixtures in tests/. */
#ifndef NETDIAG_CORE_H
#define NETDIAG_CORE_H
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>

#define ND_DHCP_MAX 548u
enum {ND_DHCPDISCOVER = 1, ND_DHCPOFFER = 2, ND_DHCPREQUEST = 3, ND_DHCPACK = 5, ND_DHCPNAK = 6};

typedef struct {
    struct in_addr yiaddr, server, mask, router, dns[2];
    unsigned dns_count;
    uint32_t lease_s, t1_s, mtu;
} NdLease;

/* A client message: broadcast flag set, client id = 01:mac, parameter list mask/router/dns/lease/t1/mtu. request/server: options
 * 50/54 (REQUEST in SELECTING state); renewal REQUESTs pass request only. Returns the length, 0 on bad arguments. */
size_t nd_dhcp_build(uint8_t *out, size_t cap, int type, uint32_t xid, const uint8_t mac[6], const struct in_addr *request,
                     const struct in_addr *server, uint16_t secs);
/* Checks op/htype/hlen/xid/chaddr/magic and parses the options; returns the message type, 0 when not a usable reply. */
int nd_dhcp_parse(const uint8_t *in, size_t len, uint32_t xid, const uint8_t mac[6], NdLease *lease);
/* IPv4 + UDP header around payload, checksums computed. host_order: ip_len/ip_off in host byte order (BSD raw-socket rule). */
size_t nd_udp_ip_frame(uint8_t *out, size_t cap, struct in_addr src, struct in_addr dst, uint16_t sport, uint16_t dport,
                       const uint8_t *payload, size_t len, int host_order);
uint16_t nd_checksum(const void *data, size_t len, uint32_t seed);
/* DNS: a recursive A query for name; the first A record of a matching answer. */
size_t nd_dns_query(uint8_t *out, size_t cap, uint16_t id, const char *name);
/* 1 an A record (answer set), 0 no A record, negative: -1 malformed, -2 wrong id/not a response, -3 rcode != 0 */
int nd_dns_answer(const uint8_t *in, size_t len, uint16_t id, struct in_addr *answer);
typedef struct {int status; int apple_success;} NdHttp;
/* 1 when the buffer begins with a valid "HTTP/1.x NNN" status line; apple_success: the body contains "<TITLE>Success</TITLE>". */
int nd_http_parse(const char *buf, size_t len, NdHttp *out);
#endif
