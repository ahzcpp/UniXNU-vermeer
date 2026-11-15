#!/bin/bash
# SUGGESTED macOS-runner build for Main's CI (NOT run by Claude; no SDK here). Genuine iPhoneOS SDK + macOS SDK.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?output}; mkdir -p "$out/work" "$out/delivery"; out=$(cd "$out" && pwd); w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version > "$w/xcode.txt"; xcrun --sdk iphoneos --show-sdk-version > "$w/sdk.txt"
# 1. text policy + portable format test (host)
python3 tests/policy_check425.py | tee "$d/policy.txt"
xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  tests/test_format425.c -o "$w/test_format425"
"$w/test_format425" | tee "$d/format-test.txt"
# 2. own SC prototypes must match Apple's real header
xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror -fsyntax-only -DNS425_HAVE_SC_CONTEXT \
  -include SystemConfiguration/SystemConfiguration.h src/netstate425_sc.h
# 3. same source on the runner (read-only SC snapshot of the RUNNER's network; not K70 evidence)
xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror -O2 src/unixnu-netstate425.c \
  -framework CoreFoundation -framework SystemConfiguration -o "$w/netstate-macos"
"$w/netstate-macos" -n 2 > "$d/runner.txt"; cat "$d/runner.txt"
grep -q '^NETSTATE425 result=complete frames=2 ' "$d/runner.txt"
if grep -vqE '^NETSTATE425 [A-Za-z0-9_=. -]+$' "$d/runner.txt"; then echo 'non-schema row'; exit 1; fi
# 4. iOS 16 arm64 build, ad-hoc sign with the single no-container entitlement (no SCDynamicStore write entitlement)
xcrun --sdk iphoneos clang -std=c11 -arch arm64 -mios-version-min=16.0 -O2 -Wall -Wextra -Werror src/unixnu-netstate425.c \
  -framework CoreFoundation -framework SystemConfiguration -o "$d/unixnu-netstate425"
strip -x "$d/unixnu-netstate425"
codesign --force --sign - --pagesize 4096 --identifier org.unixnu.diag.netstate425 --entitlements launchd/entitlements.plist "$d/unixnu-netstate425"
codesign --verify --strict -vv "$d/unixnu-netstate425" > "$w/verify.txt" 2>&1
codesign -d -vvv "$d/unixnu-netstate425" > "$w/codesign.txt" 2>&1
codesign -d --entitlements - --xml "$d/unixnu-netstate425" > "$w/entitlements.xml" 2>/dev/null
otool -L "$d/unixnu-netstate425" > "$w/dylibs.txt"
nm -u "$d/unixnu-netstate425" > "$d/imports.txt"
python3 - "$d/imports.txt" "$w/dylibs.txt" <<'EOF'
import re, sys
imp = {l.strip() for l in open(sys.argv[1]) if l.strip()}
sc = {i for i in imp if i.startswith('_SC')}
assert sc == {'_SCDynamicStoreCreate', '_SCDynamicStoreCopyValue', '_SCError'}, sc
for bad in ('_socket', '_connect', '_getaddrinfo', '_inet_ntop', '_SCPreferences', '_SCNetworkInterfaceCopyAll', '_nwi_',
            '_fork', '_execve', '_setuid', '_ioctl', '_CFCopyDescription', '_CFStringGetCString', '_printf', '_puts'):
    assert not any(i.startswith(bad) for i in imp), bad
libs = [l.split()[0] for l in open(sys.argv[2]).read().splitlines()[1:] if l.strip()]
assert all(re.search(r'(libSystem\.B\.dylib|CoreFoundation\.framework|SystemConfiguration\.framework)', x) for x in libs), libs
print('imports/dylibs PASS', sorted(sc), len(imp))
EOF
shasum -a 256 "$d/unixnu-netstate425" | tee "$d/binary.sha256"
