/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Immutable CHRP snapshot core. No allocation, device access or persistence. */
#ifndef K70_PROXY_BANK399_H
#define K70_PROXY_BANK399_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace k70ro399 {
enum Status {ok=0,not_ready=1,bad_bank=2,unsupported=3,not_writable=4};
struct Geometry {uint32_t size,count,current;};
struct Bank {const uint8_t*bytes;size_t length;Geometry geometry;uint32_t version;bool ready;};
static inline uint16_t le16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static inline uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint32_t adler(const uint8_t*p,size_t n){uint32_t a=1,b=0;for(size_t i=0;i<n;i++){a=(a+p[i])%65521u;b=(b+a)%65521u;}return(b<<16)|a;}
static inline uint8_t header_sum(const uint8_t*p){unsigned s=p[0];for(unsigned i=2;i<16;i++)s+=p[i];while(s>255)s=(s&255)+(s>>8);return(uint8_t)s;}
static inline bool name_is(const uint8_t*p,const char*s){size_t n=strlen(s);if(n>12||memcmp(p,s,n))return false;for(size_t i=n;i<12;i++)if(p[i])return false;return true;}
static inline bool terminated_records(const uint8_t*p,size_t n){size_t i=0;while(i<n&&p[i]){const void*z=memchr(p+i,0,n-i);if(!z)return false;i=(size_t)((const uint8_t*)z-p)+1;}return true;}
static inline Status validate(const uint8_t*p,size_t n,Geometry g,Bank*out){
 if(!out)return bad_bank;*out=Bank{};
 if(!p)return not_ready;
 if(g.count!=1||g.current!=0)return unsupported; /* proxy has ONE backed image, no fabricated banks */
 if(n!=g.size||n<48||n>(1u<<20)||(n&15))return bad_bank;
 unsigned version=name_is(p+4,"nvram")?1:(name_is(p+4,"2nvram")?2:0);
 if(!version)return unsupported;
 if(le32(p+16)!=adler(p+20,n-20))return bad_bank;
 size_t at=0;unsigned common=0,system=0;while(at<n){
  if(n-at<16)return bad_bank;const uint8_t*h=p+at;size_t length=(size_t)le16(h+2)*16;
  if(h[0]!=0x70||h[1]!=header_sum(h)||length<16||length>n-at)return bad_bank;
  if(at==0){if(length!=32)return unsupported;}
  else if(name_is(h+4,"common")||name_is(h+4,"2common")){
   if(++common!=1||length<=16||!terminated_records(h+16,length-16))return bad_bank;
  }else if(name_is(h+4,"system")||name_is(h+4,"2system")){
   if(++system!=1||length<=16||!terminated_records(h+16,length-16))return bad_bank;
  }else if(!name_is(h+4,"free"))return unsupported;
  at+=length;
 }
 if(at!=n||common!=1)return bad_bank;
 *out=Bank{p,n,g,version,true};return ok;
}
static inline Status read(const Bank&b,uint64_t off,void*out,uint64_t len){
 if(!b.ready||!b.bytes)return not_ready;
 if(off>b.length||len>b.length-off||(!out&&len))return bad_bank;
 if(len)memcpy(out,b.bytes+(size_t)off,(size_t)len);return ok;
}
static inline Status select(const Bank&b,uint32_t bank){return !b.ready?not_ready:(bank==0?ok:bad_bank);}
static inline Status reject_write(const Bank&b){return b.ready?not_writable:not_ready;}
/* Source-level admission gate is shared by native glue and host failure tests. */
struct Admission {bool optin,parent_valid,snapshot_valid,started,exclusive;};
static inline bool may_activate(Admission a){return a.optin&&a.parent_valid&&a.snapshot_valid&&a.started&&a.exclusive;}
struct Activation {uint32_t claimed,active;const void*owner;};
static inline bool is_active(const Activation*a){return __atomic_load_n(&a->active,__ATOMIC_ACQUIRE)!=0;}
static inline bool for_provider(const Activation*a,const void*p){return p&&is_active(a)&&__atomic_load_n(&a->owner,__ATOMIC_ACQUIRE)==p;}
static inline bool activate(Activation*a,const void*p){
 if(!p)return false;uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&a->claimed,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
 __atomic_store_n(&a->owner,p,__ATOMIC_RELEASE);__atomic_store_n(&a->active,1u,__ATOMIC_RELEASE);return true;
}
}
#endif