#!/usr/bin/env python3
"""Offline full boot container: audited explicit RAM512 FD + verified storage FAT.

No fastboot, adb, partition selection, userdata operation, or live-RAM claim.
"""
import argparse,hashlib,json,os,re,struct,sys,zlib
from pathlib import Path
HERE=Path(__file__).resolve().parent
if not __debug__:raise SystemExit('Run without -O: packaging input/output verification is mandatory')
ROOT=Path('/root/k70-clean-20261104');DELIVERY=Path('/mnt/e/K70-UniXNU-20261104')
WORK=ROOT/'storage-candidate-20261104';DEVICE=WORK/'device'
PAYLOAD=WORK/'out/vermeer/ipados-17.7.11-21H461/vermeer/payload/payload.img'
PAYLOAD_SHA='f02a97df12648541b0fe1cb4ad337ecf8206fcd67e33a9fc052adc0eafcdfc64'
DTB=DELIVERY/'stock-analysis/vendor_boot/dtb-002.dtb'
DTB_SHA='1d0daa058e0b964b57c2c5e53b037c569d5437ed334a32e8fdea9be5be239c88'
CAPACITY=192*1024**2
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def capture_firmware(fd_path, expected_sha256, audit_path):
    """Capture both original inputs once; all later reads use private snapshots."""
    fd_bytes=Path(fd_path).read_bytes()
    assert len(fd_bytes)==0x300000 and hashlib.sha256(fd_bytes).hexdigest()==expected_sha256, 'Explicit FD hash/size mismatch'
    audit_bytes=Path(audit_path).read_bytes()
    audit=json.loads(audit_bytes)
    assert audit['schema']=='k70-boot05-fd-audit/1' and audit['success'] is True
    assert not audit['errors'] and audit['fd_sha256']==expected_sha256, 'Audit names another FD'
    assert audit['fd_bytes']==len(fd_bytes)
    return fd_bytes,audit_bytes,audit

