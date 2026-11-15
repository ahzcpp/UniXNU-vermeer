"""Audit actual linked constructor tables, and the source bootstrap ordering."""
from pathlib import Path
import hashlib,json,struct,subprocess
D=Path('/mnt/d/K70-UniXNU-data');O=D/'kernel407-work';ctor='__GLOBAL__sub_I_NativeProxyNVRAM399.cpp'
def inits(p):
    b=p.read_bytes();syms={int(l.split()[0],16):l.split()[-1] for l in subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm','--defined-only',str(p)],text=True).splitlines() if len(l.split())==3};at=32;out=[]
    for _ in range(struct.unpack_from('<I',b,16)[0]):
        cmd,size=struct.unpack_from('<II',b,at)
        if cmd==0x19:
            for i in range(struct.unpack_from('<I',b,at+64)[0]):
                off=at+72+80*i;name=b[off:off+16].rstrip(b'\0').decode();segment=b[off+16:off+32].rstrip(b'\0').decode()
                if name!='__mod_init_func':continue
                va,n,fo=struct.unpack_from('<QQI',b,off+32);assert n%8==0
                for j,value in enumerate(struct.unpack_from('<'+'Q'*(n//8),b,fo)):
                    assert value in syms,hex(value);out.append({'segment':segment,'section':name,'index':j,'function':syms[value]})
        at+=size
    return out
parent=inits(D/'kernel394-work/mach.i16.unstripped.noctf');native=inits(O/'mach.i16.unstripped.noctf');prelinked=inits(O/'kernel-prelinked.macho')
names=lambda rows:[x['function'] for x in rows]
assert names(native).count(ctor)==names(prelinked).count(ctor)==1
assert [x for x in names(native) if x!=ctor]==names(parent)
assert names(prelinked)==names(native)
where=next(x for x in native if x['function']==ctor);assert where['segment']=='__DATA_CONST'
assert names(native).index(ctor)<names(native).index('_last_kernel_constructor')
S=Path('/root/k70-clean-20261104/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/tree/.cache/xnu-work');source=S/'iokit/Kernel/IOStartIOKit.cpp';text=source.read_text()
assert text.index('\tOSlibkernInit();')<text.index('\tgRootNub = new IOPlatformExpertDevice;')<text.index('\tgRootNub->configureDefaults();')
runtime=S/'libkern/c++/OSRuntime.cpp';rt=runtime.read_text();assert 'OSRuntimeInitializeCPP(NULL)' in rt and 'nextsegfromheader(header, segment)' in rt and 'sectionNames[kOSSectionNameInitializer]' in rt
report=dict(status='PASS_ACTUAL_NATIVE_AND_PRELINKED_CONSTRUCTOR_TABLES',normal_constructor_in_native_and_prelinked=True,native_constructor_before_iokit_post_constructor_init=True,source_CPP_init_before_root_creation=True,parent_constructor_sequence_retained=True,only_new_constructor=ctor,normal_constructor_position=where,constructor_counts={'394':len(parent),'407_native':len(native),'407_prelinked':len(prelinked)},source_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,runtime)},hardware_tested=False,limitations='Static linked tables and actual native bootstrap source; not an execution trace, successful matching, root quiet or UI/network proof.')
(O/'startup-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
