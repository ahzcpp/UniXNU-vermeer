"""Build only the own fixed read-only capability probe with Apple's real SDK."""
from pathlib import Path
import hashlib,json,subprocess,sys,re,plistlib
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
binary=out/'unixnu-mgprobe';source=root/'src/unixnu-mgprobe.c';ent=root/'launchd/entitlements.plist'
assert sys.platform=='darwin'
def run(*a):return subprocess.check_output([str(x) for x in a],stderr=subprocess.STDOUT,text=True)
sdk=run('xcrun','--sdk','iphoneos','--show-sdk-path').strip()
command=['xcrun','--sdk','iphoneos','clang','-target','arm64-apple-ios16.0','-isysroot',sdk,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(source),'-framework','CoreFoundation','-o',str(binary)]
run(*command)
run('codesign','--force','--sign','-','--pagesize','4096','--identifier','org.unixnu.wifi.mgprobe','--entitlements',ent,binary)
run('codesign','--verify','--strict',binary)
details=run('codesign','--display','--verbose=4',binary)
cd=re.search(r'^CDHash=([0-9a-f]{40})$',details,re.M)[1]
assert 'Page size=4096' in details
report=dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),cdhash=cd,command=command,codesign=details,hardware_tested=False,read_only=True,fixed_keys_only=True,contains_unique_identifiers=False,installed=False)
(out/'mgprobe-build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['binary_sha256','cdhash','hardware_tested']}))
