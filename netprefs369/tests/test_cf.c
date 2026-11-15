#include "../src/netprefs369_cf.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define T(x) do{assert(x);checks++;}while(0)
int main(void) {
 T(np_model_bytes_ok("x",2,256));
 CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 T(!np_timestamp(NULL,CFSTR("*QUIET*")).present);T(!np_timestamp(CFSTR("bad"),CFSTR("*QUIET*")).present);
 CFDictionarySetValue(d,CFSTR("*QUIET*"),kCFBooleanTrue);np_stamp ts=np_timestamp(d,CFSTR("*QUIET*"));T(ts.present&&!ts.numeric);
 double values[]={1.25,0,-1,NAN,INFINITY,1e13};
 for(unsigned i=0;i<6;i++){CFNumberRef n=CFNumberCreate(NULL,kCFNumberDoubleType,&values[i]);CFDictionarySetValue(d,CFSTR("*QUIET*"),n);ts=np_timestamp(d,CFSTR("*QUIET*"));T(ts.present);T(ts.numeric==(i<2));CFRelease(n);}
 int known=0;T(np_model_match(CFSTR("fixture"),CFSTR("fixture"),&known)&&known);T(!np_model_match(CFSTR("other"),CFSTR("fixture"),&known)&&known);T(!np_model_match(NULL,CFSTR("fixture"),&known)&&!known);T(!np_model_match(kCFBooleanTrue,CFSTR("fixture"),&known)&&!known);T(!np_model_match(CFSTR("fixture"),NULL,&known)&&!known);
 np_interfaces r=np_reduce_interfaces(NULL);T(!r.present&&!r.known);r=np_reduce_interfaces(d);T(r.present&&!r.known);
 CFMutableArrayRef a=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);r=np_reduce_interfaces(a);T(r.known&&r.count==0&&!r.truncated);
 unsigned char synthetic[6]={2,0,0,0,0,1};CFDataRef mac=CFDataCreate(NULL,synthetic,6);int zero=0;CFNumberRef unit=CFNumberCreate(NULL,kCFNumberIntType,&zero);
 CFDictionarySetValue(d,CFSTR("IOInterfaceNamePrefix"),CFSTR("en"));CFDictionarySetValue(d,CFSTR("IOInterfaceUnit"),unit);CFDictionarySetValue(d,CFSTR("IOMACAddress"),mac);
 CFArrayAppendValue(a,d);r=np_reduce_interfaces(a);T(r.en0mac6==1&&r.unit0==1&&r.prefix_en==1&&r.mac6==1);
 CFDictionarySetValue(d,CFSTR("IOInterfaceUnit"),kCFBooleanFalse);r=np_reduce_interfaces(a);T(r.unit0==0&&r.en0mac6==0);
 CFDictionarySetValue(d,CFSTR("IOInterfaceUnit"),unit);CFDictionarySetValue(d,CFSTR("IOMACAddress"),CFSTR("not-data"));r=np_reduce_interfaces(a);T(r.mac6==0&&r.en0mac6==0);
 CFDictionarySetValue(d,CFSTR("IOMACAddress"),mac);for(int i=1;i<65;i++)CFArrayAppendValue(a,d);r=np_reduce_interfaces(a);T(r.count==64&&r.inspected==64&&r.truncated&&r.en0mac6==64);
 CFRelease(unit);CFRelease(mac);CFRelease(a);CFRelease(d);puts("NETPREFS369 CF_FIXTURE PASS");printf("NETPREFS369 checks=%u\n",checks);return 0;
}
