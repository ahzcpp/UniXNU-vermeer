#!/bin/bash
# Isolated genuine macOS SDK build; no phone, package or launchd action.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?absolute isolated output directory required}
case "$out" in /*) ;; *) exit 2 ;; esac
mkdir -p "$out/work" "$out/delivery"
cd "$here"
w=$out/work
d=$out/delivery
python3 -B tests/policy_check.py | tee "$w/policy.txt"
python3 -B tests/run_host.py | tee "$w/legacy-host.txt"
python3 -B tests/run_host450.py | tee "$w/host450.txt"
python3 -B tests/run_fs456.py | tee "$w/fs456.txt"
python3 - <<'PY'
from pathlib import Path
s=Path('src/proc450_webkit.h').read_text()
mirror=s[s.index('#define NS450_BSDINFO'):s.index('int proc_pidpath')]
assert Path('tests/abi450_mirror.inc').read_text()==mirror
PY
flags=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter)
xcodebuild -version > "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-path > "$w/sdk.txt"
xcrun --sdk iphoneos --show-sdk-version >> "$w/sdk.txt"
xcrun --sdk macosx clang "${flags[@]}" tests/abi_check.c -o "$w/abi389"
"$w/abi389" > "$w/abi389.txt"
xcrun --sdk macosx clang "${flags[@]}" tests/abi450.c -o "$w/abi450"
"$w/abi450" > "$w/abi450.txt"
xcrun --sdk macosx clang "${flags[@]}" -fsyntax-only tests/memory_abi.c
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${flags[@]}" -fsyntax-only tests/memory_abi.c
xcrun --sdk macosx clang "${flags[@]}" src/unixnu-proc389.c -o "$w/proc450-macos"
# Ordinary runner user, fixed finite observer, no Safari launch or network traffic.
"$w/proc450-macos" -w -f > "$w/runner-wk450.txt" 2>&1
python3 tests/check_smoke456.py "$w/runner-wk450.txt"
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${flags[@]}" src/unixnu-proc389.c -o "$d/unixnu-proc456"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.proc456 --entitlements launchd/entitlements.plist "$d/unixnu-proc456"
codesign --verify --strict -vv "$d/unixnu-proc456" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-proc456" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-proc456" > "$w/entitlements.xml" 2> "$w/entitlements-stderr.txt"
otool -L "$d/unixnu-proc456" > "$w/dylibs.txt"
otool -l "$d/unixnu-proc456" > "$w/load-commands.txt"
nm -u "$d/unixnu-proc456" > "$w/imports.txt"
cp launchd/org.unixnu.diag.proc456.plist "$d/"
plutil -lint "$d/org.unixnu.diag.proc456.plist"
cp launchd/entitlements.plist "$d/proc456-entitlements.plist"
for name in fs456.txt policy.txt legacy-host.txt host450.txt abi389.txt abi450.txt runner-wk450.txt xcode.txt sdk.txt verify.txt codesign.txt entitlements.xml dylibs.txt load-commands.txt imports.txt; do
  cp "$w/$name" "$d/$name"
done
python3 - "$d" "$w" <<'PY'
from pathlib import Path
import hashlib,json,plistlib,re,struct,sys
d,w=map(Path,sys.argv[1:]);p=d/'unixnu-proc456';b=p.read_bytes()
assert plistlib.loads((w/'entitlements.xml').read_bytes())=={'com.apple.private.security.no-container':True}
libs=[x.split()[0] for x in (w/'dylibs.txt').read_text().splitlines()[1:] if x.strip()]
assert libs==['/usr/lib/libSystem.B.dylib']
off=32;so=None
for _ in range(struct.unpack_from('<I',b,16)[0]):
    cmd,size=struct.unpack_from('<II',b,off)
    if cmd==0x1d:so,ss=struct.unpack_from('<II',b,off+8)
    off+=size
assert so is not None
magic,length,count=struct.unpack_from('>III',b,so);assert magic==0xfade0cc0
pages=[]
for i in range(count):
    typ,bo=struct.unpack_from('>II',b,so+12+8*i)
    if struct.unpack_from('>I',b,so+bo)[0]==0xfade0c02:pages.append(1<<b[so+bo+39])
assert pages and all(n==4096 for n in pages)
assert '_proc_pidpath' in (w/'imports.txt').read_text()
inputs=[p for folder in ['src','tests','ci','launchd'] for p in Path(folder).rglob('*') if p.is_file()]
inputs.append(Path('BASE-INPUTS.json'))
rep={'binary':p.name,'sha256':hashlib.sha256(b).hexdigest(),'cdhash':re.search(r'CDHash=([0-9a-f]{40})',(w/'codesign.txt').read_text()).group(1),'page_sizes':pages,'dylibs':libs,'source':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'files':{p.name:{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in d.iterdir() if p.is_file()},'fs456_fixture_ASan_UBSan':True,'filesystem_uses_genuine_SDK_structs':True,'host_ASan_UBSan':True,'genuine_macOS_ABI_checks':True,'runner_wk450_smoke':True,'hardware_tested':False,'installed':False,'actual20H392_import_audit':'PENDING','actual_target_XNU_ABI':'PENDING','admitted_for_deployment':False}
(d/'build456-report.json').write_text(json.dumps(rep,indent=2)+'\n')
PY
