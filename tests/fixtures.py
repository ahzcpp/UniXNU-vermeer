"""Synthetic reports ONLY. Run the real Foundation parser/reader on macOS."""
import copy,json,os,pathlib,subprocess,sys,tempfile,time
binary=sys.argv[1];cases=0
PRIVATE='PRIVATE_USER_SENTINEL';URL='https://private.invalid/secret';ID='12345678-1234-1234-1234-123456789ABC'
base={'procName':'MobileSafari','pid':123,'termination':{'namespace':'FRONTBOARD','code':0x8badf00d,'description':'scene-update watchdog '+PRIVATE+' '+URL+' '+ID},'exception':{'type':'EXC_CRASH','signal':'SIGKILL'},'threads':[{'queue':'com.apple.main-thread','frames':[{'imageIndex':0,'imageOffset':123,'symbol':'mach_msg2_trap','symbolLocation':4},{'imageIndex':1,'imageOffset':456,'symbol':PRIVATE},{'imageIndex':0,'imageOffset':789,'symbol':'IPC::Connection::waitForSyncReply('+PRIVATE+')'}]},{'triggered':True,'frames':[{'imageIndex':0,'imageOffset':88,'symbol':'_dispatch_sync_f_slow'}]}],'faultingThread':1,'usedImages':[{'name':'libsystem_kernel.dylib','path':'/Users/'+PRIVATE,'uuid':ID},{'name':PRIVATE,'path':URL}],'userID':PRIVATE,'captureTime':ID,'asi':{'Private': [URL]},'crashReporterKey':ID}
keys={'event','report','status','reason','errno','role','pid_known','pid','termination_known','namespace','code_known','code','watchdog_code','reason_tags','description_verbatim','exception_type','signal','atomic_snapshot','index','main_confirmed','thread0_candidate','triggered','frames_known','frames_total','frames_capped','thread','frame','symbol','symbol_known','symbol_family','image_kind','image_index_known','image_index','image_offset_known','image_offset','symbol_location_known','symbol_location','entries','candidate_count','candidates_dropped','read_count','reports_not_read','scan_errors','scan_capped','time_capped','absence_proven','skipped_type','skipped_size','skipped_age','file_time_known','mtime_epoch','current_epoch','age_seconds','file_time_not_crash_time'}
def encoded(body=base,header=None):return (json.dumps(header or {'name':'MobileSafari','bug_type':'309','incident_id':ID})+'\n'+json.dumps(body)).encode()
def run(files,setup=None):
 global cases
 with tempfile.TemporaryDirectory(prefix='cp464-synthetic-') as tmp:
  root=pathlib.Path(tmp)
  for name,data in files.items():p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
  if setup:setup(root)
  result=subprocess.run([binary,'--fixture-root',tmp],capture_output=True,timeout=20)
  assert result.returncode==0,(result.returncode,result.stderr.decode(errors='replace'))
  raw=result.stdout.decode();assert len(result.stdout)<=60000 and len(raw.splitlines())<=230
  for secret in [PRIVATE,URL,ID,tmp,'/private/var', 'MobileSafari-test.ips']:assert secret not in raw,secret
  assert all(x.startswith('CP464 ') for x in raw.splitlines())
  rows=[json.loads(x[6:]) for x in raw.splitlines()];assert rows and rows[-1]['event']=='end'
  for row in rows:assert set(row)<=keys,set(row)-keys
  assert rows[-1]['absence_proven'] is False and rows[-1]['atomic_snapshot'] is False
  cases+=1;return rows
