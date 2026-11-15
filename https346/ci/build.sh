#!/bin/bash
# CI (macOS runner, genuine iPhoneOS SDK). Source-only inputs. Builds the own HTTPS probe for macOS (runs once on the RUNNER's
# network, which says nothing about K70) and for iOS 16 arm64, ad-hoc signs it with 4K code pages and the one existing
# no-container entitlement, and writes an audit report. Installs nothing.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?out}; mkdir -p "$out/delivery" "$out/work"
w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version | tee "$w/xcode.txt"
SDK=$(xcrun --sdk iphoneos --show-sdk-path); echo "sdk $SDK" | tee "$w/sdk.txt"
xcrun --sdk iphoneos --show-sdk-version | tee -a "$w/sdk.txt"
python3 tests/policy_check.py | tee "$d/policy-check.txt"
FLAGS=(-std=gnu11 -fobjc-arc -fmodules -O2 -Wall -Wextra -Werror -Wno-unused-parameter)
# 1. macOS build + one run on the runner's own network (observe only; result is informational)
xcrun --sdk macosx clang "${FLAGS[@]}" -o "$w/https346-macos" src/unixnu-https346.m -framework Foundation
set +e; "$w/https346-macos" > "$d/runner-macos-run.txt" 2>&1; rc=$?; set -e
echo "exit=$rc" >> "$d/runner-macos-run.txt"; cat "$d/runner-macos-run.txt"
# output hygiene: every line is a tagged probe line (or the exit marker); nothing body-like
python3 - "$d/runner-macos-run.txt" <<'EOF'
import sys, re
lines = open(sys.argv[1]).read().splitlines()
bad = [l for l in lines if not (l.startswith('HTTPS346 ') or l.startswith('exit='))]
bad += [l for l in lines if re.search(r'<|>|cookie|Set-Cookie|token|\d+\.\d+\.\d+\.\d+|[0-9a-f]{1,4}::|[0-9a-f]{1,4}(:[0-9a-f]{1,4}){3,}', l, re.I)]
assert not bad, bad
print('runner output hygiene ok lines=%d' % len(lines))
EOF
# 2. iOS 16 arm64
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${FLAGS[@]}" -o "$d/unixnu-https346" src/unixnu-https346.m -framework Foundation
strip -x "$d/unixnu-https346" 2>/dev/null || true
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.wifi.https346 --entitlements launchd/entitlements.plist "$d/unixnu-https346"
codesign -d -vvv "$d/unixnu-https346" > "$w/codesign.txt" 2>&1; cat "$w/codesign.txt"
codesign -d --entitlements - --xml "$d/unixnu-https346" > "$w/entitlements.xml" 2>/dev/null || codesign -d --entitlements - "$d/unixnu-https346" > "$w/entitlements.xml" 2>&1 || true
codesign --verify --strict -vv "$d/unixnu-https346" 2>&1 | tee "$w/verify.txt"
otool -L "$d/unixnu-https346" | tee "$w/otool-L.txt"
otool -l "$d/unixnu-https346" | grep -A4 LC_BUILD_VERSION | tee "$w/build-version.txt"
lipo -info "$d/unixnu-https346" | tee "$w/lipo.txt"
cp launchd/org.unixnu.wifi.https346.plist "$d/"; plutil -lint "$d/org.unixnu.wifi.https346.plist"
cp launchd/entitlements.plist "$d/https346-entitlements.plist"
python3 - "$d" "$w" <<'EOF'
import hashlib, json, re, sys, pathlib
d, w = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
cs = (w/'codesign.txt').read_text()
cdhash = re.search(r'CDHash=([0-9a-f]{40})', cs).group(1)
import struct
def cd_page_sizes(blob):
    # LC_CODE_SIGNATURE -> SuperBlob (big-endian) -> every CodeDirectory (0xfade0c02): pageSize is log2 at offset 39
    ncmds = struct.unpack_from('<I', blob, 16)[0]; off = 32; sig = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', blob, off)
        if cmd == 0x1d: sig = struct.unpack_from('<II', blob, off + 8)
        off += size
    so, ss = sig
    magic, length, count = struct.unpack_from('>III', blob, so)
    assert magic == 0xfade0cc0, hex(magic)
    sizes = []
    for i in range(count):
        typ, boff = struct.unpack_from('>II', blob, so + 12 + 8 * i)
        if struct.unpack_from('>I', blob, so + boff)[0] == 0xfade0c02:
            sizes.append(1 << blob[so + boff + 39])
    return sizes
sizes = cd_page_sizes((d/'unixnu-https346').read_bytes())
assert sizes and all(s == 4096 for s in sizes), sizes
page = str(sizes[0])
assert 'Identifier=org.unixnu.wifi.https346' in cs
ents = (w/'entitlements.xml').read_text()
assert 'com.apple.private.security.no-container' in ents, ents
libs = [l.split()[0] for l in (w/'otool-L.txt').read_text().splitlines()[1:] if l.strip()]
allowed = {'/System/Library/Frameworks/Foundation.framework/Foundation', '/usr/lib/libobjc.A.dylib', '/usr/lib/libSystem.B.dylib',
           '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation'}
assert set(libs) <= allowed, libs
b = (d/'unixnu-https346').read_bytes()
src = pathlib.Path('src/unixnu-https346.m').read_bytes()
rep = dict(binary='unixnu-https346', binary_sha256=hashlib.sha256(b).hexdigest(), bytes=len(b), cdhash=cdhash, page_size=int(page),
           identifier='org.unixnu.wifi.https346', entitlements=ents, codesign=cs, verify=(w/'verify.txt').read_text(), dylibs=libs,
           build_version=(w/'build-version.txt').read_text(), lipo=(w/'lipo.txt').read_text(), xcode=(w/'xcode.txt').read_text(),
           sdk=(w/'sdk.txt').read_text(), source_sha256=hashlib.sha256(src).hexdigest(),
           plist_sha256=hashlib.sha256((d/'org.unixnu.wifi.https346.plist').read_bytes()).hexdigest(),
           policy=(d/'policy-check.txt').read_text().strip(),
           runner_macos_result=[l for l in (d/'runner-macos-run.txt').read_text().splitlines() if ' RESULT ' in l],
           runner_note='macOS runner network only; not K70 evidence', hardware_tested=False, installed=False, source_only_inputs=True)
(d/'build-report.json').write_text(json.dumps(rep, indent=2) + '\n')
print(json.dumps({k: rep[k] for k in ('binary_sha256', 'cdhash', 'page_size', 'dylibs', 'runner_macos_result')}, indent=1))
EOF
