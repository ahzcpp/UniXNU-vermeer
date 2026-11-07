#!/usr/bin/env python3
"""Linux port of UniXNU scripts/build-sheng-hid-input.py for the K70 kernel, plus HidInputProbe and glue.

Checks the original IOHIDFamily against a built K70 kernel (same 21H461 kernelcache), writes the
IOHIDDevice ABI header, compiles NativeHidInput.cpp, binds the original IOHIDDevice methods, stages
the deferred metaclass initializer, and compiles HidInputProbe.cpp and k70-input-glue.c. No phone I/O.
Usage: build-hid-objects.py <reference kernel-payload.macho> <out dir>
"""
import hashlib,json,os,struct,subprocess,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=Path('/root/k70-clean-20261104');WORK=ROOT/'storage-candidate-20261104'
TREE=WORK/'out/vermeer/ipados-17.7.11-21H461/vermeer/tree';NATIVE=TREE/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
DEVICE=WORK/'device';CORE=ROOT/'UniXNU'
HID_SRC=Path('/root/survey-20261105/touch/ShengHidInput/NativeHidInput.cpp')
HID_REV='71a40e76daeae1bb4127546f96ffa7339c7a06ff'
CAND=ROOT/'display-candidate-20261104'
def require(v,why):
    if not v:raise SystemExit('FAIL: '+why)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
