#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?isolated runner output required}
mkdir -p "$out/work" "$out/text" "$out/delivery"
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
# Only the owned production executable is retained. No fixture, SDK or framework.
cp "$out/work/crashprobe464" "$out/delivery/crashprobe464"
cp config/entitlements.plist "$out/delivery/entitlements.plist"
cp "$out/text/native-report.json" "$out/delivery/native-report.json"
python3 - "$out/text" "$out/delivery" <<'PY'
from pathlib import Path
import hashlib,json,os,sys
p,d=map(Path,sys.argv[1:]);assert {x.name for x in p.iterdir()}=={'sdk.txt','policy.txt','fixtures.txt','codesign.txt','native-report.json'}
for x in p.iterdir():x.read_text()
assert 'PASS bounded' in (p/'policy.txt').read_text()
f=json.loads((p/'fixtures.txt').read_text());assert f['status']=='PASS_SYNTHETIC_FOUNDATION_READER_PRIVACY'
proof={'status':'PASS_CI_GENUINE_SDK_SYNTHETIC_ASAN_UBSAN_AND_SIGNATURE','commit':os.environ['GITHUB_SHA'],'run_id':os.environ['GITHUB_RUN_ID'],'run_attempt':os.environ['GITHUB_RUN_ATTEMPT'],'fixture_cases':f['cases'],'real_reports_read':False,'fixture_report_uploaded':False,'SDK_or_framework_uploaded':False,'files':{x.name:{'bytes':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in d.iterdir() if x.is_file()}}
(d/'CI-PROOF.json').write_text(json.dumps(proof,indent=2)+'\n')
assert {x.name for x in d.iterdir()}=={'crashprobe464','entitlements.plist','native-report.json','CI-PROOF.json'}
PY
