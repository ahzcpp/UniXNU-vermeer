#!/bin/bash
# For Main's own CI (NOT pushed or triggered by Claude). macOS runner with the genuine iPhoneOS SDK; source-only inputs.
# Policy check, prototype check vs the real macOS IOKit header, host simulation (synthetic registries, NOT K70 evidence),
# macOS runner smoke run (NOT K70 evidence), iOS 16 arm64 build, ad-hoc 4K-page signature with the existing no-container
# entitlement, CodeDirectory page-size readback, dylib allow-list, report.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?out}; mkdir -p "$out/delivery" "$out/work"
w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version | tee "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-path | tee "$w/sdk.txt"; xcrun --sdk iphoneos --show-sdk-version | tee -a "$w/sdk.txt"
python3 tests/policy_check.py | tee "$d/policy-check.txt"
FLAGS=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter)
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/proto_check" tests/proto_check.c -framework IOKit
"$w/proto_check" | tee "$d/proto-check.txt"
# host simulation: same source against tests/mock_iokit.c (ASan/UBSan, reference-leak check); host mach stub only here
CC="$(xcrun --sdk macosx -f clang)" python3 tests/run_sim.py | tee "$d/sim.txt"

grep -q '^SIM PASS' "$d/sim.txt"
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/iokitholder-macos" src/unixnu-iokitholder385.c -framework IOKit
"$w/iokitholder-macos" -f > "$d/runner-macos-run.txt" 2>&1 || true
cat "$d/runner-macos-run.txt"
grep -q '^IOKITHOLDER385 RESULT result=done samples=1' "$d/runner-macos-run.txt"
grep -q '^IOKITHOLDER385 walk n=0 ' "$d/runner-macos-run.txt"
! grep -vq '^IOKITHOLDER385 ' "$d/runner-macos-run.txt"
test "$(grep -c '^IOKITHOLDER385 candidate ' "$d/runner-macos-run.txt")" -le 12
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${FLAGS[@]}" -o "$d/unixnu-iokitholder385" src/unixnu-iokitholder385.c \
  -framework IOKit
strip -x "$d/unixnu-iokitholder385" 2>/dev/null || true
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.iokitholder385 --entitlements launchd/entitlements.plist "$d/unixnu-iokitholder385"
codesign -d -vvv "$d/unixnu-iokitholder385" > "$w/codesign.txt" 2>&1; cat "$w/codesign.txt"
codesign -d --entitlements - --xml "$d/unixnu-iokitholder385" > "$w/entitlements.xml" 2>/dev/null || true
codesign --verify --strict -vv "$d/unixnu-iokitholder385"
otool -L "$d/unixnu-iokitholder385" | tee "$w/otool-L.txt"
otool -l "$d/unixnu-iokitholder385" | grep -A4 LC_BUILD_VERSION | tee "$w/build-version.txt"
nm -u "$d/unixnu-iokitholder385" | tee "$w/imports.txt"
for p in launchd/org.unixnu.diag.iokitholder385.plist launchd/org.unixnu.diag.iokitholder385.interval.plist; do cp "$p" "$d/"; plutil -lint "$d/$(basename "$p")"; done
python3 - "$d" "$w" <<'EOF'
import hashlib, json, re, struct, sys, pathlib
d, w = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
b = (d/'unixnu-iokitholder385').read_bytes()
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
                     '/usr/lib/libSystem.B.dylib'}, libs
iokit = sorted(s.strip().lstrip('_') for s in (w/'imports.txt').read_text().split() if s.strip().startswith('_IO'))
expected = {'IORegistryEntryFromPath', 'IORegistryEntryCreateIterator', 'IORegistryEntryGetChildIterator', 'IORegistryEntryGetParentEntry',
            'IOIteratorNext', 'IOIteratorIsValid', 'IOObjectIsEqualTo', 'IOObjectRelease', 'IOServiceGetBusyState', 'IOObjectGetClass'}
assert set(iokit) <= expected, iokit   # all 10 verified present in the 20H392 IOKit export trie (iokit-exports385.json)
rep = dict(binary='unixnu-iokitholder385', binary_sha256=hashlib.sha256(b).hexdigest(), bytes=len(b),
           cdhash=re.search(r'CDHash=([0-9a-f]{40})', cs).group(1), page_size=pages[0], dylibs=libs, iokit_imports=iokit, entitlements=ents,
           build_version=(w/'build-version.txt').read_text(), sdk=(w/'sdk.txt').read_text(), xcode=(w/'xcode.txt').read_text(),
           source_sha256=hashlib.sha256(pathlib.Path('src/unixnu-iokitholder385.c').read_bytes()).hexdigest(),
           header_sha256=hashlib.sha256(pathlib.Path('src/iokitholder_abi.h').read_bytes()).hexdigest(),
           runner_note='macOS runner smoke run and host simulation only; not K70 evidence', hardware_tested=False, installed=False)
(d/'build-report.json').write_text(json.dumps(rep, indent=2) + '\n')
print(json.dumps({k: rep[k] for k in ('binary_sha256', 'cdhash', 'page_size', 'dylibs', 'iokit_imports')}, indent=1))
EOF
