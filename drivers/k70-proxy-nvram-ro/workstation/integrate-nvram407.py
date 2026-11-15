"""Offline407 over394: reviewed immutable399 + scoped401v2 + ordered403.

All three required together. Inactive unless explicit chosen u32 selector=1.
No phone, persistent media writes, fake resource/quiet, bootenv or trust changes.
"""
from pathlib import Path
import hashlib,json,shutil,subprocess
P=Path(__file__).resolve().parent;D=Path('/mnt/d/K70-UniXNU-data');B=D/'kernel394-work';O=D/'kernel407-work';H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
gate=json.loads((D/'nvram-acceptance407.json').read_text());assert gate['status']=='PASS_OFFLINE_INTEGRATION_GATES'
assert gate['independent401_review_pass'] and gate['independent_native405_review_pass'] and not gate['hardware_tested']
for path,digest in gate['frozen_inputs'].items():assert H(path)==digest,path
c=D/'nvram-native399';core=D/'nvram-core401/v2';hook=D/'nvram-hook403'
cr=json.loads((core/'compile-review.json').read_text());hr=json.loads((hook/'compile-review.json').read_text());nr=json.loads((c/'compile-review.json').read_text())
ca=json.loads((core/'object-audit.json').read_text());ha=json.loads((hook/'object-audit.json').read_text());na=json.loads((c/'object-audit.json').read_text())
assert ca['status']=='PASS_FOUR_SCOPED_NATIVE_FUNCTIONS_ABI_UNCHANGED' and ha['status']=='PASS_TWO_SCOPED_PE_METHODS_NO_EXPORTED_ABI_CHANGE'
assert na['status']=='PASS_NATIVE_IMPORTS_AND_ATOMIC_ONLY_GETTERS' and nr['normal_builtin_constructor_preserved'] and not nr['late_Apple_initializer_shim_used']
assert json.loads((B/'main-review.json').read_text())['NECP377v4_and_counter380_unchanged']
O.mkdir(exist_ok=False)
t=(P/'build-test370-claude-gpu-kernel.py').read_text().replace('kernel370-work','kernel407-work').replace('i16-test370','i16-test407').replace('libShengUfs.stock370.a','libShengUfs.stock407.a');driver=P/'build-test407-claude-gpu-kernel.py';driver.write_text(t);scope={'__file__':str(driver),'__name__':'__main__'};exec(t[:t.index('# Only isolated copies are edited.')],scope)
for name in ('libK70GopDisplay.gpu-probe.a','libQcomXnuUsbDebug.oem.a','libK70Gpu.handoff.a','NativeLegacyDisplay.cpp','K70PanelObserve.h','K70AutorefreshFix.h'):shutil.copy2(B/name,O/name)
shutil.copy2(B/'libShengUfs.stock394.a',O/'libShengUfs.stock407.a')
lines=(B/'link.filelist').read_text().splitlines();replacements={}
for filename,new in [('IONVRAM.cpo',core/'candidate/IONVRAM.cpo'),('IOPlatformExpert.cpo',hook/'candidate/IOPlatformExpert.cpo')]:
    matches=[x for x in lines if x.endswith('/iokit/DEVELOPMENT/'+filename)];assert len(matches)==1,matches;old=matches[0];replacements[old]=str(new);lines[lines.index(old)]=str(new)
filelist=O/'link.filelist';filelist.write_text('\n'.join(lines)+'\n')
ram=D/'kernel375-work/NativeRamBlock.candidate.o';assert H(ram)=='ac868295566e6eab824a5e1564a8bf262c505f0054bfb52711f3ef5b71dcc4ee'
archive=Path(nr['archive']);assert H(archive)==nr['archive_sha256']
inputs={x:H(x) for x in lines};inputs[str(ram)]=H(ram);inputs[str(archive)]=H(archive)
for name in ('libK70GopDisplay.gpu-probe.a','libQcomXnuUsbDebug.oem.a','libK70Gpu.handoff.a','libShengUfs.stock407.a'):inputs[str(O/name)]=H(O/name)
baseline=json.loads((P/'i16-test394-kernel-result.json').read_text());scope['report'].update({k:v for k,v in baseline.items() if k not in ('steps','native','kernel','status','frozen_input_sha256')});scope['report'].update(integration='Offline407 over reviewed394: genuine immutable399 proxy controller, scoped401v2 truthfulRO guards and ordered403 enrollment/options hooks; defaultOFF explicitchosen selector only.',frozen_input_sha256=inputs,offline_only=True,device_accessed=False)
scope.update(filelist=filelist,bsd=D/'kernel295-work/bsd_init-gpu.c',serial=D/'counter380-work/serial',GLUE=O/'libK70Gpu.handoff.a')
tail=t[t.index('flags=shlex.split('):];line="run('append-wifi-observer',['/usr/lib/llvm-18/bin/llvm-ar','r',OUT/'libK70GopDisplay.gpu-probe.a',Path('/mnt/d/K70-UniXNU-data/wifi-readonly-build/K70WifiReadOnly.o')],OUT,env)";assert tail.count(line)==1;tail=tail.replace(line,'# Exact394 archive retained')
anchor="archives=shlex.split((NATIVE/'mach.development.bcm2837.link/mach.development.bcm2837.linkarchives').read_text())";assert tail.count(anchor)==1
tail=tail.replace(anchor,anchor+"\nram_original=TREE/'build/ram-block/NativeRamBlock.o'\nassert archives.count(str(ram_original))==1\nassert sha(ram_original)=='f9fee59621848dd6a23f6ca25c8668fb6e8380663526a581a506617a8a6b517e'\narchives[archives.index(str(ram_original))]="+repr(str(ram))+"\narchives+=['-Wl,-force_load,'+"+repr(str(archive))+"]")
exec(tail,scope);assert all(H(x)==h for x,h in inputs.items())
for name in ('libK70GopDisplay.gpu-probe.a','libQcomXnuUsbDebug.oem.a','libK70Gpu.handoff.a'):assert H(O/name)==H(B/name)
assert H(O/'libShengUfs.stock407.a')==H(B/'libShengUfs.stock394.a')
dp=P/'build-test407-gpu-diagnostics.py';dp.write_text((P/'build-test370-gpu-diagnostics.py').read_text().replace('kernel370','kernel407').replace('i16-test370','i16-test407'))
with (O/'diagnostic407.log').open('x') as f:subprocess.run(['python3',str(dp),'--usb-log'],stdout=f,stderr=subprocess.STDOUT,check=True)
proof=dict(parent=394,only_two_direct_objects_replaced=True,replacements={k:{'path':v,'sha256':H(v)} for k,v in replacements.items()},only_added_archive=str(archive),added_archive_sha256=H(archive),all_other_direct_objects_identical394=True,all_parent_archives_unchanged=True,NECP377v4_RAM375_counter380_preserved=True,radio_GPU_WRM_BPF_TKIP_unchanged=True,permissions_nonce_bootenv_owner_trust_unchanged=True,truthful_readonly_no_persistence=True,default_off_explicit_chosen_only=True,independent_reviews=True,acceptance_sha256=H(D/'nvram-acceptance407.json'),hardware_tested=False)
(O/'main-review.json').write_text(json.dumps(proof,indent=2)+'\n');print('407 linked/prelinked/storage diagnostics PASS; startup table and packaging review still required; NO device access')
