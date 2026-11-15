#ifndef NETPREFS369_CF_H
#define NETPREFS369_CF_H
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
/* Pure reducers; never serialize identifiers, values or dictionary contents. */
typedef struct { int present, numeric; double stamp; } np_stamp;
static np_stamp np_timestamp(CFTypeRef object, CFStringRef key) {
 np_stamp r={0,0,0};
 if(!object || CFGetTypeID(object)!=CFDictionaryGetTypeID()) return r;
 CFTypeRef v=CFDictionaryGetValue((CFDictionaryRef)object,key); r.present=v!=NULL;
 if(v && CFGetTypeID(v)==CFNumberGetTypeID()) {
  double d=0; if(CFNumberGetValue((CFNumberRef)v,kCFNumberDoubleType,&d) && isfinite(d) && d>=0 && d<=1e12) {r.numeric=1;r.stamp=d;}
 }
 return r;
}
static int np_model_match(CFTypeRef model, CFStringRef hardware, int *known) {
 *known=0;
 if(!model || CFGetTypeID(model)!=CFStringGetTypeID() || !hardware || CFStringGetLength((CFStringRef)model)>255) return 0;
 *known=1; return CFEqual(model,hardware)!=0;
}
typedef struct { int present,known,truncated; long count,inspected,dicts,prefix_en,unit0,mac6,en0mac6; } np_interfaces;
static np_interfaces np_reduce_interfaces(CFTypeRef value) {
 np_interfaces r={0}; r.present=value!=NULL;
 if(!value || CFGetTypeID(value)!=CFArrayGetTypeID()) return r;
 r.known=1; CFIndex n=CFArrayGetCount((CFArrayRef)value); r.count=(long)(n>64?64:n); r.truncated=n>64;
 CFIndex lim=n>64?64:n; r.inspected=(long)lim;
 for(CFIndex i=0;i<lim;i++) {
  CFTypeRef v=CFArrayGetValueAtIndex((CFArrayRef)value,i);
  if(!v || CFGetTypeID(v)!=CFDictionaryGetTypeID()) continue;
  r.dicts++; CFDictionaryRef d=(CFDictionaryRef)v;
  CFTypeRef prefix=CFDictionaryGetValue(d,CFSTR("IOInterfaceNamePrefix"));
  CFTypeRef unit=CFDictionaryGetValue(d,CFSTR("IOInterfaceUnit"));
  CFTypeRef mac=CFDictionaryGetValue(d,CFSTR("IOMACAddress"));
  int en=prefix && CFGetTypeID(prefix)==CFStringGetTypeID() && CFEqual(prefix,CFSTR("en"));
  double u=1; int zero=unit && CFGetTypeID(unit)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)unit,kCFNumberDoubleType,&u) && isfinite(u) && u==0;
  int m=mac && CFGetTypeID(mac)==CFDataGetTypeID() && CFDataGetLength((CFDataRef)mac)==6;
  r.prefix_en+=en; r.unit0+=zero; r.mac6+=m; r.en0mac6+=en&&zero&&m;
 }
 return r;
}
/* Require a bounded, nonempty, terminated sysctl string, not silent truncation. */
static int np_model_bytes_ok(const char *b, size_t returned, size_t capacity) {
 if(!b || returned<2 || returned>capacity) return 0;
 const char *z=memchr(b,0,returned); return z==b+returned-1;
}
#endif
