#include "../src/netdiag_bpf_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define C(x) do{checks++;assert(x);}while(0)
static const uint8_t mac[6]={2,0,0x5e,0x10,0x20,0x30};
static void be16(uint8_t *p,unsigned x){p[0]=x>>8;p[1]=x;}
static void le32(uint8_t *p,unsigned x){for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
/* Independent bytewise checksum verifier (including stored checksum). */
static unsigned sum(const uint8_t *p,size_t n,unsigned v){for(size_t i=0;i<n;i++)v+=(unsigned)p[i]<<((i&1)?0:8);while(v>>16)v=(v&65535)+(v>>16);return v;}
static void repair(uint8_t *f){uint8_t *ip=f+14;unsigned h=(ip[0]&15)*4,u=nd_bpf_be16(ip+2)-h;ip[10]=ip[11]=0;be16(ip+10,65535-sum(ip,h,0));uint8_t *udp=ip+h;udp[6]=udp[7]=0;unsigned v=sum(ip+12,8,17+u);unsigned c=65535-sum(udp,u,v);be16(udp+6,c?c:65535);}
static unsigned vm(const uint8_t *p,size_t n){
 unsigned a=0,x=0;
 for(size_t pc=0,steps=0;pc<16&&steps++<32;pc++){
  NdBpfInsn i=nd_dhcp_bpf_filter[pc];size_t at=i.k;
  switch(i.code){
  case 0x28:if(at>=n||n-at<2)return 0;a=nd_bpf_be16(p+at);break;
  case 0x30:if(at>=n)return 0;a=p[at];break;
  case 0x54:a&=i.k;break;
  case 0xb1:if(at>=n)return 0;x=(p[at]&15)*4;break;
  case 0x48:at+=x;if(at>=n||n-at<2)return 0;a=nd_bpf_be16(p+at);break;
  case 0x15:pc+=a==i.k?i.jt:i.jf;break;
  case 0x45:pc+=(a&i.k)?i.jt:i.jf;break;
  case 0x06:return i.k;
  default:assert(0);
  }
 }return 0;
}
static size_t offer(uint8_t *f,unsigned ihl,int type){
 uint8_t p[300]={0};p[0]=2;p[1]=1;p[2]=6;p[4]=0x12;p[5]=0x34;p[6]=0x56;p[7]=0x78;
 memcpy(p+28,mac,6);p[16]=192;p[17]=0;p[18]=2;p[19]=77;memcpy(p+236,"\x63\x82\x53\x63",4);
 uint8_t opts[]={53,1,0,54,4,192,0,2,1,1,4,255,255,255,0,255};opts[2]=type;memcpy(p+240,opts,sizeof opts);
 struct in_addr src,dst;src.s_addr=htonl(0xc0000201);dst.s_addr=UINT32_MAX;
 memset(f,255,6);memcpy(f+6,mac,6);f[6]^=0x10;f[12]=8;f[13]=0;
 size_t n=nd_udp_ip_frame(f+14,1500,src,dst,67,68,p,sizeof p,0)+14;
 if(ihl>20){memmove(f+14+ihl,f+34,n-34);memset(f+34,1,ihl-20);f[14]=(uint8_t)(0x40+ihl/4);n+=ihl-20;be16(f+16,n-14);repair(f);}
 return n;
}
static size_t record(uint8_t *out,const uint8_t *f,size_t n,unsigned h){memset(out,0,h+n+4);le32(out+8,n);le32(out+12,n);out[16]=h;out[17]=h>>8;memcpy(out+h,f,n);return (h+n+3)&~(size_t)3;}
int main(void){
 uint8_t p[ND_DHCP_MAX],f[1600],g[1600],block[4096];const uint8_t *body,*rframe;size_t z,bytes;
 size_t n=nd_dhcp_build(p,sizeof p,ND_DHCPDISCOVER,0x12345678,mac,NULL,NULL,0);
 C(nd_bpf_mac_valid(mac));uint8_t invalid[6]={0};C(!nd_bpf_mac_valid(invalid));invalid[0]=1;C(!nd_bpf_mac_valid(invalid));C(!nd_bpf_mac_valid(NULL));
 z=nd_dhcp_ether_frame(f,sizeof f,mac,p,n);C(z==342);C(!memcmp(f,"\xff\xff\xff\xff\xff\xff",6));C(!memcmp(f+6,mac,6));C(f[12]==8&&f[13]==0);
 C(f[14]==0x45&&nd_bpf_be16(f+16)==328&&nd_bpf_be16(f+34)==68&&nd_bpf_be16(f+36)==67);
 C(sum(f+14,20,0)==65535);C(sum(f+34,308,sum(f+26,8,17+308))==65535);C(!memcmp(f+42,p,300));
 C(!nd_dhcp_ether_frame(f,341,mac,p,n));C(!nd_dhcp_ether_frame(NULL,sizeof f,mac,p,n));C(!nd_dhcp_ether_frame(f,sizeof f,mac,p,SIZE_MAX));
 for(size_t cut=0;cut<300;cut++)C(!nd_bpf_client_valid(p,cut,mac));
 for(unsigned at=0;at<34;at++){memcpy(g,p,n);g[at]^=1;if(at==0||at==1||at==2||(at>=28&&at<34))C(!nd_bpf_client_valid(g,n,mac));}
 memcpy(g,p,n);g[242]=ND_DHCPOFFER;C(!nd_bpf_client_valid(g,n,mac));memcpy(g,p,n);g[241]=255;C(!nd_bpf_client_valid(g,n,mac));
 for(unsigned h=20;h<=60;h+=4){n=offer(f,h,ND_DHCPOFFER);C(vm(f,n)==ND_ETH_MAX);C(nd_bpf_dhcp_payload(f,n,mac,&body,&bytes));NdLease l;C(nd_dhcp_parse(body,bytes,0x12345678,mac,&l)==ND_DHCPOFFER);C(nd_dhcp_parse(body,bytes,0x12345679,mac,&l)==0);uint8_t other[6]={2,9,9,9,9,9};C(nd_dhcp_parse(body,bytes,0x12345678,other,&l)==0);}
 n=offer(f,20,ND_DHCPACK);C(nd_bpf_dhcp_payload(f,n,mac,&body,&bytes));NdLease lease;C(nd_dhcp_parse(body,bytes,0x12345678,mac,&lease)==ND_DHCPACK);
 memcpy(g,f,n);g[14+20+6]=g[14+20+7]=0;C(nd_bpf_dhcp_payload(g,n,mac,&body,&bytes)); /* IPv4 UDP zero checksum legal */
 unsigned mutate[]={12,14,16,20,23,24,34,36,38,40,80};
 for(size_t i=0;i<sizeof mutate/sizeof mutate[0];i++){memcpy(g,f,n);g[mutate[i]]^=1;C(!nd_bpf_dhcp_payload(g,n,mac,&body,&bytes));}
 for(unsigned frag=0;frag<3;frag++){memcpy(g,f,n);be16(g+20,frag==0?0x2000:frag==1?1:0x8000);repair(g);C(!vm(g,n));C(!nd_bpf_dhcp_payload(g,n,mac,&body,&bytes));}
 memcpy(g,f,n);be16(g+20,0x4000);repair(g);C(vm(g,n));C(nd_bpf_dhcp_payload(g,n,mac,&body,&bytes));
 for(size_t cut=0;cut<n;cut++)C(!nd_bpf_dhcp_payload(f,cut,mac,&body,&bytes));
 size_t a=record(block,f,n,18),b=record(block+a,f,n,20),pos=0;C(nd_bpf_record(block,a+b,&pos,&rframe,&bytes)==1&&bytes==n&&pos==a);C(nd_bpf_record(block,a+b,&pos,&rframe,&bytes)==1&&pos==a+b);C(nd_bpf_record(block,a+b,&pos,&rframe,&bytes)==0);
 for(size_t cut=1;cut<18;cut++){pos=0;C(nd_bpf_record(block,cut,&pos,&rframe,&bytes)==-1);}
 block[16]=17;pos=0;C(nd_bpf_record(block,a,&pos,&rframe,&bytes)==-1);block[16]=18;le32(block+8,0xffffffff);pos=0;C(nd_bpf_record(block,a,&pos,&rframe,&bytes)==-1);
 uint32_t rng=3;for(unsigned k=0;k<12000;k++){size_t len=k%1600;for(size_t i=0;i<len;i++){rng=rng*1664525+1013904223;g[i]=rng>>24;}pos=0;(void)vm(g,len);(void)nd_bpf_dhcp_payload(g,len,mac,&body,&bytes);(void)nd_bpf_record(g,len,&pos,&rframe,&bytes);C(pos<=len);}
 printf("PASS %u BPF/Ethernet/DHCP framing/filter/record/checksum/malformed-input checks; synthetic only\n",checks);return 0;
}
