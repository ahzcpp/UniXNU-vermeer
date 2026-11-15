#!/usr/bin/env python3
"""Static read-only policy checks for iokitbusy366."""
import plistlib, re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
code = re.sub(r'/\*.*?\*/', '', (root / 'src/unixnu-iokitbusy366.c').read_text(), flags=re.S)
fail = []
def need(c, m):
    if not c: fail.append(m)
for f in ('IOServiceOpen', 'IOConnect', 'IORegistryEntrySetCFPropert', 'IORegistryEntryCreateCFPropert', 'IORegistryEntryGetName',
          'IORegistryEntryGetRegistryEntryID', 'IORegistryEntryGetPath', 'IOServiceRequestProbe', 'IOServiceWaitQuiet', 'IOKitWaitQuiet',
          'socket(', 'sysctl', 'fopen(', 'unlink', 'exec', 'fork(', 'kill('):
    need(f not in code, 'forbidden: ' + f)
arr = re.search(r'k_classes\[\] = \{([^}]*)\}', code)
classes = re.findall(r'"([^"]+)"', arr.group(1)) if arr else []
need(len(re.findall(r'IOServiceMatching\(', code)) == 2 and 'IOServiceMatching(k_classes[c])' in code
     and 'IOServiceMatching("IOService")' in code, 'matching only over k_classes and the -w walk over IOService')
# -w walk bounds (Main review addendum): only on root busy, >= 60 s apart, <= 6 runs, <= 512 entries + look-ahead, <= 16 classes,
# class name only for busy entries, validated; no other per-entry reads
need('while (seen < WALK_MAX_ENTRIES && (s = IOIteratorNext(it)) != 0)' in code, 'walk must stop at WALK_MAX_ENTRIES')
need(re.search(r'#define WALK_MAX_ENTRIES 512\b', code) and re.search(r'#define WALK_MAX_CLASSES 16\b', code)
     and re.search(r'#define WALK_MAX_RUNS 6\b', code) and re.search(r'#define WALK_MIN_GAP_MS 60000LL\b', code), 'walk constants')
need("g_walk_enabled && root_quiet[0] == 'n' && g_walk_runs < WALK_MAX_RUNS" in code, 'walk only on actual root busy, bounded runs')
need(code.count('IOObjectGetClass(') == 1 and 'valid_class(cls, sizeof cls)' in code, 'class name only via validated IOObjectGetClass')
need('if (b == 0) { IOObjectRelease(s); continue; }' in code, 'class read only for busy entries')
need(sorted(set(classes)) == sorted({'IONetworkStack', 'IO80211Controller', 'IONetworkInterface', 'IO80211Interface'}), 'unexpected classes %r' % classes)
need(re.findall(r'IORegistryEntryFromPath\(MACH_PORT_NULL, "([^"]+)"\)', code) == ['IOService:/'], 'only the IOService root path')
need(re.findall(r'\bopen\("([^"]+)"', code) == ['/dev/console'], 'only /dev/console may be opened')
need('alarm(limit)' in code and 'MAX_PER_CLASS' in code and 'O_NONBLOCK' in code, 'bounds missing')
# review 17:38: iteration itself bounded, console budget holds every row of the largest run, clock failure handled
need('while (seen < MAX_PER_CLASS && (s = IOIteratorNext(it)) != 0)' in code, 'iterator walk must stop at MAX_PER_CLASS')
need(len(re.findall(r'IOIteratorNext\(', code)) == 4, 'only the two bounded loops and their look-aheads may call IOIteratorNext')
need(re.search(r'#define CONSOLE_MAX_ROWS 360\b', code)
     and 'MAX_SAMPLES * ROWS_PER_SAMPLE + 2 + WALK_MAX_RUNS * (1 + WALK_MAX_CLASSES) <= CONSOLE_MAX_ROWS' in code, 'console budget')
need('if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;' in code and code.count('clock_gettime(') == 1, 'clock failure must give -1')
need('complete ? "yes" : "no"' in code, 'per-sample completeness flag')
need(code.count('IOIteratorIsValid(') == 2 and 'int complete = iterator_valid &&' in code and 'if (!iterator_valid) unknown++;' in code, 'invalidated iterator must remain unknown/partial')
need(code.count('IOServiceGetBusyState(') == 3 and 'IOServiceGetBusyStateAndTime(' not in code, 'only genuine public busy-state API; unavailable private metrics remain unknown')
p = plistlib.loads((root / 'launchd/org.unixnu.diag.iokitbusy366.plist').read_bytes())
need(p.get('KeepAlive') is False and p.get('LaunchOnlyOnce') is True, 'launchd once, no KeepAlive')
e = plistlib.loads((root / 'launchd/entitlements.plist').read_bytes())
need(e == {'com.apple.private.security.no-container': True}, 'entitlements exactly no-container')
if fail:
    print('POLICY FAIL'); [print(' -', f) for f in fail]; sys.exit(1)
print('POLICY PASS classes=%s' % sorted(set(classes)))
