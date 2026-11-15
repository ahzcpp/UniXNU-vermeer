"""Reject invalid new selector/profile combinations before any payload builder."""
from pathlib import Path
import hashlib,json,subprocess
D=Path('/mnt/d/K70-UniXNU-data');base=json.loads((D/'wifi-variant407/package-command.json').read_text());H=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
before={n:H(D/f'builds/ios16-20H392-test{n}/boot.img') for n in (380,381,382,383,384,394,395,407)}
bad=[]
for changes in [{'--nvram-proxy-ro':'0'},{'--nvram-proxy-ro':None},{'--number':'408'},{'--number':'409'},{'--necp16-abi':'0'},{'--ram-zero-holes':'0'},{'--kernel-number':'394'}]:
    argv=base.copy()
    for key,value in changes.items():
        at=argv.index(key)
        if value is None:del argv[at:at+2]
        else:argv[at+1]=value
    bad.append(argv)
old=json.loads((D/'wifi-variant394/package-command.json').read_text());bad.append(old+['--nvram-proxy-ro','1'])
for argv in bad:
    r=subprocess.run(argv,text=True,capture_output=True)
    assert r.returncode!=0 and 'AssertionError' in r.stderr,(r.stdout,r.stderr)
    assert 'Running' not in r.stdout and 'build-test' not in r.stdout
for n,digest in before.items():assert H(D/f'builds/ios16-20H392-test{n}/boot.img')==digest
(D/'packager407-negative-proof.json').write_text(json.dumps(dict(status='PASS_INVALID_NVRAM_PROFILES_REJECTED',cases=len(bad),before_payload_builder=True,frozen_images_unchanged=before,phone_accessed=False,hardware_tested=False),indent=2)+'\n')
print('Eight invalid new/old selector/profile mixtures rejected before building; all frozen images unchanged')