def write_firmware_snapshots(output, fd_bytes, audit_bytes):
    inputs=Path(output)/'inputs';inputs.mkdir()
    fd_snapshot=inputs/'SILICIUM_UEFI.fd';audit_snapshot=inputs/'firmware-audit.json'
    with fd_snapshot.open('xb') as stream:stream.write(fd_bytes)
    with audit_snapshot.open('xb') as stream:stream.write(audit_bytes)
    assert fd_snapshot.read_bytes()==fd_bytes and audit_snapshot.read_bytes()==audit_bytes
    return fd_snapshot,audit_snapshot
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fd',type=Path,required=True)
    parser.add_argument('--fd-sha256',required=True)
    parser.add_argument('--firmware-audit',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True,help='Fresh private output directory; binaries must not be committed')
    args=parser.parse_args()
    assert re.fullmatch('[0-9a-f]{64}',args.fd_sha256),'Use explicit lowercase SHA-256 from the completed FD audit'
    fd_bytes,audit_bytes,firmware_audit=capture_firmware(args.fd,args.fd_sha256,args.firmware_audit)
    readback=json.loads((HERE/'payload-readback-verification.json').read_text())
    assert readback['all_fat_file_bytes_match'] and readback['payload_original_storage_16_aliases_and_190_slots_pass']
    assert readback['full_two_volume_policy_bytes_match_c_verified_inputs'] and readback['real_corrected_seed_bytes_match']
    assert readback['required_owned_window_bytes']==0x20000000 and not readback['actual_ufs_io_qualified']
    assert sha(PAYLOAD)==readback['fat_sha256']==PAYLOAD_SHA and PAYLOAD.stat().st_size==128*1024**2
    assert sha(DTB)==DTB_SHA
    assert not args.out.exists(),'Refuse to overwrite a previous container'
    args.out.mkdir(parents=True)
    fd_snapshot,audit_snapshot=write_firmware_snapshots(args.out,fd_bytes,audit_bytes)
    sys.path.insert(0,str(ROOT/'UniXNU/scripts'))
    from unixnu.cli import parser as cli_parser
    from unixnu.config import Context
    from unixnu import bootimg
    os.environ['PATH']='/usr/lib/llvm-18/bin:'+os.environ.get('PATH','');os.environ.pop('CCC_OVERRIDE_OPTIONS',None)
    ctx=Context(cli_parser().parse_args(['bootimg','-D',str(DEVICE),'--out',str(WORK/'out')]))
    image=args.out/'boot.img';bootimg.build(ctx,ROOT/'uefi',fd_snapshot,DTB,ramdisk=PAYLOAD,output=image)
    data=image.read_bytes();assert data[:8]==b'ANDROID!' and len(data)<=CAPACITY
    kernel_size,kernel_pa,ramdisk_size,ramdisk_pa,second_size,second_pa,tags_pa,page,version,os_version=struct.unpack_from('<10I',data,8)
    assert version==0 and page==4096 and second_size==0 and kernel_size>0 and ramdisk_size==128*1024**2
    rd_at=(1+(kernel_size+page-1)//page)*page
    assert rd_at+ramdisk_size<=len(data) and data[rd_at:rd_at+ramdisk_size]==PAYLOAD.read_bytes()
    assert all(x==0 for x in data[rd_at+ramdisk_size:]),'Unexpected trailing image content'
    encoded=data[page:page+kernel_size];decoder=zlib.decompressobj(31);combined=decoder.decompress(encoded)
    assert decoder.eof and len(combined)>len(fd_bytes) and combined[-len(fd_bytes):]==fd_bytes
    tree=decoder.unused_data;assert tree[:4]==b'\xd0\x0d\xfe\xed' and struct.unpack_from('>I',tree,4)[0]==len(tree)
    # Byte equality of all three opaque inputs is independent of the builder's
    # own round-trip check; only the deliberately added stock splash node may
    # differ from the stock DTB. fdtget verifies that actual appended node.
    appended=args.out/'appended-stock-v2.dtb';appended.write_bytes(tree)
    import subprocess
    splash=subprocess.check_output(['fdtget','-t','x',appended,'/reserved-memory/splash_region','reg'],text=True).split()
    assert [int(x,16) for x in splash]==[0,0xb8000000,0,0x2b00000]
    report={'schema':'k70-storage-fullboot-offline-v2','offline_container_verified':True,
        'image':str(image),'image_sha256':sha(image),'image_bytes':len(data),
        'boot_header_version':version,'boot_page_bytes':page,'boot_b_capacity_bytes':CAPACITY,
        'fits_boot_b':len(data)<=CAPACITY,'remaining_partition_bytes':CAPACITY-len(data),
        'explicit_fd_input':str(args.fd),'captured_fd_snapshot':str(fd_snapshot),'fd_sha256':args.fd_sha256,'fd_bytes':len(fd_bytes),
        'fd_audit':str(audit_snapshot),'fd_audit_sha256':hashlib.sha256(audit_bytes).hexdigest(),
        'original_firmware_audit_input':str(args.firmware_audit),
        'fd_input_read_once':True,'firmware_audit_input_read_once':True,
        'build_and_unpack_use_captured_fd_bytes':True,'firmware_audit_snapshot_saved':True,
        'packager_sha256':sha(Path(__file__)),
        'fd_bytes_read_back_equal':True,'fat_payload_sha256':PAYLOAD_SHA,'fat_payload_read_back_equal':True,
        'stock_v2_dtb_input_sha256':DTB_SHA,'appended_dtb_sha256':sha(appended),'stock_splash_roundtrip':True,
        'header_load_addresses':{'kernel':hex(kernel_pa),'ramdisk':hex(ramdisk_pa),'tags':hex(tags_pa)},
        'shim_bytes':len(combined)-0x300000,'shim_sha256':hashlib.sha256(combined[:-0x300000]).hexdigest(),
        'bootshim_source_sha256':sha(ROOT/'uefi/BootShim/BootShim.S'),
        'device_boot_config_sha256':sha(ROOT/'uefi/Resources/Configs/vermeer.toml'),
        'mkbootimg_script_sha256':sha(ROOT/'uefi/Resources/Scripts/mkbootimg.py'),
        'payload_kernel_sha256':readback['payload_kernel_sha256'],'payload_readback_proof_sha256':sha(HERE/'payload-readback-verification.json'),
        'required_owned_window_bytes':0x20000000,'actual_ram512_window_proven':False,
        'actual_ufs_io_qualified':False,'hardware_tested':False,'no_phone_io':True,'userdata_written':False,
        'private_apple_kernel_trustcache_and_oem_firmware_binary_not_for_git':True,
        'limitation':'Offline container only. RAM512 runtime qualification, actual UFS media and root mount remain unverified.'}
    (args.out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
