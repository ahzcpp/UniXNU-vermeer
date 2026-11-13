"""Inspect our own newly built bundle and deployment metadata, no device I/O."""
from pathlib import Path
import hashlib,json,plistlib,re,struct,subprocess,sys
out=Path(sys.argv[1]);bundle=out/'K70Audio.driver';p=bundle/'K70Audio';d=p.read_bytes()
assert d[:4]==b'\xcf\xfa\xed\xfe'
cpu,sub,filetype,ncmd,cmdbytes=struct.unpack_from('<5I',d,4)
assert cpu==0x100000c and sub==0 and filetype==8
at=32;builds=[];signatures=[];loads=[]
for _ in range(ncmd):
    cmd,size=struct.unpack_from('<II',d,at);assert size>=8 and at+size<=32+cmdbytes
    if cmd==0x32:
        platform,minos,sdk,ntools=struct.unpack_from('<4I',d,at+8);builds.append(dict(platform=platform,minos=minos,sdk=sdk))
        assert platform==2 and minos==0x100000
    if cmd==0x1d:
        off,n=struct.unpack_from('<2I',d,at+8);assert off<=len(d) and n<=len(d)-off;signatures.append(dict(offset=off,bytes=n))
    if cmd in (0xc,0x80000018,0x8000001f):
        nameoff,=struct.unpack_from('<I',d,at+8);assert nameoff<size
        name=d[at+nameoff:at+size].split(b'\0',1)[0].decode();assert name.startswith(('/System/Library/','/usr/lib/'));loads.append(name)
    at+=size
assert at==32+cmdbytes and len(builds)==1 and len(signatures)==1
info=plistlib.loads((bundle/'Info.plist').read_bytes());assert info['MinimumOSVersion']=='16.0' and info['CFBundleIdentifier']=='org.unixnu.k70.audio'
assert info['CFBundleExecutable']=='K70Audio' and 'K70Audio_Create' in info['CFPlugInFactories'].values()
subprocess.run(['codesign','--verify','--strict',bundle],check=True)
detail=subprocess.run(['codesign','-d','--verbose=4',bundle],text=True,capture_output=True,check=True).stderr
cdhash=re.search(r'^CDHash=([a-f0-9]{40})$',detail,re.M).group(1);assert json.loads((out/'trust.json').read_text())['cdhash']==cdhash
symbols=subprocess.check_output(['nm','-gj',p],text=True).splitlines();assert '_K70Audio_Create' in symbols
undefined=subprocess.check_output(['nm','-uj',p],text=True).splitlines()
assert not set(undefined)&{'_mprotect','_vm_protect','_dyld_register_func_for_add_image','_dlsym'}, 'unqualified rewrites/rebinding must compile out'
report=dict(status='macOS_CI_build_and_static_bundle_checks_only_no_iOS_hardware_acceptance',
 sha256=hashlib.sha256(d).hexdigest(),bytes=len(d),architecture='arm64 subtype0',build=builds[0],loads=loads,cdhash=cdhash,
 min_os_bundle=info['MinimumOSVersion'],host_clock_patch_default=False,virtual_audio_rebind_default=False,validated_shared_mapping=True,hardware_driver_not_supplied=True,
 device_tested=False,installed=False,no_Apple_or_OEM_binaries_in_artifact=True)
(out/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
