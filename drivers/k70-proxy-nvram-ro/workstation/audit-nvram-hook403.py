"""Confirm only two PE methods changed, no exported ABI and only399 imports."""
from pathlib import Path
import hashlib,json,subprocess
P=Path(__file__).resolve().parent;O=Path('/mnt/d/K70-UniXNU-data/nvram-hook403')
s=(P/'research374/compare-necp-objects377.py').read_text();scope={'__file__':str(P/'research374/compare-necp-objects377.py')};exec(s[:s.index('candidate=next(')],scope)
f=scope['funcs'];actual=scope['old'].parents[2]/'iokit/DEVELOPMENT/IOPlatformExpert.cpo'
a,b,c=(f(p) for p in (actual,O/'baseline/IOPlatformExpert.cpo',O/'candidate/IOPlatformExpert.cpo'))
def nm(path,option,external=False):return set(x.split()[-1] for x in subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm',option,*(['-g'] if external else []),str(path)],text=True).splitlines() if x.strip())
bpath=O/'baseline/IOPlatformExpert.cpo';cpath=O/'candidate/IOPlatformExpert.cpo';bu=nm(bpath,'-u');cu=nm(cpath,'-u')
report=dict(status='AUDIT_PENDING',hardware_tested=False,function_counts=[len(a),len(b),len(c)],actual_to_baseline_changed=sorted(k for k in a.keys()&b.keys() if a[k]!=b[k]),baseline_to_candidate_changed=sorted(k for k in b.keys()&c.keys() if b[k]!=c[k]),new_functions=sorted(c.keys()-b.keys()),removed_functions=sorted(b.keys()-c.keys()),added_imports=sorted(cu-bu),removed_imports=sorted(bu-cu),exported_names_unchanged=nm(bpath,'--defined-only',True)==nm(cpath,'--defined-only',True),object_sha256=hashlib.sha256(cpath.read_bytes()).hexdigest(),limitations='Relocation-masked equality is not full semantic equivalence; no runtime ordering/device proof')
assert not report['actual_to_baseline_changed'] and a.keys()==b.keys()
assert set(report['baseline_to_candidate_changed'])=={'__ZN18IODTPlatformExpert15processTopLevelEP15IORegistryEntry','__ZN18IODTPlatformExpert9createNubEP15IORegistryEntry'}
assert not report['new_functions'] and not report['removed_functions'] and report['exported_names_unchanged']
assert report['added_imports']==['_k70_nvram_ro_create399','_k70_nvram_ro_for_provider399'] and not report['removed_imports']
report['status']='PASS_TWO_SCOPED_PE_METHODS_NO_EXPORTED_ABI_CHANGE'
(O/'object-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
