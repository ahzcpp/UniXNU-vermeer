"""Record reviewed offline integration gates; never device/Wi-Fi acceptance."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
import hashlib,json
D=Path('/mnt/d/K70-UniXNU-data');H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
R=D/'wifi-agent322/work/review404';review=json.loads((R/'READY.json').read_text());assert review['status']=='PASS_PROVIDER_SCOPED_CORE_GUARDS_REVIEW' and review['blocking_findings']==[]
assert H(R/'HANDOFF.txt')==review['handoff_sha256'] and H(R/'host-results.json')==review['host_proof_sha256'] and H(R/'static-proof.json')==review['static_proof_sha256']
C=D/'wifi-agent322/work/nvram-controller399';cr=json.loads((C/'READY.json').read_text());assert cr['status']=='SOURCE_FROZEN_READY_REQUIRES_MAIN_NATIVE_BUILD_AND_401V2_403' and cr['blocking_findings']==[] and cr['actual_prebind_header_read_required'] and cr['post_prebind_failure_pinned']
for rel,digest in cr['source_sha256'].items():assert H(C/rel)==digest
F=D/'claude-nvram-native405';text=(F/'HANDOFF.txt').read_text();assert 'no new defect found in the reviewed instructions' in text and '0xe00002cf' in text and 'nonce-seeds' in text
for line in (F/'SHA256SUMS').read_text().splitlines():
    digest,rel=line.split(None,1);assert H(F/rel.strip())==digest
files=[R/'READY.json',R/'HANDOFF.txt',R/'host-results.json',R/'static-proof.json',C/'READY.json',C/'HANDOFF.txt',C/'HOST-PROOF.json',F/'HANDOFF.txt',F/'STATUS.txt']
for rel in cr['source_sha256']:files.append(C/rel)
for folder,names in [('nvram-native399',['compile-review.json','object-audit.json','NativeProxyNVRAM399.o','libK70ProxyNVRAM399.a']),('nvram-core401/v2',['compile-review.json','object-audit.json','host-proof.json','serializer-host-proof.json','candidate/IONVRAM.cpo','candidate/IONVRAM.cpp','candidate/IONVRAMCHRPHandler.cpp']),('nvram-hook403',['compile-review.json','object-audit.json','host-proof.json','candidate/IOPlatformExpert.cpo','candidate/IOPlatformExpert.cpp']),('nvram-bank406',['proof.json'])]:
    files.extend(D/folder/name for name in names)
for folder,expected in [('nvram-native399','PASS_NATIVE_IMPORTS_AND_ATOMIC_ONLY_GETTERS'),('nvram-core401/v2','PASS_FOUR_SCOPED_NATIVE_FUNCTIONS_ABI_UNCHANGED'),('nvram-hook403','PASS_TWO_SCOPED_PE_METHODS_NO_EXPORTED_ABI_CHANGE')]:assert json.loads((D/folder/'object-audit.json').read_text())['status']==expected
assert json.loads((D/'nvram-bank406/proof.json').read_text())['status']=='PASS_ACTUAL_PROXY_BANK'
report=dict(status='PASS_OFFLINE_INTEGRATION_GATES',saved_local=datetime.now(timezone(timedelta(hours=8))).isoformat(),independent401_review_pass=True,independent_native405_review_pass=True,frozen_inputs={str(p):H(p) for p in files},hardware_tested=False,phone_accessed=False,limitations=['Controlled early-bootstrap admission, not cancellation of in-flight earlier cache writes.','Actual getter no calls; actual prebind read proof/lifetime required; normal builtin constructors must survive link before platform hook.','No physical persistence, manual resource/fakequiet, permission/nonce/bootenv/trust relaxation.','Native source/objects + mocked host proof only; Wi-Fi latency/app connectivity remain device tests.'],next='Only offline link407 and audit startup table/storage. DefaultOFF, eventual explicitADTselector pair. Phonepause remains.')
(D/'nvram-acceptance407.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS offline integration gates; 404+405 independent reviews bound to exact artifacts, not runtime acceptance')
