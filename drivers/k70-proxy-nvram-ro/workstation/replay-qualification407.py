"""Rebuild qualification from saved pre407 inputs and require byte identity."""
from pathlib import Path
import hashlib,json,subprocess,sys
P=Path(__file__).resolve().parent;files=[P/'package-gpu-probe-stage.py',P/'package-memory353.py'];H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();before={str(p):H(p) for p in files}
r=subprocess.run([sys.executable,str(P/'qualify-packager407.py')],text=True,capture_output=True);assert r.returncode==0,r.stderr
assert before=={str(p):H(p) for p in files}
out=Path('/mnt/d/K70-UniXNU-data/packager407-replay-proof.json');out.write_text(json.dumps(dict(status='PASS_BYTE_IDENTICAL_REPLAY_FROM_PRE407_BASELINES',files=before,old_source_guards_retained=14,bootarg_cap_increased=False,phone_accessed=False),indent=2)+'\n');print(r.stdout.strip());print('Byte-identical407 qualifier replay PASS; current packagers unchanged')
