#!/usr/bin/env python3
"""Reproduce the isolated K70 display-source publication experiment.

Input is the exact locally qualified display164 source, not an arbitrary
upstream revision. Writes a new file only; does not build, boot or flash.
Retain the input file's license. Do not apply this as a generic import fix:
producer completion and writable aliases require separate qualification.
"""
from pathlib import Path
import argparse,hashlib,json
ap=argparse.ArgumentParser()
ap.add_argument('source',type=Path)
ap.add_argument('output',type=Path)
args=ap.parse_args()
data=args.source.read_bytes()
assert hashlib.sha256(data).hexdigest()=='14f5bb6c762a3b5790f7494a88ac21ef85b96c40ee13c7183e8d48434ceae905'
assert args.source.resolve()!=args.output.resolve() and not args.output.exists()
s=data.decode()
anchor='        if(!gpuBypass && native_legacy_gpu_offer(this,memory,sourceStride,width,height,half?halfOfferFormat():NGP_BGRX8888)) {'
assert s.count(anchor)==1
replacement=r'''        // Isolated169 experiment: publish the finished surface through the SAME
        // cacheable map/cache maintenance already used by the working CPU path.
        // No pixel reads, destination writes, additional lease or retirement change.
        bool sourcePublished=true;
        unsigned gpuPublicationMode=0;PE_parse_boot_argn("sheng-fb-gpu",&gpuPublicationMode,sizeof(gpuPublicationMode));
        if(!gpuBypass && gpuPublicationMode==1) {
            const uint64_t began=mach_absolute_time();
            IOMemoryMap *publication=memory->map(kIOMapAnywhere|kIOMapReadOnly|kIOMapCopybackCache);
            const uint64_t bytes=publication?publication->getLength():0;
            sourcePublished=publication && publication->getVirtualAddress() && bytes>=allocation && bytes<=UINT64_C(67108864);
            if(sourcePublished) {
                flush_dcache64((addr64_t)publication->getVirtualAddress(),(unsigned)bytes,false);
                __asm__ volatile("dsb sy" ::: "memory");
            }
            static unsigned publications;const unsigned count=++publications;
            if(count<=4 || !(count&127u) || !sourcePublished) {
                uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time()-began,&ns);
                char line[176];const int n=snprintf(line,sizeof(line),
                    "K70 GPU INPUT publish=%u ready=%u us=%llu bytes=%llu src=%ux%u stride=%llu fmt=%x half=%u\n",
                    count,unsigned(sourcePublished),ns/1000,bytes,width,height,sourceStride,
                    native_legacy_surface_format(surface),unsigned(half));
                if(n>0 && n<(int)sizeof(line))k70_xnu_write(line,unsigned(n));
            }
            OSSafeReleaseNULL(publication);
        }
        if(!gpuBypass && sourcePublished && native_legacy_gpu_offer(this,memory,sourceStride,width,height,half?halfOfferFormat():NGP_BGRX8888)) {'''
out=s.replace(anchor,replacement).encode()
digest=hashlib.sha256(out).hexdigest()
assert digest=='917365e564143bc33b8cfe6241a8d2f78e4f550abc721491ee71e228b393c52b'
args.output.write_bytes(out)
print(json.dumps({'input_sha256':hashlib.sha256(data).hexdigest(),'output_sha256':digest,
                  'built':False,'phone_written':False,'hardware_verified_by_this_script':False},indent=2))