ref=Path(sys.argv[1]);OUT=Path(sys.argv[2]);OUT.mkdir(parents=True,exist_ok=True)
require(subprocess.check_output(['git','-C',HID_SRC.parent,'rev-parse','HEAD'],text=True).strip()==HID_REV,'pinned ShengHidInput revision')
sys.path.insert(0,str(CORE/'kits/XnuPrelink'))
from native_prelink import macho,backed_offset,exports
b=ref.read_bytes();plan=json.loads(ref.with_suffix('.json').read_text());m=macho(b)
require(hashlib.sha256(b).hexdigest()==plan['candidate_sha256'],'reference kernel matches its plan')
HID_TEXT_SHA='7e4c5104814f2e773d5be13d092dec53ee44ba5a61a81264b229780406e86b72'
require(plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily']==HID_TEXT_SHA,'original IOHIDFamily (21H461)')
def read(a,n):
    at=backed_offset(m['segments'],a,n,len(b));return b[at:at+n]
names={n:a for a,ns in exports(b,m).items() for n in ns}
e=next(e for e in plan['extents'] if e['module']=='com.apple.iokit.IOHIDFamily' and e['segment']=='__TEXT_EXEC')
require(hashlib.sha256(read(e['va'],e['size'])).hexdigest()==HID_TEXT_SHA,'complete original HID instructions unchanged in this kernel')
vptr=0xfffffff005d045c0;methods=struct.unpack('<207Q',read(vptr,207*8))
base=struct.unpack('<168Q',read(names['__ZTV9IOService']+16,168*8))
changed=[i for i in range(168) if methods[i]!=base[i]]
require(changed==[0,1,7,12,14,21,23,76,81,82,86,87,91,92,93,101,132,140],'exact original HID native-base overrides')
require([i for i,a in enumerate(methods) if a==names['___cxa_pure_virtual']]==[180] and all(methods) and read(vptr+207*8,24)==bytes(24),'exact original 207-slot abstract device')
require(read(0xfffffff004ad94f8,0x20)==bytes.fromhex('fd7bbfa9fd0300912bd90094489100f008011791080000f9fd7bc1a8c0035fd6'),'original protected constructor')
require(struct.unpack('<Q',read(0xfffffff005d00f10,8))[0]==names['__ZN9IOServiceC2EPK11OSMetaClass'],'native protected constructor binding')
require(struct.unpack('<Q',read(0xfffffff005d00f18,8))[0]==names['__ZN9IOServiceD2Ev'],'native protected destructor binding')
require(read(0xfffffff004ad9aec,0x34)==bytes.fromhex('680240f908a142f9e00313aae10314aa00013fd6680240f9603d003608d142f9e1630091e00313aa00013fd6a02d0035e00f40f9'),'original handleStart/report-descriptor ABI')
require(methods[181]==0xfffffff004add0c4 and read(0xfffffff004add110,28)==bytes.fromhex('680240f9081543f9e00313aae20316aae30315aae40314aa00013fd6'),'original handleReport')
header='''// Generated from the qualified original image; opaque private storage only.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
class IOHIDDevice : public IOService {
 OSDeclareAbstractStructors(IOHIDDevice)
 unsigned char originalPrivate[0xd0-sizeof(IOService)];
public:
 using IOService::setProperty;
 IOReturn Dispatch(IORPC) override;
 void free() override;
 bool init(OSDictionary *properties=nullptr) override;
 bool setProperty(const OSSymbol *,OSObject *) override;
 bool willTerminate(IOService *,IOOptionBits) override;
 IOReturn configureReport(IOReportChannelList *,IOReportConfigureAction,void *,void *) override;
 IOReturn updateReport(IOReportChannelList *,IOReportUpdateAction,void *,void *) override;
 bool start(IOService *) override;
 void stop(IOService *) override;
 bool handleOpen(IOService *,IOOptionBits,void *) override;
 void handleClose(IOService *,IOOptionBits) override;
 bool handleIsOpen(const IOService *) const override;
 bool matchPropertyTable(OSDictionary *,SInt32 *) override;
 IOReturn message(UInt32,IOService *,void *) override;
 IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **) override;
'''
for i in range(168,207):
    if i==171:header+=' virtual OSString *newTransportString() const;\n'
    elif i==173:header+=' virtual OSString *newProductString() const;\n'
    elif i==180:header+=' virtual IOReturn newReportDescriptor(IOMemoryDescriptor **) const=0;\n'
    elif i==181:header+=' virtual IOReturn handleReport(IOMemoryDescriptor *,unsigned,IOOptionBits);\n'
    else:header+=f' virtual __attribute__((deprecated("Unqualified prototype: never call from source"))) void opaque{i}();\n'
header+='};\nstatic_assert(sizeof(IOHIDDevice)==0xd0,"Original device instance size");\n'
(OUT/'OriginalHidDeviceABI.h').write_text(header)

# Compile with the recorded IOKit C++ command of the display provider (same kernel, same flags).
os.environ['UNIXNU_TOOLCHAIN']=str(WORK/'toolchain-view');sys.path.insert(0,str(CORE/'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import kernel
ctx=Context(parser().parse_args(['build','kernel','-D',str(DEVICE),'--out',str(WORK/'out'),'--kernelcache',str(ROOT/'out/ipsw/21H461/kernelcache.decompressed'),'--jobs','4']))
env=dict(os.environ,**kernel.linux_env(ctx))
pcmd=json.loads((CAND/'K70DisplayProvider-command.json').read_text())
def compile_cpp(src,obj,extra=()):
    cmd=[str(src) if a==str(CAND/'source/K70DisplayProvider.cpp') else str(obj) if a==str(CAND/'K70DisplayProvider.o') else a for a in pcmd]
    require(str(src) in cmd and str(obj) in cmd,'command template substitution')
    cmd+=['-I',str(OUT),'-I',str(CAND/'source'),*extra]
    r=subprocess.run(cmd,cwd=NATIVE/'iokit/DEVELOPMENT',env=env,capture_output=True,text=True)
    require(r.returncode==0,f'compile {src.name}:\n'+r.stdout[-3000:]+r.stderr[-3000:])
obj=OUT/'NativeHidInput.o';compile_cpp(HID_SRC,obj)
raw=obj.read_bytes();om=macho(raw)
st=next(p for c,p,_ in om['commands'] if c==2);sy,count,strings,_=struct.unpack_from('<4I',raw,st+8);symbols=[]
for i in range(count):
    ix,kind,section,_,va=struct.unpack_from('<IBBHQ',raw,sy+i*16)
    symbols.append((raw[strings+ix:raw.index(b'\0',strings+ix)].decode(),va,kind))
vt=next(va for name,va,_ in symbols if name=='__ZTV14NativeHidInput');relocations={}
for segment in om['segments']:
    for sec in segment['sections']:
        if sec['va']<=vt<sec['va']+sec['size']:
            ro,rc=struct.unpack_from('<2I',raw,sec['command_offset']+56)
            for i in range(rc):
                offset,bits=struct.unpack_from('<2I',raw,ro+i*8)
                if bits>>27&1 and bits>>28==0 and bits>>25&3==3:relocations[sec['va']+offset]=symbols[bits&0xffffff][0]
compiled=[relocations.get(vt+16+i*8) for i in range(207)]
require(all(compiled) and vt+16+207*8 not in relocations,'compiled exact 207-slot vtable')
bindings={'__ZN11IOHIDDeviceC2EPK11OSMetaClass':0xfffffff004ad94f8,'__ZN11IOHIDDeviceD2Ev':0xfffffff004ad9518,'__ZN11IOHIDDevice10gMetaClassE':0xfffffff006ae6fa0,'__ZTV11IOHIDDevice':vptr-16}
owned=[]
for i,name in enumerate(compiled):
    if name.startswith(('__ZN14NativeHidInput','__ZNK14NativeHidInput')):owned.append(i)
    elif '11IOHIDDevice' in name:bindings[name]=methods[i]
    else:require(i<168 and names.get(name)==methods[i],'actual inherited native method '+str(i))
require(owned==[0,1,7,14,86,87,111,171,173,180],'only qualified HID methods overridden')
undefined={n for n,_,k in symbols if k==1 and '11IOHIDDevice' in n}
require(undefined==set(bindings),'all and only actual original HID imports bound')
asm=OUT/'hid-original-bindings.S';asm.write_text(''.join(f'.globl {n}\n.set {n}, {a:#x}\n' for n,a in sorted(bindings.items())))
clang=ROOT/'toolchain/llvm/bin/clang-24'
subprocess.run([clang,'--target=arm64-apple-ios17.0','-c',asm,'-o',OUT/'hid-original-bindings.o'],check=True,env=env)
late=OUT/'NativeHidInput.late.o'
# llvm-objcopy cannot globalize Mach-O symbols; the same stager already used for the display and
# AppleKeyStore objects removes __mod_init_func and exports the initializer.
sys.path.insert(0,str(HERE.parent/'display-next/reference'))
from coachz_late_init import stage_initializer
late.write_bytes(stage_initializer(raw,'__GLOBAL__sub_I_NativeHidInput.cpp'))
def code(data,image):
    s=next(s for g in image['segments'] for s in g['sections'] if s['name']=='__text');return data[s['fileoff']:s['fileoff']+s['size']]
require(code(raw,om)==code(late.read_bytes(),macho(late.read_bytes())),'owned code unchanged by deferred metaclass registration')

# HidInputProbe: auto Home is gated on presented frames; this kernel counts them in k70_display_transfers.
probe=(DEVICE/'package/HidInputProbe.cpp').read_text()
old='extern volatile uint32_t native_legacy_transfer_calls;\n'
require(probe.count(old)==1 and probe.count('native_legacy_transfer_calls<2')==1,'HidInputProbe frame gate')
probe=probe.replace(old,'extern volatile uint64_t k70_display_transfers;\n').replace('native_legacy_transfer_calls<2','k70_display_transfers<2')
(OUT/'HidInputProbe.cpp').write_text(probe)
compile_cpp(OUT/'HidInputProbe.cpp',OUT/'HidInputProbe.o')
glue_obj=OUT/'k70-input-glue.o'
r=subprocess.run([clang,'--target=arm64-apple-ios17.0','-mkernel','-fno-builtin','-O2','-Wall','-Werror','-c',HERE/'k70-input-glue.c','-o',glue_obj],env=env,capture_output=True,text=True)
require(r.returncode==0,'glue compile:\n'+r.stderr[-3000:])
report={'reference_kernel_sha256':plan['candidate_sha256'],'original_hid_text_sha256':HID_TEXT_SHA,'owned_slots':owned,'bindings':{k:hex(v) for k,v in bindings.items()},
 'objects':{p.name:sha(p) for p in (late,OUT/'hid-original-bindings.o',OUT/'HidInputProbe.o',glue_obj)},'hid_source_revision':HID_REV,'runtime_qualified':False}
(OUT/'hid-objects.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original HID identity, exact owned vtable, bindings; HidInputProbe and glue compiled')
print(json.dumps(report['objects'],indent=2))
