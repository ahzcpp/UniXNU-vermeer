#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?output};mkdir -p "$out/work" "$out/delivery";out=$(cd "$out" && pwd);w=$out/work;d=$out/delivery
cd "$here"
xcodebuild -version > "$w/xcode.txt"
xcrun --sdk iphoneos --show-sdk-version > "$w/sdk.txt"
python3 tests/policy437.py | tee "$d/policy.txt"
F=(-std=gnu11 -O2 -Wall -Wextra -Werror)
xcrun --sdk macosx clang "${F[@]}" -fsanitize=address,undefined tests/classify.c -o "$w/classify"
"$w/classify" | tee "$d/classifier.txt"
xcrun --sdk macosx clang "${F[@]}" -fsanitize=address,undefined tests/test_identity424.c -o "$w/identity424"
"$w/identity424" | tee "$d/identity424.txt"
xcrun --sdk iphoneos clang "${F[@]}" -arch arm64 -mios-version-min=16.0 -fsyntax-only tests/test_identity424.c
xcrun --sdk iphoneos clang "${F[@]}" -arch arm64 -mios-version-min=16.0 -fsyntax-only tests/classify.c
xcrun --sdk macosx clang "${F[@]}" -fsanitize=address,undefined tests/test_watchdog437.c -o "$w/watchdog437"
python3 tests/check_watchdog437.py "$w/watchdog437" | tee "$d/watchdog437.txt"
xcrun --sdk macosx clang "${F[@]}" -fsanitize=address,undefined tests/test_formats437.c -o "$w/formats437"
"$w/formats437" | tee "$d/formats437.txt"
OF=(-std=gnu11 -fobjc-arc -fblocks -fmodules -O2 -Wall -Wextra -Werror)
FW=(-framework Foundation -framework Network)
xcrun --sdk macosx clang "${OF[@]}" src/unixnu-netbody437.m "${FW[@]}" -o "$w/netpath-macos"
# Never invoke --drop-to-mobile on a CI runner. Callback tests change no real IDs.
# Runner's own network only. Not K70, Safari, account or Store evidence.
"$w/netpath-macos" --control-only > "$d/runner.txt"
python3 tests/check_output.py "$d/runner.txt"
xcrun --sdk iphoneos clang "${OF[@]}" -arch arm64 -mios-version-min=16.0 src/unixnu-netbody437.m "${FW[@]}" -o "$d/unixnu-netbody437"
strip -x "$d/unixnu-netbody437"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.netbody437 --entitlements launchd/entitlements.plist "$d/unixnu-netbody437"
codesign --verify --strict -vv "$d/unixnu-netbody437" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-netbody437" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-netbody437" > "$w/entitlements.xml" 2>/dev/null
otool -L "$d/unixnu-netbody437" > "$w/dylibs.txt"
otool -l "$d/unixnu-netbody437" > "$w/loadcmds.txt"
nm -u "$d/unixnu-netbody437" > "$d/imports.txt"
python3 ci/verify.py "$d" "$w"
