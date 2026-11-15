#!/bin/bash
# CI (macOS runner, genuine iPhoneOS SDK): host tests, a real-network run of the same tool built for macOS (observe mode), the arm64
# iOS 16 build, ad-hoc signature with the one entitlement the existing own supplicant uses, and an audit report. Source-only inputs.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?out}; mkdir -p "$out/delivery" "$out/work"
w=$out/work; d=$out/delivery
cd "$here"
xcodebuild -version | tee "$w/xcode.txt"
SDK=$(xcrun --sdk iphoneos --show-sdk-path); echo "sdk $SDK"
# 1. host tests (macOS clang, ASan+UBSan)
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -o "$w/test_core" tests/test_core.c src/netdiag_core.c
"$w/test_core" | tee "$d/host-tests.txt"
# 2. the whole tool on a real network (the runner's): observe mode exercises getifaddrs, the routing-table dump, DNS over UDP,
#    the system resolver, TCP and HTTP. Not DHCP and no address change (the runner's interface is already configured).
clang -std=c11 -Wall -Wextra -Werror -O2 -o "$w/netdiag-macos" src/unixnu-netdiag.c src/netdiag_core.c
IF=$(route -n get default 2>/dev/null | awk '/interface:/{print $2}'); IF=${IF:-en0}
sudo "$w/netdiag-macos" observe -i "$IF" -D 8.8.8.8 -l "$w/observe.log" > "$d/macos-observe.txt" 2>&1 || true
cat "$d/macos-observe.txt"
# 3. iOS arm64 build
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 -std=c11 -O2 -Wall -Wextra -Werror \
  -o "$d/unixnu-netdiag" src/unixnu-netdiag.c src/netdiag_core.c
strip -x "$d/unixnu-netdiag" 2>/dev/null || true
codesign -s - -f -i org.unixnu.wifi.netdiag --entitlements launchd/entitlements.plist "$d/unixnu-netdiag"
codesign -d -vvv "$d/unixnu-netdiag" > "$w/codesign.txt" 2>&1; cat "$w/codesign.txt"
codesign -d --entitlements - "$d/unixnu-netdiag" > "$w/entitlements.txt" 2>&1 || true
otool -L "$d/unixnu-netdiag" | tee "$w/otool-L.txt"
otool -l "$d/unixnu-netdiag" | grep -A4 LC_BUILD_VERSION | tee "$w/build-version.txt"
# 4. netpublish: lease-parser tests, a dry run and a read-only SystemConfiguration listing on the runner, then the iOS build
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o "$w/test_lease" tests/test_lease.c
"$w/test_lease" | tee -a "$d/host-tests.txt"
clang -std=c11 -Wall -Wextra -Werror -O2 -o "$w/netpublish-macos" src/unixnu-netpublish.c -framework CoreFoundation -framework SystemConfiguration
# the own declarations must agree with Apple's real header: compile once more against it (macOS) as a prototype check
clang -std=c11 -Wall -Wextra -Werror -O0 -fsyntax-only -include SystemConfiguration/SystemConfiguration.h src/netpublish_sc.h
printf 'if=en0\nip=192.0.2.77\nmask=255.255.255.0\nrouter=192.0.2.1\ndns=198.51.100.53,198.51.100.54\nlease=3600\n' > "$w/synthetic.lease"
"$w/netpublish-macos" dry-run "$w/synthetic.lease" > "$d/macos-netpublish.txt" 2>&1 || true
sudo "$w/netpublish-macos" list >> "$d/macos-netpublish.txt" 2>&1 || true
cat "$d/macos-netpublish.txt"
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 -std=c11 -O2 -Wall -Wextra -Werror \
  -o "$d/unixnu-netpublish" src/unixnu-netpublish.c -framework CoreFoundation -framework SystemConfiguration
strip -x "$d/unixnu-netpublish" 2>/dev/null || true
codesign -s - -f -i org.unixnu.wifi.netpublish --entitlements launchd/netpublish-entitlements.plist "$d/unixnu-netpublish"
codesign -d -vvv "$d/unixnu-netpublish" > "$w/codesign-netpublish.txt" 2>&1; cat "$w/codesign-netpublish.txt"
codesign -d --entitlements - "$d/unixnu-netpublish" > "$w/entitlements-netpublish.txt" 2>&1 || true
otool -L "$d/unixnu-netpublish" | tee "$w/otool-L-netpublish.txt"
cp launchd/org.unixnu.wifi.netpublish.plist "$d/"; plutil -lint "$d/org.unixnu.wifi.netpublish.plist"
cp launchd/org.unixnu.wifi.netdiag.plist "$d/"
plutil -lint "$d/org.unixnu.wifi.netdiag.plist"
python3 - "$d" "$w" <<'EOF'
import hashlib, json, re, sys, pathlib
d, w = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
cs = (w/'codesign.txt').read_text(); obs = (d/'macos-observe.txt').read_text()
cdhash = re.search(r'CDHash=([0-9a-f]{40})', cs).group(1)
libs = [l.split()[0] for l in (w/'otool-L.txt').read_text().splitlines()[1:] if l.strip()]
rep = dict(binary_sha256=hashlib.sha256((d/'unixnu-netdiag').read_bytes()).hexdigest(), bytes=(d/'unixnu-netdiag').stat().st_size,
           cdhash=cdhash, codesign=cs, dylibs=libs, entitlements=(w/'entitlements.txt').read_text(),
           build_version=(w/'build-version.txt').read_text(), xcode=(w/'xcode.txt').read_text(),
           plist_sha256=hashlib.sha256((d/'org.unixnu.wifi.netdiag.plist').read_bytes()).hexdigest(),
           host_tests=(d/'host-tests.txt').read_text().strip(),
           macos_observe_result=[l for l in obs.splitlines() if ' RESULT ' in l],
           hardware_tested=False, installed=False, source_only_inputs=True)
assert libs == ['/usr/lib/libSystem.B.dylib'], libs
cs2 = (w/'codesign-netpublish.txt').read_text()
libs2 = [l.split()[0] for l in (w/'otool-L-netpublish.txt').read_text().splitlines()[1:] if l.strip()]
rep['netpublish'] = dict(binary_sha256=hashlib.sha256((d/'unixnu-netpublish').read_bytes()).hexdigest(), cdhash=re.search(r'CDHash=([0-9a-f]{40})', cs2).group(1),
    dylibs=libs2, entitlements=(w/'entitlements-netpublish.txt').read_text(), codesign=cs2,
    plist_sha256=hashlib.sha256((d/'org.unixnu.wifi.netpublish.plist').read_bytes()).hexdigest(), macos=(d/'macos-netpublish.txt').read_text())
(d/'build-report.json').write_text(json.dumps(rep, indent=2) + '\n'); print(json.dumps({k: rep[k] for k in ('binary_sha256','cdhash','dylibs','macos_observe_result')}, indent=1))
EOF
