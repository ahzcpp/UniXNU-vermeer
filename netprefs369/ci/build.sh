#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?output directory}; mkdir -p "$out/work" "$out/delivery";out=$(cd "$out" && pwd);w=$out/work;d=$out/delivery
cd "$here"
xcodebuild -version > "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-path > "$w/sdk.txt"
xcrun --sdk iphoneos --show-sdk-version >> "$w/sdk.txt"
python3 tests/policy_check.py | tee "$d/policy.txt"
python3 tests/io_test.py | tee "$d/io-test.txt"
F=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation)
FW=(-framework CoreFoundation -framework SystemConfiguration)
xcrun --sdk macosx clang "${F[@]}" -fsyntax-only tests/proto_check.c
xcrun --sdk macosx clang "${F[@]}" -fsanitize=address,undefined tests/test_cf.c -framework CoreFoundation -o "$w/test-cf"
"$w/test-cf" | tee "$d/cf-fixtures.txt"
xcrun --sdk iphoneos clang "${F[@]}" -arch arm64 -mios-version-min=16.0 -fsyntax-only tests/test_cf.c
xcrun --sdk macosx clang "${F[@]}" src/unixnu-netprefs369.c "${FW[@]}" -o "$w/netprefs-macos"
"$w/netprefs-macos" -n 1 -i 1 > "$d/runner.txt"
python3 - "$d/runner.txt" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text(); lines=s.splitlines()
assert 1<=len(lines)<=256 and all(x.startswith('NETPREFS369 ') for x in lines)
assert 'result=1 samples=1' in s
assert not re.search(r'/Users/|/private/|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}',s)
PY
xcrun --sdk iphoneos clang "${F[@]}" -arch arm64 -mios-version-min=16.0 src/unixnu-netprefs369.c "${FW[@]}" -o "$d/unixnu-netprefs369"
strip -x "$d/unixnu-netprefs369"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.netprefs369 --entitlements launchd/entitlements.plist "$d/unixnu-netprefs369"
codesign --verify --strict -vv "$d/unixnu-netprefs369" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-netprefs369" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-netprefs369" > "$w/entitlements.xml" 2>/dev/null
otool -L "$d/unixnu-netprefs369" > "$w/dylibs.txt"
otool -l "$d/unixnu-netprefs369" > "$w/loadcmds.txt"
nm -u "$d/unixnu-netprefs369" > "$d/imports.txt"
python3 ci/verify.py "$d" "$w"
