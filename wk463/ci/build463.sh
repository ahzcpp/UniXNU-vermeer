#!/bin/bash
# Genuine macOS/iPhoneOS SDK build for unixnu-wk463 (GitHub macOS runner). Owned source only; no device action.
#  1. Fetch five xnu-10063.141.1 headers from the public apple-oss-distributions/xnu tag; gen_real_structs.py enforces
#     pinned sha256 for each; real_structs.inc is generated here, never committed.
#  2. ABI offsets vs real struct text; parser semantics + truncation/corruption fuzz under ASan/UBSan.
#  3. iPhoneOS arm64 build (-Wall -Wextra -Werror), ad-hoc signature with the single no-container entitlement.
#  4. macOS build of the same source + finite real-kernel smoke as root on the runner: renamed copies of /bin/sleep
#     ("MobileSafari") and /usr/bin/nc ("com.apple.WebKit.Networking", loopback client receiving 65536 bytes) exercise
#     role discovery, pcblist_n parsing and system stackshot parsing on a real (newer) XNU. Smoke status is recorded,
#     not used to block the artifact (runner kernel != device kernel).
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?absolute isolated output directory required}
case "$out" in /*) ;; *) exit 2 ;; esac
mkdir -p "$out/work" "$out/delivery" "$out/xnu"; w=$out/work; d=$out/delivery
cd "$here"
tag=xnu-10063.141.1
for f in bsd/sys/socketvar.h bsd/netinet/in_pcb.h bsd/netinet/tcp_var.h bsd/sys/stackshot.h osfmk/kern/kcdata.h; do
  mkdir -p "$out/xnu/$(dirname $f)"
  curl -fsSL --retry 3 "https://raw.githubusercontent.com/apple-oss-distributions/xnu/$tag/$f" -o "$out/xnu/$f"
done
python3 -B tests/gen_real_structs.py "$out/xnu" | tee "$w/gen.txt"
flags=(-std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter)
san=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all)
xcrun --sdk macosx clang "${flags[@]}" -Itests tests/abi_real.c -o "$w/abi_real"
"$w/abi_real" | tee "$w/abi_real.txt"
xcrun --sdk macosx clang "${flags[@]}" "${san[@]}" -Wno-address-of-packed-member -Itests tests/parse_test.c -o "$w/parse_test"
"$w/parse_test" | tee "$w/parse_test.txt"
xcodebuild -version > "$w/xcode.txt"
{ xcrun --sdk iphoneos --show-sdk-path; xcrun --sdk iphoneos --show-sdk-version; } > "$w/sdk.txt"
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 "${flags[@]}" src/unixnu-wk463.c -o "$d/unixnu-wk463"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.wk463 --entitlements launchd/entitlements.plist "$d/unixnu-wk463"
codesign --verify --strict -vv "$d/unixnu-wk463" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-wk463" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-wk463" > "$w/entitlements.xml" 2> "$w/entitlements-stderr.txt"
otool -L "$d/unixnu-wk463" > "$w/dylibs.txt"
otool -l "$d/unixnu-wk463" > "$w/load-commands.txt"
nm -u "$d/unixnu-wk463" > "$w/imports.txt"
cp launchd/org.unixnu.diag.wk463.plist "$d/"; plutil -lint "$d/org.unixnu.diag.wk463.plist"
cp launchd/entitlements.plist "$d/wk463-entitlements.plist"
# 4. real-kernel smoke on the runner (macOS build of the identical source)
xcrun --sdk macosx clang "${flags[@]}" src/unixnu-wk463.c -o "$w/wk463-macos"
s=$out/smoke; mkdir -p "$s"
cp /bin/sleep "$s/MobileSafari"; "$s/MobileSafari" 150 & sleeper=$!
python3 - <<'PY' &
import socket, time
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); srv.bind(('127.0.0.1', 18443)); srv.listen(1)
c, _ = srv.accept(); c.sendall(b'\0' * 65536); time.sleep(140)
PY
server=$!
sleep 1
cp /usr/bin/nc "$s/com.apple.WebKit.Networking"; "$s/com.apple.WebKit.Networking" -d 127.0.0.1 18443 > /dev/null & client=$!
sleep 1
set +e
sudo "$w/wk463-macos" > "$w/runner-wk463.txt" 2> "$w/runner-wk463.err"; echo "smoke_rc=$?" >> "$w/runner-wk463.txt"
python3 -B tests/check_smoke463.py "$w/runner-wk463.txt" "$sleeper" "$client" > "$w/smoke-check.txt" 2>&1; echo "check_rc=$?" >> "$w/smoke-check.txt"
set -e
kill "$sleeper" "$server" "$client" 2>/dev/null || true
cat "$w/smoke-check.txt"
for name in gen.txt abi_real.txt parse_test.txt xcode.txt sdk.txt verify.txt codesign.txt entitlements.xml dylibs.txt load-commands.txt imports.txt runner-wk463.txt smoke-check.txt; do cp "$w/$name" "$d/$name"; done
python3 - "$d" "$here" <<'PY'
import hashlib, json, plistlib, re, sys
from pathlib import Path
d, src = map(Path, sys.argv[1:]); b = d / 'unixnu-wk463'
H = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
ent = {'com.apple.private.security.no-container': True}
assert plistlib.loads((d / 'entitlements.xml').read_bytes()) == ent
libs = [x.split()[0] for x in (d / 'dylibs.txt').read_text().splitlines()[1:] if x.strip()]
assert libs == ['/usr/lib/libSystem.B.dylib'], libs
imp = sorted({x.strip() for x in (d / 'imports.txt').read_text().splitlines() if x.strip()})
need = {'_syscall', '_sysctlbyname', '_proc_listallpids', '_proc_pidpath', '_vm_deallocate', '_mach_timebase_info'}
assert need <= set(imp), need - set(imp)
for bad in ('_task_for_pid', '_kill', '_ptrace', '_socket', '_connect', '_sendto'):
    assert bad not in imp, bad
assert 'PASS fails=0' in (d / 'abi_real.txt').read_text() and 'PASS fails=0' in (d / 'parse_test.txt').read_text()
cd = re.search(r'CDHash=([0-9a-f]{40})', (d / 'codesign.txt').read_text()).group(1)
smoke = (d / 'smoke-check.txt').read_text()
files = {p.name: {'sha256': H(p), 'bytes': p.stat().st_size} for p in sorted(d.iterdir()) if p.is_file() and p.name != 'build463-report.json'}
source = {p.relative_to(src).as_posix(): H(p) for p in sorted(src.rglob('*')) if p.is_file() and p.name != 'real_structs.inc'}
rep = {'binary': 'unixnu-wk463', 'sha256': H(b), 'cdhash': cd, 'dylibs': libs, 'imports': imp, 'entitlements': ent,
       'host_ABI_real_headers': True, 'parser_ASan_UBSan': True, 'runner_smoke': 'PASS' if 'SMOKE PASS' in smoke else 'NOT_PASS',
       'files': files, 'source': source, 'hardware_tested': False}
(d / 'build463-report.json').write_text(json.dumps(rep, indent=2) + '\n')
print(json.dumps({k: rep[k] for k in ('sha256', 'cdhash', 'runner_smoke')}, indent=1))
PY
