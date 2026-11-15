"""Read back407/408; isolate RO selector from all radio/GPU/userland resources."""
from pathlib import Path
import hashlib,importlib.util,json,struct,subprocess,tomllib
P=Path(__file__).resolve().parent;D=Path('/mnt/d/K70-UniXNU-data');sp=importlib.util.spec_from_file_location('adt',P/'build-test60-payload.py');adt=importlib.util.module_from_spec(sp);sp.loader.exec_module(adt)
H=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def fat(number,name):return subprocess.check_output(['mtype','-i',str(D/f'builds/gpu{number}-payload/payload.img'),'::/'+name])
def flatten(node,path=''):
    out={path+'/'+k:(f,v.hex()) for k,f,v in node[0]}
    for child in node[1]:out.update(flatten(child,path+'/'+adt.name(child)))
    return out
old=json.loads((D/'builds/gpu394-payload/payload-verification.json').read_text());old_tree=flatten(adt.parse(fat(394,'EFI/UniXNU/Payloads/kernel.macho.adt')))
canonical=lambda argv:[x for x in argv if not x.startswith('k70-walltime=')]
old_args=canonical(tomllib.loads(fat(394,'EFI/UniXNU/unixnu.toml').decode())['entry'][0]['args'].split())
mutable={'EFI/UniXNU/unixnu.toml','EFI/UniXNU/Payloads/kernel.macho','EFI/UniXNU/Payloads/kernel.macho.adt'};profiles={};payloads=[];trees=[]
for number,flag in [(407,1),(408,0)]:
    proof=json.loads((D/f'builds/ios16-20H392-test{number}/verification.json').read_text());p=json.loads((D/f'builds/gpu{number}-payload/payload-verification.json').read_text());plan=json.loads((D/f'wifi-variant{number}/boot-plan.json').read_text())
    boot=D/f'builds/ios16-20H392-test{number}/boot.img';assert H(boot)==plan['boot_sha256']==proof['image_sha256']
    assert not proof['hardware_tested'] and not proof['phone_written']
    assert proof['fat_payload_read_back_equal'] and proof['fd_bytes_read_back_equal'] and proof['memory_budget_adt_read_back_equal'] and proof['nvram_ro_selector_adt_read_back_equal']
    assert proof['necp16_requested']==proof['ram_zero_holes_requested']==1 and proof['nvram_proxy_ro_requested']==flag
    assert proof['owned_window_bytes']==2684354560 and proof['vm_compressor_requested']==2 and proof['disabled_setup_zero_omitted']
    assert proof['fd_sha256']=='a729f6cc2797ce9365e62354b64c6c8125f1b22851f5cea98288624c3b3dde89'
    argv=tomllib.loads(fat(number,'EFI/UniXNU/unixnu.toml').decode())['entry'][0]['args'];assert argv==p['boot_arguments'] and len(argv.encode())==995 and canonical(argv.split())==old_args
    assert set(p['files'])==set(old['files'])
    for name in p['files']:
        if name not in mutable:assert p['files'][name]==old['files'][name],name
    tree=flatten(adt.parse(fat(number,'EFI/UniXNU/Payloads/kernel.macho.adt')))
    assert set(tree)-set(old_tree)=={'/chosen/k70-nvram-proxy-ro'} and set(old_tree)<=set(tree)
    assert tree['/chosen/k70-nvram-proxy-ro']==(0,struct.pack('<I',flag).hex())
    assert {k for k in old_tree if tree[k]!=old_tree[k]}<={'/chosen/manifest-properties/BNCH','/chosen/boot-manifest-hash'}
    # Verify the actual owner signature and manifest's real kernel/ADT/seed binding.
    folder=D/f'builds/gpu{number}-payload';manifest=(folder/'owner-boot-manifest.json').read_bytes();m=json.loads(manifest);sig=(folder/'owner-boot-manifest.sig').read_bytes()
    key='/root/k70-clean-20261104/out-root-policies/keys/owner-public.pem';r=subprocess.run(['openssl','dgst','-sha256','-verify',key,'-signature',str(folder/'owner-boot-manifest.sig'),str(folder/'owner-boot-manifest.json')],capture_output=True,text=True);assert r.returncode==0,r.stderr
    assert m['kernel_sha256']==p['files']['EFI/UniXNU/Payloads/kernel.macho']['sha256'] and m['data_seed_sha256']==p['files']['EFI/UniXNU/Payloads/scratch-data.img']['sha256']
    assert tree['/chosen/boot-manifest-hash'][1]==hashlib.sha384(manifest+sig).hexdigest()
    node=adt.parse(fat(number,'EFI/UniXNU/Payloads/kernel.macho.adt'));chosen=next(c for c in node[1] if adt.name(c)=='chosen');chosen[0][:]=[q for q in chosen[0] if q[0]!='boot-manifest-hash']
    assert hashlib.sha256(adt.serialize(node)).hexdigest()==m['adt_before_manifest_hash_sha256']
    profiles[str(number)]=dict(boot_image=str(boot),boot_sha256=H(boot),selector=flag,bootarg_bytes=995,hardware_tested=False,repack_current_clock_before_future_boot=True)
    payloads.append(p);trees.append(tree)
assert payloads[0]['files']['EFI/UniXNU/Payloads/kernel.macho']==payloads[1]['files']['EFI/UniXNU/Payloads/kernel.macho']
assert {k for k in trees[0] if trees[0][k]!=trees[1][k]}=={'/chosen/k70-nvram-proxy-ro','/chosen/manifest-properties/BNCH','/chosen/boot-manifest-hash'}
report=dict(status='PASS_EXACT_RO_SELECTOR_CONTROL_PAIR',same_kernel_and_userland_resources=True,NECP_RAM_flags_both1=True,only_RO_ADT_selector_plus_fresh_owner_digests_differ=True,radio_GPU_Data_firmware_owned_RAM_unchanged=True,owner_signatures_and_real_kernel_ADT_seed_binding_verified=True,bootarg_cap_unchanged=True,profiles=profiles,hardware_tested=False,phone_accessed=False,limitations='Readback/static signing checks; not UI/rootquiet/network/persistence acceptance. Fresh clock repack before future authorized phone test.')
(D/'nvram407-pair-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
