"""Isolated native PE hooks: deterministic enrollment and preserve real /options.

Requires reviewed399+401 together. No bank writes, manual resource publication,
waits, private offsets, matching timeout changes, or shared source edits.
"""
from pathlib import Path
import difflib,hashlib,json,os,subprocess
O=Path('/mnt/d/K70-UniXNU-data/nvram-hook403');O.mkdir(exist_ok=True)
assert not (O/'compile-review.json').exists(),'Frozen completed build'
T=Path('/root/k70-clean-20261104/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/tree');SRC=T/'.cache/xnu-work';N=T/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
original=SRC/'iokit/Kernel/IOPlatformExpert.cpp';s=original.read_text();candidate=s
def before(anchor,addition,within=None):
    global candidate
    if within:
        at=s.index(anchor,s.index(within));pos=candidate.index(anchor,candidate.index(within))
    else:
        assert s.count(anchor)==candidate.count(anchor)==1
        at=s.index(anchor);pos=candidate.index(anchor)
    line=s[:at].count('\n')+1
    candidate=candidate[:pos]+addition+'\n#line '+str(line)+' "IOPlatformExpert.cpp"\n'+candidate[pos:]
anchor='void printDictionaryKeys(OSDictionary * inDictionary, char * inMsg);'
# The header is included by this file; exact classes/layouts remain untouched.
before(anchor,'extern "C" SInt32 k70_nvram_ro_create399(IOService *provider);\nextern "C" bool k70_nvram_ro_for_provider399(const void *provider);')
anchor='\t// Publish the cpus.\n'
before(anchor,'''
    // dtNVRAM was assigned above. Invoke before top-level nubs can take over
    // /options. An absent, malformed or non-1 chosen selector does NOTHING.
    IORegistryEntry *k70Chosen399 = IORegistryEntry::fromPath("/chosen", gIODTPlane);
    if (k70Chosen399) {
        OSData *selector = OSDynamicCast(OSData, k70Chosen399->getProperty("k70-nvram-proxy-ro"));
        if (selector && selector->getLength() == 4 && selector->getBytesNoCopy()) {
            const UInt8 *v = static_cast<const UInt8 *>(selector->getBytesNoCopy());
            if (v[0] == 1 && v[1] == 0 && v[2] == 0 && v[3] == 0) {
                SInt32 result = k70_nvram_ro_create399(dtNVRAM);
                IOLog("k70:nvram399 enrollment=%08x active=%u\\n", static_cast<UInt32>(result),
                    k70_nvram_ro_for_provider399(dtNVRAM) ? 1u : 0u);
            }
        }
        k70Chosen399->release();
    }
''')
anchor='\tnub = new IOPlatformDevice;'
before(anchor,'''
    // Do not turn the real, successfully bound IODTNVRAM /options object
    // into an empty IOPlatformDevice. All other nubs and feature-OFF retain
    // their original path; no registry links or variable contents are copied.
    if (k70_nvram_ro_for_provider399(from)) return NULL;
''',within='IODTPlatformExpert::createNub(')
entries=[]
for line in (N/'compile_commands.json').read_text().splitlines():
    line=line.strip().rstrip('$').strip().rstrip(',')
    if line.startswith('{'):entries.append(json.loads(line))
entry=next(e for e in entries if e['file']==str(original));env=dict(os.environ);env.pop('CCC_OVERRIDE_OPTIONS',None)
H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
report=dict(hardware_tested=False,shared_source_tree_modified=False,original_source_sha256=H(original),compiles={})
for mode,text in [('baseline',s),('candidate',candidate)]:
    folder=O/mode;folder.mkdir(exist_ok=True);source=folder/'IOPlatformExpert.cpp';source.write_text(text);obj=folder/'IOPlatformExpert.cpo'
    a=entry['arguments'].copy();assert a.count(str(original))==1;a[a.index(str(original))]=str(source);a[a.index('-o')+1]=str(obj)
    if '-MF' in a:a[a.index('-MF')+1]=str(folder/'IOPlatformExpert.d')
    a+=['-iquote',str(original.parent),'-Wno-unsafe-buffer-usage']
    with (folder/'build.log').open('w') as f:r=subprocess.run(a,cwd=entry['directory'],env=env,stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:print((folder/'build.log').read_text()[-7000:]);raise SystemExit(r.returncode)
    report['compiles'][mode]=dict(source_sha256=H(source),object=str(obj),object_sha256=H(obj),arguments=a,directory=entry['directory'])
(O/'IOPlatformExpert.cpp.patch').write_text(''.join(difflib.unified_diff(s.splitlines(True),candidate.splitlines(True),fromfile='a/iokit/Kernel/IOPlatformExpert.cpp',tofile='b/iokit/Kernel/IOPlatformExpert.cpp')))
(O/'compile-review.json').write_text(json.dumps(report,indent=2)+'\n')
print('403 target PE baseline/candidate compiled; review pending, no integration/device')
