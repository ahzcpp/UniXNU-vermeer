#!/usr/bin/env python3
"""Execute the actual patched AIR integer kernels on the host and compare all pixels.

This validates coordinates, channel conversion, padded pitches and write bounds.
It does not simulate GPU imports, caches, fences or achieved frame rate.
"""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
root=Path(sys.argv[1]).resolve();out=root/'build/scanout-host';out.mkdir(parents=True,exist_ok=True)
source=root/'drivers/AdrenoMetal/shaders/scanout_convert.ll'
s=source.read_text().replace('target triple = "air64_v28-apple-macosx26.0.0"','target triple = "x86_64-unknown-linux-gnu"')
s+='''
define void @host_opaque(ptr %s, ptr %d, ptr %p, i32 %x, i32 %y) {
  %g0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %g1 = insertelement <2 x i32> %g0, i32 %y, i32 1
  %ss = addrspacecast ptr %s to ptr addrspace(1)
  %dd = addrspacecast ptr %d to ptr addrspace(1)
  %pp = addrspacecast ptr %p to ptr addrspace(2)
  call void @scanout_opaque(ptr addrspace(1) %ss, ptr addrspace(1) %dd, ptr addrspace(2) %pp, <2 x i32> %g1)
  ret void
}
define void @host_convert(ptr %s, ptr %d, ptr %t, ptr %p, i32 %x, i32 %y) {
  %g0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %g1 = insertelement <2 x i32> %g0, i32 %y, i32 1
  %ss = addrspacecast ptr %s to ptr addrspace(1)
  %dd = addrspacecast ptr %d to ptr addrspace(1)
  %tt = addrspacecast ptr %t to ptr addrspace(1)
  %pp = addrspacecast ptr %p to ptr addrspace(2)
  call void @scanout_convert(ptr addrspace(1) %ss, ptr addrspace(1) %dd, ptr addrspace(1) %tt, ptr addrspace(2) %pp, <2 x i32> %g1)
  ret void
}
'''
(out/'scanout-host.ll').write_text(s)
c=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
void host_opaque(const uint32_t *,uint32_t *,const uint32_t *,uint32_t,uint32_t);
void host_convert(const uint32_t *,uint32_t *,const uint32_t *,const uint32_t *,uint32_t,uint32_t);
static uint32_t bits(uint32_t x) {x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;return x^(x>>16);}
int main(void) {
    const uint32_t dims[][4]={{1,1,1,1},{3,5,7,11},{7,11,3,5},{15,7,16,8},{16,8,15,7},{750,1624,1440,3200},{1440,3200,750,1624},{16384,1,16384,1}};
    uint32_t table[65536];for(unsigned i=0;i<65536;i++)table[i]=bits(i)&255;
    unsigned count=0;uint64_t pixels=0;
    for(unsigned k=0;k<sizeof(dims)/sizeof(*dims);k++)for(unsigned half=0;half<2;half++)for(unsigned clip=0;clip<2;clip++) {
        uint32_t sw=dims[k][0],sh=dims[k][1],dw=dims[k][2],dh=dims[k][3],rows=clip?dh/2:dh;
        uint32_t ss=sw*(half?2:1)+7,ds=dw+13;
        size_t sn=(size_t)ss*sh,dn=(size_t)ds*dh;
        uint32_t *src=malloc((sn+32)*4),*dst=malloc((dn+32)*4),*ref=malloc((dn+32)*4),*before=malloc((sn+32)*4);
        assert(src && dst && ref && before);
        for(size_t i=0;i<sn+32;i++)src[i]=bits((uint32_t)i+191);
        memcpy(before,src,(sn+32)*4);
        for(size_t i=0;i<dn+32;i++)dst[i]=ref[i]=0x1234abcd;
        _Alignas(16) uint32_t p[8]={dw,rows,ss,ds,sw,sh,dw,dh};
        for(uint32_t y=0;y<((rows+7)/8)*8;y++)for(uint32_t x=0;x<((dw+15)/16)*16;x++) {
            if(half)host_convert(src+16,dst+16,table,p,x,y);else host_opaque(src+16,dst+16,p,x,y);
        }
        for(uint32_t y=0;y<rows;y++)for(uint32_t x=0;x<dw;x++) {
            size_t sx=(uint64_t)x*sw/dw,sy=(uint64_t)y*sh/dh,at=16+sy*ss+sx*(half?2:1);
            uint32_t value=src[at];
            if(half)value=(table[value&65535]<<16)|(table[value>>16]<<8)|table[src[at+1]&65535];
            ref[16+(size_t)y*ds+x]=value|0xff000000;
        }
        if(memcmp(dst,ref,(dn+32)*4)||memcmp(src,before,(sn+32)*4)){fprintf(stderr,"FAIL case %u half%u clip%u\n",k,half,clip);return 1;}
        pixels+=(uint64_t)dw*rows;count++;
        free(src);free(dst);free(ref);free(before);
    }
    printf("{\"cases\":%u,\"pixels_compared\":%llu,\"every_pixel_equal\":true,\"source_unchanged\":true,\"canaries_and_pitch_padding_unchanged\":true}\n",count,(unsigned long long)pixels);
}
'''
(out/'check.c').write_text(c)
compiler=shutil.which('clang-18') or shutil.which('clang')
assert compiler,'A host LLVM compiler is required'
subprocess.run([compiler,'-O2','-c',out/'scanout-host.ll','-o',out/'scanout-host.o'],check=True)
subprocess.run([compiler,'-O2','-Wall','-Wextra','-Werror',out/'check.c',out/'scanout-host.o','-o',out/'check'],check=True)
result=json.loads(subprocess.check_output([out/'check'],text=True))
result.update(air_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),gpu_hardware_tested=False)
(root/'build/ios16-scanout-host-check.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
