#!/usr/bin/env python3
"""FAT payload variant: a verified base FAT with a new kernel and an explicit boot-argument delta.

Only the menu title and the boot arguments (old minus --remove, plus --append) may change in
unixnu.toml; ADT, trust cache and Data seed stay byte-identical. No phone I/O.
"""
import argparse,hashlib,json,shutil,struct,subprocess,tomllib
from pathlib import Path
PREFIX='EFI/UniXNU/';K=PREFIX+'Payloads/kernel.macho';C=PREFIX+'unixnu.toml'
FILES={C,K,K+'.adt',K+'.trustcache',PREFIX+'Payloads/scratch-data.img'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def adt_nodes(data):
    """path -> (header offset, end-of-properties offset, {name: bytes}) for every ADT node."""
    nodes={}
    def node(off,parent,depth=0):
        assert depth<64;head=off;n,c=struct.unpack_from('<II',data,off);off+=8;props={}
        for _ in range(n):
            key=data[off:off+32].split(b'\0',1)[0].decode('ascii');size=struct.unpack_from('<I',data,off+32)[0]&0x7fffffff;off+=36
            assert key not in props;props[key]=data[off:off+size];off=(off+size+3)&~3
        name=props['name'].rstrip(b'\0').decode('ascii');path=parent+'/'+name if depth else '/'
        nodes[path]=(head,off,props)
        for _ in range(c):off=node(off,'' if path=='/' else path,depth+1)
        return off
    assert node(0,'')==len(data);return nodes
def add_adt_u32(data,additions):
    """Insert new u32 properties; existing properties may not be overwritten."""
    for path,key,value in additions:
        nodes=adt_nodes(data);head,end,props=nodes[path];assert key not in props,(path,key)
        prop=key.encode('ascii').ljust(32,b'\0')+struct.pack('<I',4)+struct.pack('<I',value)
        n,c=struct.unpack_from('<II',data,head)
        data=data[:head]+struct.pack('<II',n+1,c)+data[head+8:end]+prop+data[end:]
    return data
def extract(image,out):
    out.mkdir();subprocess.run(['mcopy','-i',str(image),'-s','::/EFI',str(out)],check=True)
    assert {p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file()}==FILES
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--base',type=Path,required=True);ap.add_argument('--base-sha256',required=True)
ap.add_argument('--kernel-result',type=Path,required=True);ap.add_argument('--variant',required=True)
ap.add_argument('--title',required=True);ap.add_argument('--append',default='');ap.add_argument('--remove',default='')
ap.add_argument('--adt-u32',default='',help='Comma list of /node:key=value u32 properties to insert')
ap.add_argument('--out',type=Path,required=True)
a=ap.parse_args()
adt_additions=[]
for item in filter(None,a.adt_u32.split(',')):
    path,_,kv=item.partition(':');key,_,value=kv.partition('=');adt_additions.append((path,key,int(value,0)))
result=json.loads(a.kernel_result.read_text())
assert result['status']=='compiled_prelinked_probed_storage_verified_not_device_tested'
kernel_bytes=Path(result['kernel']['path']).read_bytes();assert hashlib.sha256(kernel_bytes).hexdigest()==result['kernel']['sha256']
base=a.base.read_bytes();assert hashlib.sha256(base).hexdigest()==a.base_sha256
assert not a.out.exists();a.out.mkdir(parents=True)
snap=a.out/'base-payload.img';snap.write_bytes(base);original=a.out/'original';extract(snap,original)
old_text=(original/C).read_text();old=tomllib.loads(old_text);entry=old['entry'][0];assert entry['id']=='xnu'
tokens=entry['args'].split();remove=a.remove.split();append=a.append.split()
for t in remove:assert tokens.count(t)==1,('remove target absent',t)
for t in append:assert t.split('=')[0] not in {x.split('=')[0] for x in tokens},('already set',t)
new_args=' '.join([t for t in tokens if t not in remove]+append)
text=old_text.replace('title = "'+entry['title']+'"','title = "'+a.title+'"').replace('args = "'+entry['args']+'"','args = "'+new_args+'"')
new=tomllib.loads(text);assert new['entry'][0]['args']==new_args and new['entry'][0]['title']==a.title
new['entry'][0]['args']=entry['args'];new['entry'][0]['title']=entry['title'];assert new==old,'only title and boot arguments may change'
changed=a.out/'changed';changed.mkdir();kernel=changed/'kernel.macho';kernel.write_bytes(kernel_bytes)
cfg=changed/'unixnu.toml';cfg.write_text(text)
replaced=[(kernel,K),(cfg,C)];repl={K:kernel,C:cfg}
if adt_additions:
    A=K+'.adt';old_adt=(original/A).read_bytes();new_adt=add_adt_u32(old_adt,adt_additions)
    old_nodes,new_nodes=adt_nodes(old_adt),adt_nodes(new_adt);assert set(old_nodes)==set(new_nodes)
    for path,(_,_,props) in old_nodes.items():
        added={k for p,k,_ in adt_additions if p==path}
        assert set(new_nodes[path][2])==set(props)|added,path
        for k,v in props.items():assert new_nodes[path][2][k]==v,(path,k)
        for p,k,value in adt_additions:
            if p==path:assert new_nodes[path][2][k]==struct.pack('<I',value)
    adt=changed/'kernel.macho.adt';adt.write_bytes(new_adt);replaced.append((adt,A));repl[A]=adt
image=a.out/'payload.img';shutil.copyfile(snap,image)
for source,dest in replaced:subprocess.run(['mcopy','-o','-i',str(image),str(source),'::/'+dest],check=True)
readback=a.out/'readback';extract(image,readback)
for member in FILES:assert (readback/member).read_bytes()==(repl.get(member) or (original/member)).read_bytes(),member
report={'schema':'k70-storage-variant-payload/1','variant':a.variant,'offline_verified':True,'base_fat_sha256':a.base_sha256,
 'fat_sha256':sha(image),'fat_bytes':image.stat().st_size,'payload_image':str(image),'payload_kernel_sha256':sha(readback/K),
 'all_fat_file_bytes_match':True,'adt_unchanged_from_r5':not adt_additions,'adt_u32_added':[f'{p}:{k}={v}' for p,k,v in adt_additions],
 'adt_sha256':sha(readback/(K+'.adt')),'trust_and_seed_unchanged':True,
 'boot_arguments':new_args,'appended_boot_arguments':a.append,'removed_boot_arguments':a.remove,'required_owned_window_bytes':0x80000000,
 'files':{n:{'bytes':(readback/n).stat().st_size,'sha256':sha(readback/n)} for n in sorted(FILES)},
 'hardware_tested':False,'no_phone_io':True,'userdata_rewritten':False}
(a.out/'payload-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2))
