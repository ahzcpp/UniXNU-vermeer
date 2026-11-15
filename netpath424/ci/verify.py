from pathlib import Path
import hashlib,json,re,plistlib,struct,sys
d,w=map(Path,sys.argv[1:]);b=(d/'unixnu-netpath424').read_bytes()
assert struct.unpack_from('<II',b)==(0xfeedfacf,0x100000c)
at=32;signature=None;build=False;libs=[]
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,size=struct.unpack_from('<II',b,at);assert size>=8 and at+size<=len(b)
 if cmd==0x1d:signature=struct.unpack_from('<II',b,at+8)
 if cmd==0x32:assert struct.unpack_from('<II',b,at+8)==(2,0x100000);build=True
 if cmd in (0xc,0x80000018,0x8000001f):
  start=at+struct.unpack_from('<I',b,at+8)[0];libs.append(b[start:b.index(b'\0',start)].decode())
 at+=size
assert signature and build
base,total=signature;assert base+total<=len(b);magic,length,count=struct.unpack_from('>III',b,base);assert magic==0xfade0cc0 and length<=total
pages=[]
for i in range(count):
 slot,off=struct.unpack_from('>II',b,base+12+8*i)
 if struct.unpack_from('>I',b,base+off)[0]==0xfade0c02:pages.append(1<<b[base+off+39])
assert pages and set(pages)=={4096}
allowed={'/usr/lib/libSystem.B.dylib','/usr/lib/libobjc.A.dylib','/System/Library/Frameworks/Foundation.framework/Foundation','/System/Library/Frameworks/Network.framework/Network','/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation'}
assert set(libs)<=allowed and '/System/Library/Frameworks/Network.framework/Network' in libs and '/System/Library/Frameworks/Foundation.framework/Foundation' in libs,libs
ents=plistlib.loads((w/'entitlements.xml').read_bytes());assert ents=={'com.apple.private.security.no-container':True}
cd=re.search(r'CDHash=([0-9a-f]{40})',(w/'codesign.txt').read_text());assert cd
H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r=dict(binary='unixnu-netpath424',binary_sha256=H(d/'unixnu-netpath424'),cdhash=cd[1],source_sha256=H(Path('src/unixnu-netpath424.m')),classifier_sha256=H(Path('src/netpath_classify.h')),identity_headers_sha256={n:H(Path('src')/n) for n in ['Identity424.h','FdGate424.h','Watchdog424.h']},actual_UID_drop_tested=False,minimum_ios='16.0',pages=pages,identifier='org.unixnu.diag.netpath424',entitlements=ents,dylibs=libs,actual_imports=(d/'imports.txt').read_text(),classifier=(d/'classifier.txt').read_text(),hardware_tested=False,stock_apps_tested=False,runner_note='SDK and runner smoke only; no K70 evidence')
(d/'build-report.json').write_text(json.dumps(r,indent=2)+'\n');print('NETPATH424 native SDK/signature metadata PASS')
