#!/usr/bin/env python3
"""Compile an isolated display-only source candidate; no relink, payload or flash."""
import argparse,ast,hashlib,json,shutil,struct,subprocess,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--tree',type=Path,default=Path('/root/k70-clean-20261104/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/tree'))
p.add_argument('--out',type=Path,default=Path('/root/k70-clean-20261104/display-candidate-20261104'))
p.add_argument('--reference',type=Path,default=Path('/root/k70-clean-20261104/storage-candidate-20261104/payload-readback/EFI/UniXNU/Payloads/kernel.macho'))
p.add_argument('--plan',type=Path,default=Path('/root/k70-clean-20261104/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/kernel/kernel.json'))
a=p.parse_args();ROOT=a.tree;OUT=a.out.resolve();OUT.mkdir(parents=True,exist_ok=True)
CORE=Path('/root/k70-clean-20261104/UniXNU');GOP=CORE/'drivers/GopFramebuffer'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def require(v,why):
    if not v:raise SystemExit(why)
def run(cmd,**kw):
    r=subprocess.run([str(x) for x in cmd],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,**kw)
    if r.returncode:raise RuntimeError(r.stdout[-6000:])
    return r.stdout
require(str(OUT).startswith('/root/k70-clean-20261104/display-candidate-'),'Output must be a fresh isolated display candidate')
require(run(['git','-C',GOP,'rev-parse','HEAD']).strip()=='e47c1b8de169cd2bd0b885c7d85e433010e3cdf2','Pinned GOP source checkout')
require(not run(['git','-C',GOP,'status','--porcelain']).strip(),'Pinned GOP worktree must be unchanged')
sys.path.insert(0,str(CORE/'kits/XnuPrelink'))
from native_prelink import macho,backed_offset,exports
old=a.reference.read_bytes();old_image=macho(old)
require(hashlib.sha256(old).hexdigest()=='962381af05f3eee0c41cd08708ffba34299693388220cc994598fb63728f70d5','Frozen tested storage kernel reference')
plan=json.loads(a.plan.read_text());names={n:v for v,group in exports(old,old_image).items() for n in group}
def read(va,n):
    at=backed_offset(old_image['segments'],va,n,len(old));return old[at:at+n]
family='com.apple.iokit.IOMobileGraphicsFamily'
text=next(e for e in plan['extents'] if e['module']==family and e['segment']=='__TEXT_EXEC')
require(hashlib.sha256(read(text['va'],text['size'])).hexdigest()=='2e044ee68c1650397e1435234ca6459206262e4c7ed709cb7131c6f664f9bd9b','Exact unchanged 21H461 MobileGraphics executable')
def vptr(pc):
    x,y,z=struct.unpack('<3I',read(pc,12))
    require(x&0x9f00001f==0x90000008 and y&0xffc003ff==0x91000108 and z==0x91004108,'Constructor vptr sequence')
    imm=((x>>5)&0x7ffff)*4+((x>>29)&3)
    if imm&(1<<20):imm-=1<<21
    return (pc&~4095)+(imm<<12)+((y>>10)&4095)+16
base=struct.unpack('<262Q',read(vptr(0xfffffff00577b324),262*8))
legacy_vptr=vptr(0xfffffff0057865d8);legacy=struct.unpack('<358Q',read(legacy_vptr,358*8))
require(all(legacy) and read(legacy_vptr+358*8,24)==bytes(24),'Exact 358-entry vtable boundary')
pure=[i for i,va in enumerate(legacy) if va==names['___cxa_pure_virtual']]
require(pure==[211,213,245,246,306,307,337,338,345],'Nine original abstract leaves')
require(names['__ZN9IOService13newUserClientEP4taskPvjPP12IOUserClient']==base[141],'Native IOService newUserClient slot141')
audit={'legacy_vptr':hex(legacy_vptr),'abstract_slots':pure,'methods':[{'address':hex(v)} for v in legacy]};methods=audit['methods']
builder=(HERE/'reference/build-gop-legacy-display.sheng.py').read_text()
header_assignment=next(n for n in ast.parse(builder).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='header' for t in n.targets))
header=ast.literal_eval(header_assignment.value)
qualified={184:'bool isMainDisplay()',188:'IOReturn getDotPitch(uint32_t *)',213:'IOReturn supportedFrameInfo(uint32_t,uint32_t *,char *,uint32_t *)=0',
249:'void getDisplayDimensions(uint32_t *,uint32_t *)',290:'void kickSwapQueue()',296:'bool validateSwap(const OriginalLegacySwapRequest *)',
295:'void completeCurrentSwap()',337:'IOReturn collectCompletionReports(uint32_t,void *,uint32_t *,uint32_t)=0',288:'bool startHardware()',
354:'int applyStateRequest(OriginalLegacyStateRequest *,bool)',301:'bool getRefreshAttributes(uint64_t *,bool)'}
for i in range(168,358):
    if i in qualified:header+=' virtual '+qualified[i]+';\n'
    elif i in pure:header+=f' virtual __attribute__((noreturn)) void opaque{i}()=0;\n'
    else:header+=f' virtual __attribute__((deprecated("Unclassified ABI: never call from source"))) void opaque{i}();\n'
