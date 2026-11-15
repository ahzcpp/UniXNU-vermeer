#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?output};mkdir -p "$out/work" "$out/delivery";out=$(cd "$out" && pwd);w=$out/work;d=$out/delivery
cd "$here"
xcodebuild -version > "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-version > "$w/sdk.txt"
python3 tests/policy_check.py | tee "$d/policy.txt"
F=(-std=gnu11 -O2 -Wall -Wextra -Werror)
xcrun --sdk macosx clang "${F[@]}" -fsanitize=address,undefined tests/classify.c -o "$w/classify"
"$w/classify" | tee "$d/classifier.txt"
xcrun --sdk iphoneos clang "${F[@]}" -arch arm64 -mios-version-min=16.0 -fsyntax-only tests/classify.c
OF=(-std=gnu11 -fobjc-arc -fblocks -fmodules -O2 -Wall -Wextra -Werror)
FW=(-framework Foundation -framework Network)
xcrun --sdk macosx clang "${OF[@]}" src/unixnu-netpath376.m "${FW[@]}" -o "$w/netpath-macos"
# Runner's own network only. Not K70, Safari, account or Store evidence.
"$w/netpath-macos" --control-only > "$d/runner.txt"
python3 tests/check_output.py "$d/runner.txt"
xcrun --sdk iphoneos clang "${OF[@]}" -arch arm64 -mios-version-min=16.0 src/unixnu-netpath376.m "${FW[@]}" -o "$d/unixnu-netpath376"
strip -x "$d/unixnu-netpath376"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.netpath376 --entitlements launchd/entitlements.plist "$d/unixnu-netpath376"
codesign --verify --strict -vv "$d/unixnu-netpath376" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-netpath376" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-netpath376" > "$w/entitlements.xml" 2>/dev/null
otool -L "$d/unixnu-netpath376" > "$w/dylibs.txt"
otool -l "$d/unixnu-netpath376" > "$w/loadcmds.txt"
nm -u "$d/unixnu-netpath376" > "$d/imports.txt"
python3 ci/verify.py "$d" "$w"
