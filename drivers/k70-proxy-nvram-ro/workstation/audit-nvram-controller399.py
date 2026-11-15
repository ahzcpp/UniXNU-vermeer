"""Check native399 imports resolve against394 and getters are atomic reads only."""
from pathlib import Path
import hashlib,json,re,subprocess
D=Path('/mnt/d/K70-UniXNU-data');O=D/'nvram-native399';obj=O/'NativeProxyNVRAM399.o';native=D/'kernel394-work/mach.i16.unstripped.noctf'
nm=lambda p,args:subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm',*args,str(p)],text=True)
imports=set(x.split()[-1] for x in nm(obj,['-u']).splitlines() if x.strip());defined=set(x.split()[-1] for x in nm(native,['--defined-only']).splitlines() if x.strip())
assert not imports-defined,sorted(imports-defined)
assert not any('atomic_' in n or 'sync_' in n for n in imports)
asm=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-objdump','--macho','--disassemble','--no-show-raw-insn',str(obj)],text=True)
getters={}
for name in ('_k70_nvram_ro_active399','_k70_nvram_ro_for_provider399'):
    assert asm.count(name+':\n')==1
    text=re.split(r'\n[^\s][^\n]*:\n',asm.split(name+':\n',1)[1],1)[0]
    ops=[line.split(':',1)[1].strip().split()[0] for line in text.splitlines() if re.match(r'^\s*[0-9a-f]+:\s*\S',line)]
    assert ops and set(ops)<={'adrp','add','ldar','cmp','cset','ret','cbz','mov'}
    assert 'ldar' in ops and 'ret' in ops
    (O/(name+'.asm.txt')).write_text(text)
    getters[name]={'instructions':len(ops),'mnemonics':ops,'calls_stores_locks_allocations':False,'asm_sha256':hashlib.sha256(text.encode()).hexdigest()}
report=dict(status='PASS_NATIVE_IMPORTS_AND_ATOMIC_ONLY_GETTERS',hardware_tested=False,object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),imports=len(imports),all_imports_defined_in_native394=True,runtime_atomic_helper_imports=False,getters=getters,limitations='Object and symbol resolution only. Full linking and native constructor/startup/binding verification still required; no device test.')
(O/'object-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
