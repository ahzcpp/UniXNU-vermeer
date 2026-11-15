"""Explicit407/408 RO selector controls; retain every ancestor qualification."""
from pathlib import Path
import ast,hashlib,json,re
P=Path(__file__).resolve().parent;p=P/'package-gpu-probe-stage.py';backup=P/'package-gpu-probe-stage.pre407.py'
if not backup.exists():backup.write_bytes(p.read_bytes())
original=backup.read_text();s=original
def replace(old,new,count=1):
    global s
    assert s.count(old)==count,(old,s.count(old));s=s.replace(old,new)
replace("    394:'Offline394 over reviewed380:',","    394:'Offline394 over reviewed380:',\n    407:'Offline407 over reviewed394:',")
replace('COUNTER_PAIRS=((380,380),(381,380),(384,380),(382,382),(383,382),(394,394),(395,394))','COUNTER_PAIRS=((380,380),(381,380),(384,380),(382,382),(383,382),(394,394),(395,394),(407,407),(408,407))')
anchor="ap.add_argument('--iokit-match-log',action='store_true',help='Existing start/config/catalogue logs only; no tracking or wait-policy change')"
replace(anchor,anchor+"\nap.add_argument('--nvram-proxy-ro',type=int,choices=[0,1],help='Explicit reviewed399+401+403 immutable proxy backend, requires407; no persistence')")
replace('if args.kernel_number in (380,382,394):','if args.kernel_number in (380,382,394,407):',2)
replace('395:(0,0,False)}[args.number]','395:(0,0,False),407:(1,1,False),408:(1,1,False)}[args.number]')
replace('args.kernel_number==394','args.kernel_number in (394,407)',4)
replace('if args.kernel_number in (380,394):','if args.kernel_number in (380,394,407):')
counter=0
def extend(match):
    global counter
    items=match.group(1).split(',')
    if '370' not in items:return match.group(0)
    assert all(x in items for x in ('380','382','394')) and '407' not in items
    counter+=1;return 'if args.kernel_number in ('+match.group(1)+',407):'
s=re.sub(r'if args\.kernel_number in \(([^)\n]+)\):',extend,s);assert counter==14,counter
anchor='assert args.number>60\n'
new='''if args.kernel_number==407:
    assert args.nvram_proxy_ro=={407:1,408:0}[args.number]
    nvram_base=Path('/mnt/d/K70-UniXNU-data')
    nr=json.loads((nvram_base/'kernel407-work/main-review.json').read_text())
    assert nr['parent']==394 and all(nr[k] for k in ('only_two_direct_objects_replaced','all_other_direct_objects_identical394','all_parent_archives_unchanged','NECP377v4_RAM375_counter380_preserved','radio_GPU_WRM_BPF_TKIP_unchanged','permissions_nonce_bootenv_owner_trust_unchanged','truthful_readonly_no_persistence','default_off_explicit_chosen_only','independent_reviews'))
    for v in nr['replacements'].values():assert hashlib.sha256(Path(v['path']).read_bytes()).hexdigest()==v['sha256']
    assert hashlib.sha256(Path(nr['only_added_archive']).read_bytes()).hexdigest()==nr['added_archive_sha256']
    ng=json.loads((nvram_base/'nvram-acceptance407.json').read_text())
    assert ng['status']=='PASS_OFFLINE_INTEGRATION_GATES' and hashlib.sha256((nvram_base/'nvram-acceptance407.json').read_bytes()).hexdigest()==nr['acceptance_sha256']
    for path,digest in ng['frozen_inputs'].items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest
    ctor=json.loads((nvram_base/'kernel407-work/startup-audit.json').read_text())
    assert ctor['normal_constructor_in_native_and_prelinked'] and ctor['native_constructor_before_iokit_post_constructor_init'] and ctor['source_CPP_init_before_root_creation']
else:
    assert args.nvram_proxy_ro is None

'''
replace(anchor,new+anchor)
anchor='if extra:\n'
new='''if args.kernel_number==407:
    anchor="    chosen[0][:] = [p for p in chosen[0] if p[0] != 'text-fixups']"
    assert t.count(anchor)==1
    code="    assert not any(p[0]=='k70-nvram-proxy-ro' for p in chosen[0])\\n"
    code+="    chosen[0].append(['k70-nvram-proxy-ro',0,struct.pack('<I',"+str(args.nvram_proxy_ro)+")])\\n"
    code+="    changes.append(dict(property='/chosen/k70-nvram-proxy-ro',value="+str(args.nvram_proxy_ro)+",origin='explicit immutable proxy399 control; no persistence'))\\n"
    t=t.replace(anchor,code+anchor)
'''
replace(anchor,new+anchor)
anchor="assert len(memory)==1 and memory[0][2]==struct.pack('<Q',owned_bytes)"
replace(anchor,anchor+"\nif args.kernel_number==407:\n    nvram_flag=[p for p in chosen[0] if p[0]=='k70-nvram-proxy-ro']\n    assert len(nvram_flag)==1 and nvram_flag[0][2]==struct.pack('<I',args.nvram_proxy_ro)")
anchor="verification['iokit_match_log_requested']=args.iokit_match_log"
replace(anchor,anchor+"\nverification['nvram_proxy_ro_requested']=args.nvram_proxy_ro\nverification['nvram_ro_selector_adt_read_back_equal']=args.kernel_number==407")
ast.parse(s)
q=P/'package-memory353.py';b=P/'package-memory353.pre407.py'
if not b.exists():b.write_bytes(q.read_bytes())
t=b.read_text()
assert t.count('394,395)')==4;t=t.replace('394,395)','394,395,407,408)')
assert t.count('395:394}')==1;t=t.replace('395:394}','395:394,407:407,408:407}')
ast.parse(t)
assert p.read_text() in (original,s) and q.read_text() in (b.read_text(),t),'Unrelated packager edit'
p.write_text(s);q.write_text(t)
report=dict(ancestor_membership_guards_retained=14,only_explicit_profiles={'407':{'kernel':407,'necp':1,'RAM':1,'nvram':1},'408':{'kernel':407,'necp':1,'RAM':1,'nvram':0}},bootarg_cap_increased=False,new_selector_in_ADT_before_owner_manifest=True,old_packager_sha256=hashlib.sha256(backup.read_bytes()).hexdigest(),new_packager_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),hardware_tested=False)
(Path('/mnt/d/K70-UniXNU-data')/'packager407-source-proof.json').write_text(json.dumps(report,indent=2)+'\n')
print('407 explicit qualified NVRAM pair; all14 ancestor checks retained; no cap/permissions/trust relaxation')
