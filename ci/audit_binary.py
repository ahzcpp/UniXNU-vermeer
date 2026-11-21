"""Runner-only native signature and import metadata. Never uploads the binary."""
from pathlib import Path
import hashlib,json,plistlib,struct,sys
p=Path(sys.argv[1]);out=Path(sys.argv[2]);b=p.read_bytes();magic,cpu,_,_,ncmds,_,_,_=struct.unpack_from('<8I',b);assert magic==0xfeedfacf and cpu==0x100000c
libs=[];at=32;sym=None;sig=None;platform=None
for _ in range(ncmds):
 cmd,size=struct.unpack_from('<II',b,at);assert size>=8 and at+size<=len(b)
 if cmd in [0xc,0x80000018,0x8000001f,0x80000023,0x20]:
  pos=at+struct.unpack_from('<I',b,at+8)[0];libs.append(b[pos:b.index(0,pos)].decode())
 if cmd==2:sym=struct.unpack_from('<4I',b,at+8)
 if cmd==0x1d:sig=struct.unpack_from('<2I',b,at+8)
 if cmd==0x32:platform,minos=struct.unpack_from('<2I',b,at+8)
 at+=size
assert platform==2 and minos==0x100000
assert set(libs)<=set(['/usr/lib/libSystem.B.dylib','/usr/lib/libobjc.A.dylib','/System/Library/Frameworks/Foundation.framework/Foundation','/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation'])
assert sym and sig;so,ss=sig;blob=b[so:so+ss];assert len(blob)==ss and struct.unpack_from('>I',blob)[0]==0xfade0cc0
count=struct.unpack_from('>I',blob,8)[0];blobs={}
for i in range(count):
 slot,pos=struct.unpack_from('>II',blob,12+8*i);size=struct.unpack_from('>I',blob,pos+4)[0];assert slot not in blobs and pos+size<=len(blob);blobs[slot]=blob[pos:pos+size]
cd=blobs[0];version,flags,hoff,ioff,ns,nc,limit,sz,kind,_,shift=struct.unpack_from('>7I4B',cd,8)
assert flags==2 and sz==32 and kind==2 and shift==12 and limit==so and nc==(limit+4095)//4096
for i in range(nc):assert cd[hoff+i*32:hoff+(i+1)*32]==hashlib.sha256(b[i*4096:min((i+1)*4096,limit)]).digest()
for i in range(1,ns+1):
 h=cd[hoff-i*32:hoff-(i-1)*32];assert h==(hashlib.sha256(blobs[i]).digest() if i in blobs else bytes(32))
assert cd[ioff:cd.index(0,ioff)]==b'org.unixnu.diag.crashprobe464'
assert plistlib.loads(blobs[5][8:])=={'com.apple.private.security.no-container':True} and 7 in blobs
imports=[];off,n,strings,strlen=sym
for i in range(n):
 ix,typ,sect,desc,value=struct.unpack_from('<IBBHQ',b,off+i*16)
 if typ&0x0e==0 and typ&1 and ix:
  ordinal=desc>>8;assert 1<=ordinal<=len(libs);name=b[strings+ix:b.index(0,strings+ix)].decode();imports.append({'symbol':name,'declared_library':libs[ordinal-1],'weak_reference':bool(desc&0x40)})
src=Path(__file__).resolve().parents[1];files={str(p.relative_to(src)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ['src','tests','ci','config'] for p in (src/folder).rglob('*') if p.is_file()}
report={'status':'PASS_RUNNER_NATIVE_SIGNATURE_AND_IMPORT_EXTRACTION','binary_sha256':hashlib.sha256(b).hexdigest(),'binary_bytes':len(b),'cdhash':hashlib.sha256(cd).hexdigest()[:40],'platform':platform,'minos':minos,'pages_verified':nc,'special_slots_verified':ns,'entitlements':{'com.apple.private.security.no-container':True},'dylibs':libs,'imports':imports,'source':files,'actual20H392_imports':'PENDING_LOCAL_CACHE_CHECK','runtime_permissions':'UNKNOWN','binary_uploaded':False,'admitted_for_deployment':False}
out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['status','binary_sha256','cdhash','pages_verified','binary_uploaded','admitted_for_deployment']}))
