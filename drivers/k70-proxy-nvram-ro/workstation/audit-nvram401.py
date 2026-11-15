"""Audit the four scoped native functions, exports/imports and read-only ABI data."""
from pathlib import Path
import hashlib,json,struct,subprocess
P=Path(__file__).resolve().parent;O=Path('/mnt/d/K70-UniXNU-data/nvram-core401/v2')
s=(P/'research374/compare-necp-objects377.py').read_text();scope={'__file__':str(P/'research374/compare-necp-objects377.py')};exec(s[:s.index('candidate=next(')],scope)
funcs=scope['funcs'];original=scope['old'].parents[2]/'iokit/DEVELOPMENT/IONVRAM.cpo'
bpath=O/'baseline/IONVRAM.cpo';cpath=O/'candidate/IONVRAM.cpo'
a,b,c=map(funcs,(original,bpath,cpath))
expected={'__ZN18IONVRAMCHRPHandler18serializeVariablesEv','__ZN9IODTNVRAM12syncInternalEb','__ZN9IODTNVRAM26setPropertyWithGUIDAndNameEPKhPKcP8OSObject','__ZN9IODTNVRAM29removePropertyWithGUIDAndNameEPKhPKc'}
def nm(path,option,external=False):
    lines=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm',option,*(['-g'] if external else []),str(path)],text=True).splitlines()
    return sorted(x.split()[-1] for x in lines if x.strip())
def sections(path):
    raw=path.read_bytes();at=32;out={}
    for _ in range(struct.unpack_from('<I',raw,16)[0]):
        cmd,size=struct.unpack_from('<II',raw,at)
        if cmd==0x19:
            for i in range(struct.unpack_from('<I',raw,at+64)[0]):
                off=at+72+80*i;name=raw[off:off+16].rstrip(b'\0').decode();va,n,fo,align,ro,rn=struct.unpack_from('<QQIIII',raw,off+32)
                segment=raw[off+16:off+32].rstrip(b'\0').decode()
                if name not in ('__const','__data','__kalloc_type'):continue
                data=bytearray(raw[fo:fo+n]);relocations=[]
                for j in range(rn):
                    start,v=struct.unpack_from('<iI',raw,ro+j*8);width=1<<((v>>25)&3)
                    assert 0<=start<=len(data)-width;data[start:start+width]=bytes(width);relocations.append((start,width,(v>>28)&15))
                out[segment+':'+name]={'bytes':n,'relocation_masked_sha256':hashlib.sha256(data).hexdigest(),'relocation_layout':sorted(relocations)}
        at+=size
    return out
au,bu,cu=(set(nm(p,'-u')) for p in (original,bpath,cpath))
report=dict(status='AUDIT_PENDING',hardware_tested=False,actual_functions=len(a),baseline_functions=len(b),candidate_functions=len(c),
 actual_to_baseline_changed=sorted(k for k in a.keys()&b.keys() if a[k]!=b[k]),
 baseline_to_candidate_changed=sorted(k for k in b.keys()&c.keys() if b[k]!=c[k]),
 new_functions=sorted(c.keys()-b.keys()),removed_functions=sorted(b.keys()-c.keys()),
 added_imports=sorted(cu-bu),removed_imports=sorted(bu-cu),
 exported_symbol_names_unchanged=nm(bpath,'--defined-only',True)==nm(cpath,'--defined-only',True),
 data_sections_equal=sections(bpath)==sections(cpath),data_sections=sections(cpath),
 object_sha256=hashlib.sha256(cpath.read_bytes()).hexdigest(),
 limitation='Relocation-masked byte comparison is not full semantic equivalence; bank/registration/device not exercised')
assert not report['actual_to_baseline_changed'] and a.keys()==b.keys()
assert set(report['baseline_to_candidate_changed'])==expected
assert not report['new_functions'] and not report['removed_functions']
assert report['added_imports']==['_k70_nvram_ro_for_provider399'] and not report['removed_imports']
assert report['exported_symbol_names_unchanged'] and report['data_sections_equal']
assert au==bu
report['status']='PASS_FOUR_SCOPED_NATIVE_FUNCTIONS_ABI_UNCHANGED'
(O/'object-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='data_sections'},indent=2))