header+='};\nstatic_assert(sizeof(IOMobileFramebufferLegacy)==0x18a0,"Exact 21H461 instance extent");\n'
(OUT/'OriginalLegacyFramebufferABI.h').write_text(header)
snap=OUT/'source';snap.mkdir(exist_ok=True)
files=['NativeLegacyDisplay.cpp','NativeLegacyRequest.h','NativeGOPWire.h','NativeHalfCodes.h','NativeGpuTrace.h']
for name in files:shutil.copy2(GOP/name,snap/name)
driver=snap/'NativeLegacyDisplay.cpp';driver_text=driver.read_text()
old_slot='uint64_t(slot+1)<<48';new_slot='uint64_t(int64_t(slot)+1)<<48'
require(driver_text.count(old_slot)==1,'One exact trace-slot arithmetic expression')
driver.write_text(driver_text.replace(old_slot,new_slot))
shutil.copy2(HERE/'K70DisplayProvider.cpp',snap/'K70DisplayProvider.cpp')
record=json.loads((ROOT/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
def compile_source(src,obj):
    cmd=[];args=record['arguments'];i=0
    while i<len(args):
        value=args[i]
        if value==record['file']:cmd.append(str(src))
        elif value in ('-o','-MF','-MT','-MJ'):
            if value=='-o':cmd+=['-o',str(obj)]
            i+=1
        elif value=='-Xclang' and args[i+1].startswith('-W'):i+=1
        elif not value.startswith('-W'):cmd.append(value)
        i+=1
    cmd+=['-Wall','-Wextra','-Werror','-Wno-unknown-warning-option','-Wno-unused-parameter','-Wno-c99-designator','-Wno-reorder-init-list','-Wno-unnecessary-virtual-specifier',
      '-Wno-deprecated-enum-enum-conversion','-Wno-deprecated-volatile','-Wno-missing-designated-field-initializers',
      '-I',str(OUT),'-I',str(ROOT/'.cache/xnu-work/osfmk')]
    (OUT/(src.stem+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
    result=subprocess.run(cmd,cwd=record['directory'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (OUT/(src.stem+'-compile.log')).write_text(result.stdout)
    if result.returncode:raise RuntimeError(result.stdout[-6000:])
obj=OUT/'NativeLegacyDisplay.o';compile_source(snap/'NativeLegacyDisplay.cpp',obj)
raw=obj.read_bytes();image=macho(raw)
def nlists(data,m):
    at=next(at for c,at,_ in m['commands'] if c==2);sy,count,strings,_=struct.unpack_from('<4I',data,at+8);out=[]
    for i in range(count):
        ix,k,s,d,v=struct.unpack_from('<IBBHQ',data,sy+i*16)
        out.append((data[strings+ix:data.index(b'\0',strings+ix)].decode(),v,k,d))
    return out
symbols=[(n,v,k) for n,v,k,d in nlists(raw,image)]
vtable=next(v for n,v,k in symbols if n=='__ZTV19NativeLegacyDisplay');relocations={}
for segment in image['segments']:
    for sec in segment['sections']:
        if sec['va']<=vtable<sec['va']+sec['size']:
            ro,rc=struct.unpack_from('<2I',raw,sec['command_offset']+56)
            for i in range(rc):
                offset,bits=struct.unpack_from('<2I',raw,ro+i*8)
                if bits>>27&1 and bits>>28==0 and bits>>25&3==3:relocations[sec['va']+offset]=symbols[bits&0xffffff][0]
compiled=[relocations.get(vtable+16+i*8) for i in range(358)]
require(all(compiled) and vtable+16+358*8 not in relocations,'Exact compiled 358-entry owned vtable')
# This reviewed source fragment performs only exact original-byte and vtable
# dispatch checks and fills absolute aliases; it runs no build or host commands.
binding_code=(HERE/'reference/exact-21H461-binding-audit.py').read_text()
exec(compile(binding_code,'exact-21H461-binding-audit.py','exec'),globals())
asm=OUT/'original-bindings.S';asm.write_text(''.join(f'.globl {name}\n.set {name}, {va:#x}\n' for name,va in sorted(bindings.items())))
clang=Path(record['arguments'][0]);llvm=clang.parent
run([clang,'--target=arm64-apple-ios17.0','-c',asm,'-o',OUT/'original-bindings.o'])
late=OUT/'NativeLegacyDisplay.late.o'
sys.path.insert(0,str(HERE/'reference'))
from coachz_late_init import stage_initializer
late.write_bytes(stage_initializer(raw,'__GLOBAL__sub_I_NativeLegacyDisplay.cpp'))
def code(data):
    s=next(s for g in macho(data)['segments'] for s in g['sections'] if s['name']=='__text');return data[s['fileoff']:s['fileoff']+s['size']]
require(code(raw)==code(late.read_bytes()),'Late registration leaves driver machine code byte-identical')
glue=OUT/'K70DisplayProvider.o';compile_source(snap/'K70DisplayProvider.cpp',glue)
objects=[late,OUT/'original-bindings.o',glue];defined=set();strong=set();weak=set()
for f in objects:
    data=f.read_bytes()
    for n,v,k,d in nlists(data,macho(data)):
        if k&0xe0 or not k&1:continue
        if k&0xe:defined.add(n)
        elif d&0x40:weak.add(n)
        else:strong.add(n)
kernel_defined={n for n,v,k,d in nlists(old,old_image) if not k&0xe0 and k&1 and k&0xe}
missing=sorted(strong-defined-kernel_defined)
require(not missing,'Unresolved strong imports: '+repr(missing))
archive=OUT/'libK70GopDisplay.candidate.a';archive.unlink(missing_ok=True)
run(['/usr/lib/llvm-18/bin/llvm-ar','rcs',archive,*objects])
report={'status':'isolated_objects_abi_and_import_closure_passed','hardware_tested':False,'provider_runtime_registered':False,'desktop':False,
 'reference_kernel_sha256':hashlib.sha256(old).hexdigest(),'gop_commit':'e47c1b8de169cd2bd0b885c7d85e433010e3cdf2',
 'original_mobile_graphics_text_sha256':hashlib.sha256(read(text['va'],text['size'])).hexdigest(),
 'generated_abi_sha256':sha(OUT/'OriginalLegacyFramebufferABI.h'),'virtual_entries':358,'owned_entries':owned,
 'guarded_backend_leaves':[i for i in pure if i not in (213,337)],'bindings':{n:hex(v) for n,v in bindings.items()},
 'archive_sha256':sha(archive),'archive_bytes':archive.stat().st_size,
 'source_sha256':{f.name:sha(f) for f in snap.iterdir()},'strong_imports_resolved':sorted(strong-defined),
 'isolated_driver_delta':'Trace slot int32+1 widened to int64 before add; keeps signed-overflow sanitizer and avoids absent UBSan helper. No existing driver file modified.',
 'optional_unresolved_imports':sorted(weak-defined-kernel_defined),'no_source_tree_or_phone_writes':True,
 'required_scanout_yield_missing_in_current_observer':True,'gpu_or_direct_scanout_options_refused':True}
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
(HERE/'compile-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['status','archive_sha256','archive_bytes','virtual_entries','optional_unresolved_imports']},indent=2))
