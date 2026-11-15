#!/bin/bash
# For Main's own CI (not pushed by Claude). macOS runner with the genuine iPhoneOS SDK; source-only inputs.
# Policy + prototype check vs the real macOS IOKit header, 2-sample smoke run on the runner (NOT K70 evidence), iOS 16 arm64 build,
# ad-hoc 4K-page signature with the existing no-container entitlement, report.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?out}; mkdir -p "$out/delivery" "$out/work"
w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version | tee "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-path | tee "$w/sdk.txt"; xcrun --sdk iphoneos --show-sdk-version | tee -a "$w/sdk.txt"
ls "$(xcrun --sdk iphoneos --show-sdk-path)/System/Library/Frameworks/IOKit.framework/" | tee "$w/iokit-sdk.txt"
python3 tests/policy_check.py | tee "$d/policy-check.txt"
FLAGS=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter)
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/proto_check" tests/proto_check.c -framework IOKit -framework CoreFoundation
"$w/proto_check" | tee "$d/proto-check.txt"
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/iokitbusy-macos" src/unixnu-iokitbusy366.c -framework IOKit -framework CoreFoundation
"$w/iokitbusy-macos" -w -n 2 -i 1 > "$d/runner-macos-run.txt" 2>&1 || true
cat "$d/runner-macos-run.txt"
grep -q '^IOKITBUSY366 RESULT result=done' "$d/runner-macos-run.txt"
test "$(grep -c '^IOKITBUSY366 sample n=' "$d/runner-macos-run.txt")" -eq 2
test "$(grep -c '^IOKITBUSY366 class n=' "$d/runner-macos-run.txt")" -eq 8
# -w smoke: walks run only if the runner's root is actually busy; either way the output must stay tagged and bounded
"$w/iokitbusy-macos" -w -n 2 -i 1 > "$d/runner-macos-walk.txt" 2>&1 || true
cat "$d/runner-macos-walk.txt"
grep -q '^IOKITBUSY366 RESULT result=done' "$d/runner-macos-walk.txt"
! grep -vq '^IOKITBUSY366 ' "$d/runner-macos-walk.txt"
test "$(grep -c '^IOKITBUSY366 walkclass ' "$d/runner-macos-walk.txt")" -le 32
! grep -vq '^IOKITBUSY366 ' "$d/runner-macos-run.txt"
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${FLAGS[@]}" -o "$d/unixnu-iokitbusy366" src/unixnu-iokitbusy366.c \
  -framework IOKit -framework CoreFoundation
strip -x "$d/unixnu-iokitbusy366" 2>/dev/null || true
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.iokitbusy366 --entitlements launchd/entitlements.plist "$d/unixnu-iokitbusy366"
codesign -d -vvv "$d/unixnu-iokitbusy366" > "$w/codesign.txt" 2>&1; cat "$w/codesign.txt"
codesign -d --entitlements - --xml "$d/unixnu-iokitbusy366" > "$w/entitlements.xml" 2>/dev/null || true
codesign --verify --strict -vv "$d/unixnu-iokitbusy366"
otool -L "$d/unixnu-iokitbusy366" | tee "$w/otool-L.txt"
otool -l "$d/unixnu-iokitbusy366" | grep -A4 LC_BUILD_VERSION | tee "$w/build-version.txt"
cp launchd/org.unixnu.diag.iokitbusy366.plist "$d/"; plutil -lint "$d/org.unixnu.diag.iokitbusy366.plist"
python3 - "$d" "$w" <<'EOF'
import hashlib, json, re, struct, sys, pathlib
d, w = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
b = (d/'unixnu-iokitbusy366').read_bytes()
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
assert set(libs) <= {'/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit', '/System/Library/Frameworks/IOKit.framework/IOKit',
                     '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation', '/usr/lib/libSystem.B.dylib'}, libs
rep = dict(binary='unixnu-iokitbusy366', binary_sha256=hashlib.sha256(b).hexdigest(), bytes=len(b),
           cdhash=re.search(r'CDHash=([0-9a-f]{40})', cs).group(1), page_size=pages[0], dylibs=libs, entitlements=ents,
           build_version=(w/'build-version.txt').read_text(), sdk=(w/'sdk.txt').read_text(), xcode=(w/'xcode.txt').read_text(),
           source_sha256=hashlib.sha256(pathlib.Path('src/unixnu-iokitbusy366.c').read_bytes()).hexdigest(),
           header_sha256=hashlib.sha256(pathlib.Path('src/iokitbusy_abi.h').read_bytes()).hexdigest(),
           plist_sha256=hashlib.sha256((d/'org.unixnu.diag.iokitbusy366.plist').read_bytes()).hexdigest(),
           runner_note='macOS runner smoke run only; not K70 evidence', hardware_tested=False, installed=False)
(d/'build-report.json').write_text(json.dumps(rep, indent=2) + '\n')
print(json.dumps({k: rep[k] for k in ('binary_sha256', 'cdhash', 'page_size', 'dylibs')}, indent=1))
EOF