def reports(rows):return [x for x in rows if x['event']=='report']
r=run({'MobileSafari-test.ips':encoded()});assert reports(r)[0]['watchdog_code'];assert [x['symbol'] for x in r if x['event']=='frame'][:2]==['mach_msg2_trap','unknown'];assert any(x.get('symbol_family')=='ipc_sync_wait' for x in r);assert len([x for x in r if x['event']=='thread'])==2
mtime=int(time.time())-7200
r=run({'MobileSafari-test.ips':encoded()},lambda root:os.utime(root/'MobileSafari-test.ips',(mtime,mtime)))
p=reports(r)[0];assert p['mtime_epoch']==mtime and 7200<=p['age_seconds']<7230 and p['current_epoch']-p['mtime_epoch']==p['age_seconds'] and p['file_time_not_crash_time']
mtime=int(time.time())+120
r=run({'MobileSafari-test.ips':encoded()},lambda root:os.utime(root/'MobileSafari-test.ips',(mtime,mtime)))
p=reports(r)[0];assert -120<=p['age_seconds']<0 and p['file_time_known']
for bad in [b'',b'{}',b'{}\n{',b'not json\n{}',b'{}\n'+b'['*70+b']'*70,b'{}\n{"x":"\x00"}']:
 r=run({'MobileSafari-test.ips':bad});assert not any(x.get('status')=='parsed' for x in r)
for key,value in [('bug_type','288'),('bug_type',True),('bug_type',309.0)]:
 h={'name':'MobileSafari',key:value};r=run({'MobileSafari-test.ips':encoded(header=h)});assert not any(x.get('status')=='parsed' for x in r)
for code,known in [('0x8badf00d',True),(-1,False),(True,False),(float('nan'),False),('0x8badf00d/path',False),('4294967296',False)]:
 b=copy.deepcopy(base);b['termination']['code']=code;r=run({'MobileSafari-test.ips':encoded(b)});p=reports(r)[0];assert p.get('code_known',False)==known
b=copy.deepcopy(base);b['procName']='Other';assert reports(run({'MobileSafari-test.ips':encoded(b)}))[0]['reason']=='process_unconfirmed'
b=copy.deepcopy(base);b['threads'][0].pop('queue');r=run({'MobileSafari-test.ips':encoded(b)});assert next(x for x in r if x['event']=='thread')['thread0_candidate']
b=copy.deepcopy(base);b['threads'][0]['triggered']=True;r=run({'MobileSafari-test.ips':encoded(b)});assert any(x.get('reason')=='ambiguous_trigger' for x in r)
b=copy.deepcopy(base);b['faultingThread']=0;r=run({'MobileSafari-test.ips':encoded(b)});assert any(x.get('reason')=='trigger_mismatch' for x in r)
b=copy.deepcopy(base);b['threads'][0]['frames']*=30;r=run({'MobileSafari-test.ips':encoded(b)});assert sum(x['event']=='frame' and x['thread']==0 for x in r)==24
r=run({'MobileSafari-test.ips':b'x'*(2*1024*1024+1)});assert r[-1]['skipped_size']==1
r=run({'Other-test.ips':encoded()});assert r[-1]['read_count']==0
r=run({'MobileSafari-test.ips':encoded()},lambda root:os.utime(root/'MobileSafari-test.ips',(time.time()-90000,)*2));assert r[-1]['skipped_age']==1
r=run({'MobileSafari-test.ips':encoded()},lambda root:os.link(root/'MobileSafari-test.ips',root/'MobileSafari-copy.ips'));assert r[-1]['skipped_type']==2
r=run({'payload':encoded()},lambda root:os.symlink(root/'payload',root/'MobileSafari-test.ips'));assert r[-1]['skipped_type']==1
r=run({'Retired/MobileSafari-test.ips':encoded()});assert r[-1]['read_count']==1
r=run({f'MobileSafari-{i}.ips':encoded() for i in range(40)});assert r[-1]['read_count']==4 and r[-1]['candidates_dropped']==8
r=run({f'Other-{i}.ips':b'{}' for i in range(520)});assert r[-1]['scan_capped']
result=subprocess.run([binary,'--fixture-root','/this-path-does-not-exist-cp464'],capture_output=True,timeout=20);assert result.returncode==1 and b'root_unavailable' in result.stdout
print(json.dumps({'status':'PASS_SYNTHETIC_FOUNDATION_READER_PRIVACY','cases':cases+1,'real_reports_read':False,'atomic_read_claimed':False}))
