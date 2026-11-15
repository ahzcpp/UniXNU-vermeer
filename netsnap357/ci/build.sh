#!/bin/bash
# CI (macOS runner, genuine iPhoneOS SDK). Source-only. Builds the own read-only snapshot probe for macOS (2 snapshots on the
# runner as a smoke test; not K70 evidence) and iOS 16 arm64, ad-hoc signs with 4K pages + the existing no-container entitlement.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?out}; mkdir -p "$out/delivery" "$out/work"
w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version | tee "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-path | tee "$w/sdk.txt"; xcrun --sdk iphoneos --show-sdk-version | tee -a "$w/sdk.txt"
python3 tests/policy_check.py | tee "$d/policy-check.txt"
FLAGS=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter)
xcrun --sdk macosx clang "${FLAGS[@]}" -fsyntax-only tests/memory_abi.c
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${FLAGS[@]}" -fsyntax-only tests/memory_abi.c
# own proc_info mirror must equal the real macOS header layout (the iPhoneOS SDK does not ship sys/proc_info.h)
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/abi_check" tests/abi_check.c
"$w/abi_check" | tee "$d/abi-check.txt"
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/netsnap-macos" src/unixnu-netsnap357.c
sudo "$w/netsnap-macos" -m -n 2 -i 2 > "$d/runner-macos-run.txt" 2>&1 || true
cat "$d/runner-macos-run.txt"
python3 - "$d/runner-macos-run.txt" <<'EOF'
import sys, re
lines = open(sys.argv[1]).read().splitlines()
assert any(l.startswith('NETSNAP357 RESULT result=done') for l in lines), 'no RESULT'
bad = [l for l in lines if not l.startswith('NETSNAP357 ') or re.search(r'\d+\.\d+\.\d+\.\d+|[0-9a-f]{1,4}::|/Users/|/private/', l)]
assert not bad, bad
print('runner smoke ok lines=%d' % len(lines))
EOF
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${FLAGS[@]}" -o "$d/unixnu-netsnap357" src/unixnu-netsnap357.c
strip -x "$d/unixnu-netsnap357" 2>/dev/null || true
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.netsnap357 --entitlements launchd/entitlements.plist "$d/unixnu-netsnap357"
codesign -d -vvv "$d/unixnu-netsnap357" > "$w/codesign.txt" 2>&1; cat "$w/codesign.txt"
codesign -d --entitlements - --xml "$d/unixnu-netsnap357" > "$w/entitlements.xml" 2>/dev/null || codesign -d --entitlements - "$d/unixnu-netsnap357" > "$w/entitlements.xml" 2>&1 || true
codesign --verify --strict -vv "$d/unixnu-netsnap357" 2>&1 | tee "$w/verify.txt"
otool -L "$d/unixnu-netsnap357" | tee "$w/otool-L.txt"
otool -l "$d/unixnu-netsnap357" | grep -A4 LC_BUILD_VERSION | tee "$w/build-version.txt"
cp launchd/org.unixnu.diag.netsnap357.plist "$d/"; plutil -lint "$d/org.unixnu.diag.netsnap357.plist"
cp launchd/entitlements.plist "$d/netsnap357-entitlements.plist"
python3 - "$d" "$w" <<'EOF'
import hashlib, json, re, struct, sys, pathlib
d, w = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
b = (d/'unixnu-netsnap357').read_bytes()
ncmds = struct.unpack_from('<I', b, 16)[0]; off = 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', b, off)
    if cmd == 0x1d: so, ss = struct.unpack_from('<II', b, off + 8)
    off += size
magic, length, count = struct.unpack_from('>III', b, so)
pages = [1 << b[so + bo + 39] for typ, bo in (struct.unpack_from('>II', b, so + 12 + 8 * i) for i in range(count))
         if struct.unpack_from('>I', b, so + bo)[0] == 0xfade0c02]
assert pages and all(p == 4096 for p in pages), pages
cs = (w/'codesign.txt').read_text()
ents = (w/'entitlements.xml').read_text(); assert 'com.apple.private.security.no-container' in ents
libs = [l.split()[0] for l in (w/'otool-L.txt').read_text().splitlines()[1:] if l.strip()]
assert libs == ['/usr/lib/libSystem.B.dylib'], libs
rep = dict(binary='unixnu-netsnap357', binary_sha256=hashlib.sha256(b).hexdigest(), bytes=len(b),
           cdhash=re.search(r'CDHash=([0-9a-f]{40})', cs).group(1), page_size=pages[0], identifier='org.unixnu.diag.netsnap357',
           entitlements=ents, codesign=cs, verify=(w/'verify.txt').read_text(), dylibs=libs, build_version=(w/'build-version.txt').read_text(),
           xcode=(w/'xcode.txt').read_text(), sdk=(w/'sdk.txt').read_text(),
           source_sha256=hashlib.sha256(pathlib.Path('src/unixnu-netsnap357.c').read_bytes()).hexdigest(),
           plist_sha256=hashlib.sha256((d/'org.unixnu.diag.netsnap357.plist').read_bytes()).hexdigest(),
           policy=(d/'policy-check.txt').read_text().strip(), runner_note='macOS runner smoke run only; not K70 evidence',
           hardware_tested=False, installed=False, source_only_inputs=True)
(d/'build-report.json').write_text(json.dumps(rep, indent=2) + '\n')
print(json.dumps({k: rep[k] for k in ('binary_sha256', 'cdhash', 'page_size', 'dylibs')}, indent=1))
EOF
