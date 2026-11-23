#!/bin/bash
# Genuine iPhoneOS SDK build of unixnu-sysapps475 (GitHub macOS runner). Owned source only; no device action.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?absolute isolated output directory required}
case "$out" in /*) ;; *) exit 2 ;; esac
mkdir -p "$out/work" "$out/delivery"; w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version > "$w/xcode.txt"
{ xcrun --sdk iphoneos --show-sdk-path; xcrun --sdk iphoneos --show-sdk-version; } > "$w/sdk.txt"
flags=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fobjc-arc)
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${flags[@]}" -framework Foundation src/unixnu-sysapps475.m -o "$d/unixnu-sysapps475"
# macOS syntax/link check of the same source (never run: the private frameworks do not exist there)
xcrun --sdk macosx clang "${flags[@]}" -framework Foundation src/unixnu-sysapps475.m -o "$w/sysapps475-macos"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.sysapps475 --entitlements launchd/entitlements.plist "$d/unixnu-sysapps475"
codesign --verify --strict -vv "$d/unixnu-sysapps475" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-sysapps475" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-sysapps475" > "$w/entitlements.xml" 2> "$w/entitlements-stderr.txt"
otool -L "$d/unixnu-sysapps475" > "$w/dylibs.txt"
otool -l "$d/unixnu-sysapps475" > "$w/load-commands.txt"
nm -u "$d/unixnu-sysapps475" > "$w/imports.txt"
cp launchd/org.unixnu.sysapps475.plist "$d/"; plutil -lint "$d/org.unixnu.sysapps475.plist"
cp launchd/entitlements.plist "$d/sysapps475-entitlements.plist"
for name in xcode.txt sdk.txt verify.txt codesign.txt entitlements.xml dylibs.txt load-commands.txt imports.txt; do cp "$w/$name" "$d/$name"; done
python3 - "$d" "$here" <<'PY'
import hashlib, json, plistlib, re, sys
from pathlib import Path
d, src = map(Path, sys.argv[1:]); b = d / 'unixnu-sysapps475'
H = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
ent = plistlib.loads((src / 'launchd/entitlements.plist').read_bytes())
assert plistlib.loads((d / 'entitlements.xml').read_bytes()) == ent
libs = [x.split()[0] for x in (d / 'dylibs.txt').read_text().splitlines()[1:] if x.strip()]
imp = sorted({x.strip() for x in (d / 'imports.txt').read_text().splitlines() if x.strip()})
for bad in ('_task_for_pid', '_kill', '_ptrace', '_socket', '_connect', '_sendto'):
    assert bad not in imp, bad
cd = re.search(r'CDHash=([0-9a-f]{40})', (d / 'codesign.txt').read_text()).group(1)
files = {p.name: {'sha256': H(p), 'bytes': p.stat().st_size} for p in sorted(d.iterdir()) if p.is_file() and p.name != 'build475-report.json'}
source = {p.relative_to(src).as_posix(): H(p) for p in sorted(src.rglob('*')) if p.is_file()}
rep = {'binary': 'unixnu-sysapps475', 'sha256': H(b), 'cdhash': cd, 'dylibs': libs, 'imports': imp, 'entitlements': ent,
       'files': files, 'source': source, 'hardware_tested': False}
(d / 'build475-report.json').write_text(json.dumps(rep, indent=2) + '\n')
print(json.dumps({k: rep[k] for k in ('sha256', 'cdhash', 'dylibs')}, indent=1))
PY
