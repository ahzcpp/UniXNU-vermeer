"""DRAFT: genuine Mac/iPhoneOS Turnip-only build; never creates a Metal bundle."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys
O=Path(__file__).resolve().parent
def H(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(*args,**kw):return subprocess.run([str(a) for a in args],check=True,**kw)
def read(*args):return subprocess.check_output([str(a) for a in args],text=True).strip()
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--root',type=Path,required=True);ap.add_argument('--delivery',type=Path,required=True);ap.add_argument('--check-only',action='store_true');a=ap.parse_args()
 R=a.root.resolve();manifest=json.loads((O/'source-inputs.json').read_text())
 assert read('git','-C',R,'rev-parse','HEAD')==manifest['core_commit']
 for name,want in manifest['base_files'].items():assert H(R/name)==want,name
 pinfile=R/'scripts/data/adreno-compiler-inputs.json'
 assert H(pinfile)==manifest['compiler_inputs_sha256']
 pins=json.loads(pinfile.read_text());assert pins['mesa']==manifest['mesa']
 assert pins['patches']==[r['path'] for r in manifest['core_patches']]
 for r in manifest['core_patches']:assert H(R/r['path'])==r['sha256']
 extra=manifest['extra_patch'];assert H(O/extra['path'])==extra['sha256']
 # Pinned submodule commit, not a substitute checkout of a similarly named ABI.
 gitlink=read('git','-C',R,'ls-tree','HEAD','drivers/Adreno740').split()[2]
 assert read('git','-C',R/'drivers/Adreno740','rev-parse','HEAD')==gitlink
 if a.check_only:
  print('PASS draft source inputs only; no writes/build/native execution');return
 assert sys.platform=='darwin','genuine macOS+iPhoneOS SDK is required'
 assert not a.delivery.exists(),'never overwrite an existing delivery'
 sdk=read('xcrun','--sdk','iphoneos','--show-sdk-path');assert Path(sdk).is_dir()
 changes=[]
 # Exactly the same17.0->16.7 Turnip build-script edit as existing446 workflow.
 builder=R/'scripts/build-turnip-darwin.py';old=builder.read_text()
 assert old.count('arm64-apple-ios17.0')==1
 new=old.replace('17.0','16.7');builder.write_text(new)
 changes.append({'file':str(builder.relative_to(R)),'before':hashlib.sha256(old.encode()).hexdigest(),'after':H(builder),'reason':'iOS16.7 deployment only; unchanged builder otherwise'})
 # Git2.55 new-file metadata normalization, same as current full workflow.
 for r in manifest['core_patches']:
  p=R/r['path'];old=p.read_text();new=re.sub(r'(^diff --git a/(\S+) b/\2\n)(?=--- /dev/null\n)',r'\1new file mode 100644\n',old,flags=re.M)
  if new!=old:
   assert new.replace('new file mode 100644\n','')==old.replace('new file mode 100644\n','')
   p.write_text(new);changes.append({'file':r['path'],'metadata_only':True,'before':r['sha256'],'after':H(p)})
 target=R/extra['record_as'];assert not target.exists();shutil.copyfile(O/extra['path'],target)
 assert extra['record_as'] not in pins['patches'];pins['patches'].append(extra['record_as']);pinfile.write_text(json.dumps(pins,indent=2)+'\n')
 # Reuse exact existing checkout() and validate_recorded_patches() contract.
 # Deliberately do NOT call build_plugin(), translator checkout, bridge, Rust,
 # shader/MSL generation, Metal frontend build, make-gpu-addon or firmware fetch.
 sys.path.insert(0,str(R/'scripts'))
 spec=importlib.util.spec_from_file_location('runtime_source_contract',R/'scripts/build-boot-runtime.py');runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)
 mesa=R/'.cache/mesa-work';fresh=runtime.checkout(mesa,pins['mesa'])
 if fresh:
  for name in pins['patches']:runtime.run('git','-C',mesa,'apply',R/name)
 runtime.validate_recorded_patches(R,mesa,pins)
 for name,want in manifest['extra_source_outputs'].items():assert H(mesa/name)==want,name
 venv=R/'.cache/mesa-venv'
 if not (venv/'bin/python').is_file():runtime.run(sys.executable,'-m','venv',venv)
 runtime.run(venv/'bin/python','-m','pip','install','-r',R/'scripts/requirements-adreno-compiler.txt')
 try:runtime.script('build-turnip-darwin.py','--target','ios')
 except BaseException:
  for p in sorted((R/'.cache').glob('mesa-tu-ios-*.log')):print(p.name+'\n'+p.read_text(errors='replace')[-16000:],flush=True)
  raise
 built=R/'build/mesa-tu-ios';lib=built/'src/freedreno/vulkan/libvulkan_freedreno.dylib';profile=built/'builtin-build-profile.json'
 assert lib.is_file() and profile.is_file()
 prof=json.loads(profile.read_text());assert prof['profile']['mesa_revision']==pins['mesa']['commit'] and prof['profile']['target']=='ios'
 # Source profile is copied from ACTUAL builder; never fabricate metal_source_sha.
 a.delivery.mkdir(parents=True);out=a.delivery/'libvulkan_freedreno.dylib';shutil.copyfile(lib,out);out.chmod(0o755)
 unsigned_hash=H(out)
 run('strip','-x',out)
 run('codesign','--force','--sign','-','--pagesize','4096','--identifier','org.blu-sharky.turnip',out)
 run('codesign','--verify','--strict',out)
 detail=subprocess.run(['codesign','--display','--verbose=4',str(out)],capture_output=True,text=True,check=True).stderr
 assert re.search(r'^Page size=4096$',detail,re.M)
 match=re.search(r'^CDHash=([a-f0-9]{40})$',detail,re.M);assert match
 shutil.copyfile(profile,a.delivery/'builtin-build-profile.json')
 imports=read('xcrun','nm','-u',out);(a.delivery/'imports.txt').write_text(imports+'\n')
 exports=read('xcrun','nm','-gU',out);assert ' T _vk_icdGetInstanceProcAddr' in exports
 (a.delivery/'exports.txt').write_text(exports+'\n')
 from audit_standalone import audit
 audit_result=audit(out);assert audit_result['identifier']=='org.blu-sharky.turnip' and audit_result['primary_cdhash']==match[1]
 provenance={'status':'GENUINE_MAC_TURNIP_ONLY_SIGNED_SOURCE_OUTPUT','core_commit':manifest['core_commit'],'Adreno740_gitlink':gitlink,'source_inputs_sha256':H(O/'source-inputs.json'),'telemetry_patch_sha256':extra['sha256'],'extra_source_outputs':{n:H(mesa/n) for n in manifest['extra_source_outputs']},'actual_builtin_profile_sha256':H(profile),'builder_sha256':H(builder),'requirements_sha256':H(R/'scripts/requirements-adreno-compiler.txt'),'patched_pinfile_sha256':H(pinfile),'ordered_actual_patch_sha256':[{'path':n,'sha256':H(R/n)} for n in pins['patches']],'deployment_changes':changes,'build_before_strip_sha256':unsigned_hash,'signed_vulkan_sha256':H(out),'standalone_audit':audit_result,'sdk_version':read('xcrun','--sdk','iphoneos','--show-sdk-version'),'SDK_or_Apple_binaries_uploaded':False,'Metal_or_Rust_built_or_supplied':False,'actual20H392_import_resolution_or_runtime_proven':False,'files':{p.name:{'sha256':H(p),'bytes':p.stat().st_size} for p in a.delivery.iterdir() if p.is_file()}}
 (a.delivery/'source-artifact-audit.json').write_text(json.dumps(provenance,indent=2)+'\n')
 print('PASS Turnip-only native artifact; no Metal bundle, addon, owner trust, phone or GPU execution')
if __name__=='__main__':main()
