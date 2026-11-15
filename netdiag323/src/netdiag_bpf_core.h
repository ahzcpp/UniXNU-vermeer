/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Pure, bounded Ethernet DHCP framing and Darwin BPF record decoder. */
#ifndef NETDIAG_BPF_CORE_H
#define NETDIAG_BPF_CORE_H
#include "netdiag_core.h"
#include <string.h>
#define ND_ETH_MAX 1514u
typedef struct {uint16_t code;uint8_t jt,jf;uint32_t k;} NdBpfInsn;
/* No promiscuous mode. IPv4 UDP67->68 only, no fragmented IP datagrams.
 * X is the real IPv4 IHL, so IPv4 options do not shift port matching. */
static const NdBpfInsn nd_dhcp_bpf_filter[] = {
 {0x28,0,0,12}, {0x15,0,13,0x0800},
 {0x30,0,0,14}, {0x54,0,0,0xf0}, {0x15,0,10,0x40},
 {0x30,0,0,23}, {0x15,0,8,17},
 {0x28,0,0,20}, {0x45,6,0,0xbfff},
 {0xb1,0,0,14}, {0x48,0,0,14}, {0x15,0,3,67},
 {0x48,0,0,16}, {0x15,0,1,68}, {0x06,0,0,ND_ETH_MAX}, {0x06,0,0,0}
};
static inline uint16_t nd_bpf_be16(const uint8_t *p){return (uint16_t)((unsigned)p[0]*256+p[1]);}
static inline uint32_t nd_bpf_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline uint16_t nd_bpf_le16(const uint8_t *p){return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static inline int nd_bpf_mac_valid(const uint8_t *m) {
 if(!m || (m[0]&1))return 0;
 return (m[0]|m[1]|m[2]|m[3]|m[4]|m[5])!=0;
}
static inline int nd_bpf_client_valid(const uint8_t *p,size_t n,const uint8_t mac[6]) {
 if(!p||!nd_bpf_mac_valid(mac)||n<300||n>ND_DHCP_MAX)return 0;
 if(p[0]!=1||p[1]!=1||p[2]!=6||memcmp(p+28,mac,6)||
    !(nd_bpf_be16(p+10)&0x8000)||nd_bpf_le32(p+12)!=0||
    memcmp(p+236,"\x63\x82\x53\x63",4))return 0;
 unsigned types=0,ended=0;
 for(size_t at=240;at<n;) {
  unsigned code=p[at++];if(!code)continue;if(code==255){ended=1;break;}
  if(at>=n)return 0;unsigned z=p[at++];if(z>n-at)return 0;
  if(code==53){if(z!=1||++types!=1||(p[at]!=ND_DHCPDISCOVER&&p[at]!=ND_DHCPREQUEST))return 0;}
  if(code==61&&(z!=7||p[at]!=1||memcmp(p+at+1,mac,6)))return 0;
  at+=z;
 }
 return ended&&types==1;
}
static inline size_t nd_dhcp_ether_frame(uint8_t *out,size_t cap,const uint8_t mac[6],const uint8_t *p,size_t n) {
 if(!out||!nd_bpf_client_valid(p,n,mac)||cap<14u+28u+n)return 0;
 struct in_addr src={0},dst={UINT32_MAX};
 size_t z=nd_udp_ip_frame(out+14,cap-14,src,dst,68,67,p,n,0);if(!z)return 0;
 memset(out,255,6);memcpy(out+6,mac,6);out[12]=8;out[13]=0;return z+14;
}
/* LP64 Darwin uses timeval32, NOT native64 timeval. bh_hdrlen can be18,
 * while sizeof(struct bpf_hdr)==20. Records align to4 bytes, not8. */
static inline int nd_bpf_record(const uint8_t *buf,size_t len,size_t *at,const uint8_t **frame,size_t *bytes) {
 if(!buf||!at||!frame||!bytes||*at>len)return -1;
 if(*at==len)return 0;
 size_t left=len-*at;if(left<18)return -1;const uint8_t *p=buf+*at;
 uint32_t cap=nd_bpf_le32(p+8),original=nd_bpf_le32(p+12);unsigned h=nd_bpf_le16(p+16);
 if(h<18||h>left||cap>original||cap>left-h)return -1;
 size_t used=(size_t)h+cap,step=(used+3u)&~(size_t)3u;
 if(step>left){if(used!=left)return -1;step=used;}
 *frame=p+h;*bytes=cap;*at+=step;return 1;
}
static inline int nd_bpf_dhcp_payload(const uint8_t *f,size_t n,const uint8_t mac[6],const uint8_t **p,size_t *bytes) {
 static const uint8_t broadcast[6]={255,255,255,255,255,255};
 if(!f||!p||!bytes||!nd_bpf_mac_valid(mac)||n<14+20+8||n>ND_ETH_MAX||nd_bpf_be16(f+12)!=0x0800)return 0;
 if(memcmp(f,mac,6)&&memcmp(f,broadcast,6))return 0;
 const uint8_t *ip=f+14;unsigned h=(ip[0]&15)*4u;
 if((ip[0]>>4)!=4||h<20||h>60||h>n-14||ip[9]!=17||!ip[8]||nd_bpf_be16(ip+6)&0xbfff)return 0;
 unsigned total=nd_bpf_be16(ip+2);if(total<h+8||total>n-14||nd_checksum(ip,h,0))return 0;
 const uint8_t *udp=ip+h;unsigned u=nd_bpf_be16(udp+4);
 if(nd_bpf_be16(udp)!=67||nd_bpf_be16(udp+2)!=68||u<8+243||u!=total-h)return 0;
 if(nd_bpf_be16(udp+6)){
  uint32_t seed=17+u;for(unsigned i=12;i<20;i+=2)seed+=nd_bpf_be16(ip+i);
  if(nd_checksum(udp,u,seed))return 0;
 }
 *p=udp+8;*bytes=u-8;return 1;
}
#endif
