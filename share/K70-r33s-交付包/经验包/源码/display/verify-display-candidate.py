#!/usr/bin/env python3
"""Independent archive readback and constructor metadata check; no runtime claims."""
import hashlib,json,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path('/root/k70-clean-20261104/display-candidate-20261104')
HERE=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
archive=ROOT/'libK70GopDisplay.candidate.a';report=json.loads((ROOT/'verification.json').read_text())
assert sha(archive)==report['archive_sha256']
expected=['NativeLegacyDisplay.late.o','original-bindings.o','K70DisplayProvider.o']
actual=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-ar','t',archive],text=True).splitlines()
assert actual==expected
with tempfile.TemporaryDirectory(prefix='display-readback-',dir=ROOT) as tmp:
    subprocess.run(['/usr/lib/llvm-18/bin/llvm-ar','x',archive],cwd=tmp,check=True)
    for name in expected:assert (Path(tmp)/name).read_bytes()==(ROOT/name).read_bytes()
    constructors=[]
    for name in expected:
        b=(Path(tmp)/name).read_bytes();magic,cpu,subtype,kind,ncmds,size=struct.unpack_from('<6I',b)
        assert (magic,cpu,subtype,kind)==(0xfeedfacf,0x100000c,0,1)
        at=32;deployment=None
        for _ in range(ncmds):
            cmd,length=struct.unpack_from('<2I',b,at)
            if cmd==0x32:deployment=struct.unpack_from('<2I',b,at+8)
            if cmd==0x19:
                for i in range(struct.unpack_from('<I',b,at+64)[0]):
                    s=at+72+i*80;section=b[s:s+16].rstrip(b'\0');flags=struct.unpack_from('<I',b,s+64)[0]
                    assert section!=b'__mod_init_func' and flags&255!=9,'Unexpected automatic class initialization'
                    if section==b'__coachz_init':constructors.append(name)
            at+=length
        assert at==32+size and deployment==(2,0x110000)
    assert constructors==['NativeLegacyDisplay.late.o']
    symbols=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm','-g',archive],text=True)
    assert ' T _k70_display_start' in symbols and '__GLOBAL__sub_I_NativeLegacyDisplay.cpp' in symbols
result={'status':'archive_roundtrip_arm64_ios17_and_no_auto_constructor_passed','archive_sha256':sha(archive),
 'member_names':expected,'late_constructor_requires_explicit_call':True,'hardware_tested':False,'desktop':False}
(HERE/'archive-readback-verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
