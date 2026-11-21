#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?isolated runner output required}
mkdir -p "$out/work" "$out/text"
cd "$here"
xcodebuild -version > "$out/text/sdk.txt"
xcrun --sdk iphoneos --show-sdk-version >> "$out/text/sdk.txt"
flags=(-O1 -g -Wall -Wextra -Werror -Wno-unused-function)
xcrun --sdk macosx clang -std=gnu11 "${flags[@]}" -fsanitize=address,undefined tests/policy464.c -o "$out/work/policy"
"$out/work/policy" > "$out/text/policy.txt"
xcrun --sdk macosx clang "${flags[@]}" -fobjc-arc -DCP464_TEST=1 -fsanitize=address,undefined -framework Foundation -framework CoreFoundation src/crashprobe464.m -o "$out/work/fixture"
ASAN_OPTIONS=detect_leaks=0 python3 -B tests/fixtures.py "$out/work/fixture" > "$out/text/fixtures.txt"
xcrun --sdk iphoneos clang "${flags[@]}" -arch arm64 -mios-version-min=16.0 -fobjc-arc -framework Foundation -framework CoreFoundation src/crashprobe464.m -o "$out/work/crashprobe464"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.crashprobe464 --entitlements config/entitlements.plist "$out/work/crashprobe464"
codesign --verify --strict -vv "$out/work/crashprobe464" > "$out/text/codesign.txt" 2>&1
python3 -B ci/audit_binary.py "$out/work/crashprobe464" "$out/text/native-report.json"
# work/ contains all binaries and synthetic fixtures. NEVER upload it.
python3 - "$out/text" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);assert {x.name for x in p.iterdir()}=={'sdk.txt','policy.txt','fixtures.txt','codesign.txt','native-report.json'}
for x in p.iterdir():x.read_text()
PY
