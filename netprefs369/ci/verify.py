import hashlib,json,plistlib,re,struct,sys
from pathlib import Path
d,w=map(Path,sys.argv[1:]);b=(d/'unixnu-netprefs369').read_bytes()
assert struct.unpack_from('<I',b)[0]==0xfeedfacf
assert struct.unpack_from('<I',b,4)[0]==0x100000c
n=struct.unpack_from('<I',b,16)[0];off=32;so=None;minimum=None
for i in range(n):
 cmd,size=struct.unpack_from('<II',b,off);assert size>=8 and off+size<=len(b)
 if cmd==0x1d: so,ss=struct.unpack_from('<II',b,off+8)
 if cmd==0x32:
  platform,minimum=struct.unpack_from('<II',b,off+8);assert platform==2 and minimum==0x100000
 off+=size
assert so is not None and minimum==0x100000
magic,length,count=struct.unpack_from('>III',b,so);assert magic==0xfade0cc0 and so+length<=len(b)
pages=[]
for i in range(count):
 typ,bo=struct.unpack_from('>II',b,so+12+8*i)
 if struct.unpack_from('>I',b,so+bo)[0]==0xfade0c02:pages.append(1<<b[so+bo+39])
assert pages and all(x==4096 for x in pages)
ents=plistlib.loads((w/'entitlements.xml').read_bytes());assert ents=={'com.apple.private.security.no-container':True},ents
libs=[x.split()[0] for x in (w/'dylibs.txt').read_text().splitlines()[1:] if x.strip()]
assert set(libs)=={'/usr/lib/libSystem.B.dylib','/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation','/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration'},libs
cs=(w/'codesign.txt').read_text();cd=re.search(r'CDHash=([0-9a-f]{40})',cs);assert cd
report=dict(source_sha256=hashlib.sha256(Path('src/unixnu-netprefs369.c').read_bytes()).hexdigest(),sc_header_sha256=hashlib.sha256(Path('src/netprefs369_sc.h').read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(b).hexdigest(),cdhash=cd[1],pages=pages,minimum_ios='16.0',arm64=True,entitlements=ents,libs=libs,actual_imports=(d/'imports.txt').read_text(),cf_fixtures=(d/'cf-fixtures.txt').read_text(),runner_note='macOS smoke only, not K70 runtime evidence',hardware_tested=False)
(d/'build-report.json').write_text(json.dumps(report,indent=2)+'\n');print('NETPREFS369 SDK_SIGNATURE PASS')
