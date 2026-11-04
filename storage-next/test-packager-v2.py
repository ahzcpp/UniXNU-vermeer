#!/usr/bin/env python3
"""Targeted real-input capture/refusal tests; no firmware build or phone I/O."""
import hashlib,importlib.util,json,shutil,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;DELIVERY=Path('/mnt/e/K70-UniXNU-20261104')
if not __debug__:raise SystemExit('Run without -O')
FD=DELIVERY/'artifacts/boot-chain-05r1/SILICIUM_UEFI.fd'
AUDIT=DELIVERY/'boot-chain-05/source-snapshots/r1-verified-3947586d/fd-audit.json'
FD_SHA='3947586d054aaecea3f622d5e2c6f04fa71f8532cbc157b8f4aaf82c5daed1f2'
AUDIT_SHA='b91f6fc1d6171b848c9cf563106264af8dd48209ec16a35f0da93bbc4f05fc05'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(FD)==FD_SHA and sha(AUDIT)==AUDIT_SHA
spec=importlib.util.spec_from_file_location('k70_packager_v2',HERE/'package-storage-boot-v2.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
fd_bytes,audit_bytes,record=module.capture_firmware(FD,FD_SHA,AUDIT)
assert record['success'] and record['fd_sha256']==FD_SHA
checks=['actual archived r1 FD and original matching audit captured']
with tempfile.TemporaryDirectory(prefix='k70-packager-v2-input-') as directory:
    root=Path(directory);mutable=root/'mutable.fd';audit=root/'mutable-audit.json'
    mutable.write_bytes(fd_bytes);audit.write_bytes(audit_bytes)
    captured_fd,captured_audit,_=module.capture_firmware(mutable,FD_SHA,audit)
    changed=bytearray(fd_bytes);changed[4096]^=1
    mutable.write_bytes(changed);audit.write_text('{}')
    output=root/'snapshots';output.mkdir()
    snapshot,saved_audit=module.write_firmware_snapshots(output,captured_fd,captured_audit)
    assert sha(snapshot)==FD_SHA and sha(saved_audit)==AUDIT_SHA
    assert snapshot.read_bytes()==fd_bytes and saved_audit.read_bytes()==audit_bytes
    checks.append('original FD/audit changed after capture cannot alter build snapshot')
    try:module.capture_firmware(mutable,FD_SHA,audit)
    except AssertionError as error:assert str(error)=='Explicit FD hash/size mismatch'
    else:raise AssertionError('Changed FD accepted')
    checks.append('changed FD refused against explicit SHA')
    mutable.write_bytes(fd_bytes);wrong=json.loads(audit_bytes);wrong['fd_sha256']='0'*64;audit.write_text(json.dumps(wrong))
    try:module.capture_firmware(mutable,FD_SHA,audit)
    except AssertionError as error:assert str(error)=='Audit names another FD'
    else:raise AssertionError('Mismatched audit accepted')
    checks.append('successful audit for another FD refused')
# Add only a matching immutable sidecar; preserve the existing report and boot.
destination=DELIVERY/'artifacts/storage-native-01/firmware-audit-r1.snapshot.json'
if not destination.exists():
    with destination.open('xb') as stream:stream.write(audit_bytes)
assert sha(destination)==AUDIT_SHA
original_image=DELIVERY/'artifacts/storage-native-01/boot.img'
original_zip=HERE/'source-only/storage-native-01-source.zip'
assert sha(original_image)=='55f0424e70d536eca8d708f8a5c57a08451b942fc4ae72c5a7498aca3a104b3e'
assert sha(original_zip)=='6e3a75e217666dea94dd3b6bd1bda2aaa8780b7e0ab066cc1518f9b37a265d98'
assert sha(HERE/'package-storage-boot.py')=='14c21dc1cdb76b33e8ffbd880f3d6aaaa49b266098330fcfa32b8692151492b8'
report={'schema':'k70-packager-v2-targeted-tests-v1','success':True,'checks':checks,
        'actual_r1_fd_sha256':FD_SHA,'matched_r1_audit_sha256':AUDIT_SHA,
        'original_artifact_audit_sidecar_added':str(destination),
        'frozen_v1_boot_zip_and_script_byte_identical':True,'no_phone_io':True,
        'v2_source_sha256':sha(HERE/'package-storage-boot-v2.py'),
        'patch_sha256':sha(HERE/'k70-storage-packager-v2.patch')}
(HERE/'packager-v2-targeted-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
