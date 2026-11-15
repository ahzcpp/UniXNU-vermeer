"""Compile a reviewed frozen399 snapshot with built-in controller target recipe.

Use normal native built-in C++ initialization. No late-Apple superclass shim,
bank/phone I/O, source tree edits or registration is performed by this build.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
D=Path('/mnt/d/K70-UniXNU-data');I=D/'wifi-agent322/work/nvram-controller399';O=D/'nvram-native399';O.mkdir(exist_ok=True)
assert not (O/'compile-review.json').exists(),'Frozen completed build'
assert (I/'READY.json').exists(),'Wait for reviewed399 freeze marker'
ready=json.loads((I/'READY.json').read_text());host=json.loads((I/'host-out/test-results.json').read_text())
H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert host['status']=='PASS_HOST_ASAN_UBSAN' and host['source_sha256']==H(I/'src/NativeProxyNVRAM399.cpp') and host['core_sha256']==H(I/'src/ProxyBank399.h')
source_dir=O/'source';source_dir.mkdir(exist_ok=True);inputs={}
for path in (I/'src').iterdir():
    if path.is_file():inputs[path.name]=H(path);shutil.copy2(path,source_dir/path.name)
assert all(H(source_dir/n)==h==H(I/'src'/n) for n,h in inputs.items())
T=Path('/root/k70-clean-20261104/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/tree');SRC=T/'.cache/xnu-work';N=T/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837';entries=[]
for line in (N/'compile_commands.json').read_text().splitlines():
    line=line.strip().rstrip('$').strip().rstrip(',')
    if line.startswith('{'):entries.append(json.loads(line))
e=next(e for e in entries if e['file']==str(SRC/'iokit/Families/IONVRAM/IONVRAMController.cpp'));a=e['arguments'].copy();a[a.index(e['file'])]=str(source_dir/'NativeProxyNVRAM399.cpp');obj=O/'NativeProxyNVRAM399.o';a[a.index('-o')+1]=str(obj)
if '-MF' in a:a[a.index('-MF')+1]=str(O/'NativeProxyNVRAM399.d')
a+=['-DIOKIT_ENABLE_SHARED_PTR','-iquote',str(SRC/'iokit/Families/IONVRAM'),'-Wno-unsafe-buffer-usage']
env=dict(os.environ);env.pop('CCC_OVERRIDE_OPTIONS',None)
with (O/'compile.log').open('w') as f:r=subprocess.run(a,cwd=e['directory'],env=env,stdout=f,stderr=subprocess.STDOUT)
if r.returncode:print((O/'compile.log').read_text()[-12000:]);raise SystemExit(r.returncode)
archive=O/'libK70ProxyNVRAM399.a';assert not archive.exists();subprocess.run(['/usr/lib/llvm-18/bin/llvm-ar','rcs',str(archive),str(obj)],check=True)
nm=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm','--defined-only',str(obj)],text=True);exports=sorted(x.split()[-1] for x in nm.splitlines() if x.strip())
assert any('GLOBAL__sub_I_NativeProxyNVRAM399.cpp' in x for x in exports)
for n in ('_k70_nvram_ro_create399','_k70_nvram_ro_for_provider399','_k70_nvram_ro_active399','_k70_nvram_ro_counters399'):assert n in exports
assert all(H(source_dir/n)==h==H(I/'src'/n) for n,h in inputs.items()),'Frozen input changed'
report=dict(status='PASS_NATIVE_BUILTIN_CONTROLLER_COMPILE',hardware_tested=False,source_hashes=inputs,worker_ready_sha256=H(I/'READY.json'),worker_host_proof_sha256=H(I/'host-out/test-results.json'),object=str(obj),object_sha256=H(obj),archive=str(archive),archive_sha256=H(archive),arguments=a,directory=e['directory'],normal_builtin_constructor_preserved=True,late_Apple_initializer_shim_used=False,limitations='Actual target compile only. No kernel link, startup order, real binding, device or persistence acceptance.')
(O/'compile-review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ('arguments','source_hashes')},indent=2))
