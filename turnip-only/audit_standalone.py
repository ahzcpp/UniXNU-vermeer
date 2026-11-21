"""Thin arm64/iOS owned dylib code-page/special-slot audit; not dyld proof."""
from pathlib import Path
import collections,hashlib,struct
def audit(path):
 b=Path(path).read_bytes();assert 32<=len(b)<128*1024**2
 assert struct.unpack_from('<4I',b)==(0xfeedfacf,0x100000c,0,6)
 ncmd,cmdbytes=struct.unpack_from('<II',b,16);assert 0<ncmd<=4096 and 32+cmdbytes<=len(b)
 at=32;sigs=[];minimums=[];dependencies=[];symtab=None
 for _ in range(ncmd):
  cmd,size=struct.unpack_from('<II',b,at);assert size>=8 and at+size<=32+cmdbytes
  if cmd==0x1d:assert size==16;sigs.append(struct.unpack_from('<II',b,at+8))
  if cmd==0x32:
   assert size>=24;platform,minimum=struct.unpack_from('<II',b,at+8);assert platform==2 and minimum==0x100700;minimums.append(minimum)
  if cmd==0x25:
   assert size>=16;minimum=struct.unpack_from('<I',b,at+8)[0];assert minimum==0x100700;minimums.append(minimum)
  if cmd==2:assert size==24 and symtab is None;symtab=struct.unpack_from('<4I',b,at+8)
  if cmd in {0xc,0x80000018,0x8000001f,0x20}:
   assert size>=24;off=struct.unpack_from('<I',b,at+8)[0];assert 24<=off<size
   end=b.index(0,at+off,at+size);dependencies.append({'command':cmd,'name':b[at+off:end].decode('ascii')})
  at+=size
 assert at==32+cmdbytes and minimums and len(sigs)==1 and symtab
 start,sz=sigs[0];assert start>=at and start+sz<=len(b)
 magic,length,count=struct.unpack_from('>III',b,start);assert magic==0xfade0cc0 and 0<count<=64 and 12+count*8<=length<=sz
 blobs={};ranges=[]
 for i in range(count):
  slot,rel=struct.unpack_from('>II',b,start+12+8*i);assert slot not in blobs and 12+8*count<=rel<=length-8
  magic,n=struct.unpack_from('>II',b,start+rel);assert n>=8 and rel+n<=length
  assert not any(rel<end and lo<rel+n for lo,end in ranges);ranges.append((rel,rel+n));blobs[slot]=b[start+rel:start+rel+n]
 if 0x10000 in blobs:assert blobs[0x10000]==struct.pack('>II',0xfade0b01,8),'nonempty CMS is not ad-hoc'
 cds=[]
 for slot,cd in blobs.items():
  if slot!=0 and not 0x1000<=slot<=0x1005:continue
  assert len(cd)>=44 and struct.unpack_from('>I',cd)[0]==0xfade0c02
  ver,flags,hoff,ident,ns,nc,limit,hsize,htype,platform,page=struct.unpack_from('>7I4B',cd,8)
  assert flags&2 and limit==start and page==12 and nc==(limit+4095)//4096
  hashes={1:(hashlib.sha1,20),2:(hashlib.sha256,32),3:(hashlib.sha256,20),4:(hashlib.sha384,48)}
  assert htype in hashes and hsize==hashes[htype][1] and hoff-ns*hsize>=0 and hoff+nc*hsize<=len(cd)
  f=hashes[htype][0];digest=lambda raw:f(raw).digest()[:hsize]
  assert 44<=ident<len(cd);end=cd.index(0,ident);identifier=cd[ident:end].decode('ascii');assert identifier=='org.blu-sharky.turnip'
  for i in range(nc):assert cd[hoff+i*hsize:hoff+(i+1)*hsize]==digest(b[i*4096:min(limit,(i+1)*4096)])
  specials={}
  for i in range(1,ns+1):
   value=cd[hoff-i*hsize:hoff-(i-1)*hsize]
   if value==bytes(hsize):continue
   assert i in blobs,'standalone dylib has unknown/external nonzero special slot'
   assert value==digest(blobs[i]);specials[str(i)]=hashlib.sha256(blobs[i]).hexdigest()
  cds.append({'slot':slot,'version':ver,'flags':flags,'identifier':identifier,'page_bytes':4096,'code_pages':nc,'hash_type':htype,'special_slots':specials,'cdhash':digest(cd).hex()[:40]})
 assert any(c['slot']==0 for c in cds)
 so,number,st,stbytes=symtab;assert so+number*16<=len(b) and st+stbytes<=len(b)
 groups=collections.Counter();undef=0
 for i in range(number):
  ix,ty,section,desc,value=struct.unpack_from('<IBBHQ',b,so+i*16)
  if ty&0xe0 or not ty&1 or ty&0xe or not ix:continue
  assert ix<stbytes;b.index(0,st+ix,st+stbytes)
  groups[(desc>>8,bool(desc&0x40),bool(desc&0x80),value==0)]+=1;undef+=1
 primary=next(c for c in cds if c['slot']==0)
 return {'identifier':primary['identifier'],'primary_cdhash':primary['cdhash'],'CodeDirectories':cds,'minOS':minimums,'dylibs':dependencies,'undefined_count':undef,'undefined_groups':[{'ordinal':k[0],'weak_reference':k[1],'weak_definition_bit':k[2],'value_zero':k[3],'count':n} for k,n in sorted(groups.items())],'actual_iOS16_import_resolution_proven':False,'special_ordinal_not_automatically_missing_strong_import':True}
