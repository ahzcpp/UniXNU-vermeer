#!/usr/bin/env python3
"""Static read-only policy checks for iokitholder385."""
import plistlib, re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
raw = (root / 'src/unixnu-iokitholder385.c').read_text()
code = re.sub(r'/\*.*?\*/', '', raw, flags=re.S)
fail = []
def need(c, m):
    if not c: fail.append(m)
# forbidden: identities, properties, user clients, writes, private SPI, process control
for f in ('IOServiceOpen', 'IOConnect', 'IORegistryEntrySetCFPropert', 'IORegistryEntryCreateCFPropert', 'IORegistryEntryGetName',
          'IORegistryEntryGetNameInPlane', 'IORegistryEntryGetLocationInPlane', 'IORegistryEntryGetRegistryEntryID', 'IORegistryEntryGetPath',
          'IORegistryEntryCopyPath', 'IORegistryEntrySearchCFProperty', 'IOServiceGetBusyStateAndTime', 'IOServiceRequestProbe',
          'IOServiceWaitQuiet', 'IOKitWaitQuiet', 'IOServiceMatching', 'IOServiceGetMatchingServices', 'socket(', 'sysctl', 'fopen(',
          'unlink', 'exec', 'fork(', 'kill(', 'malloc(', 'IOObjectCopyClass'):
    need(f not in code, 'forbidden: ' + f)
# only the documented calls
calls = set(re.findall(r'\b(IO[A-Za-z]+)\s*\(', code))
allowed = {'IORegistryEntryFromPath', 'IORegistryEntryCreateIterator', 'IORegistryEntryGetChildIterator', 'IORegistryEntryGetParentEntry',
           'IOIteratorNext', 'IOIteratorIsValid', 'IOObjectIsEqualTo', 'IOObjectRelease', 'IOServiceGetBusyState', 'IOObjectGetClass'}
need(calls <= allowed, 'unexpected IOKit calls %r' % sorted(calls - allowed))
need(re.findall(r'k_root_path = "([^"]+)"', code) == ['IOService:/'] and code.count('IORegistryEntryFromPath(') == 1, 'only the IOService root path')
need('k_plane[128] = "IOService"' in code and code.count('IORegistryEntryCreateIterator(') == 1, 'only the IOService plane')
# bounds
for name, val in (('WALK_CAP', 1024), ('CHILD_CAP', 64), ('HOLDER_ROWS', 12), ('CHILD_CLASSES', 4), ('MAX_SAMPLES', 12), ('CONSOLE_MAX_ROWS', 200)):
    need(re.search(r'#define %s %d\b' % (name, val), code), 'constant %s' % name)
need('while (seen < WALK_CAP && (e = IOIteratorNext(it)) != 0)' in code, 'walk must stop at WALK_CAP')
need('while (cs->seen < CHILD_CAP && (c = IOIteratorNext(it)) != 0)' in code, 'child scan must stop at CHILD_CAP')
need(len(re.findall(r'IOIteratorNext\(', code)) == 4, 'only the two bounded loops and their look-aheads may call IOIteratorNext')
need('if (t->printed >= HOLDER_ROWS) return;' in code, 'holder rows capped')
need('else if (cs->nclasses < CHILD_CLASSES)' in code, 'child class names capped')
need('MAX_SAMPLES * ROWS_PER_SAMPLE + 2 <= CONSOLE_MAX_ROWS' in code, 'console budget assert')
need(code.count('IOObjectGetClass(') == 1 and 'valid_class(cls, sizeof cls)' in code, 'class names only via validated IOObjectGetClass')
need('if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;' in code and code.count('clock_gettime(') == 1, 'clock failure gives -1')
need('if (now < 0 || now < (long long)win_lo * 1000 || now > (long long)win_hi * 1000) return 0;' in code, 'window gate silent outside window')
need(re.findall(r'\bopen\("([^"]+)"', code) == ['/dev/console'] and 'O_NONBLOCK' in code, 'only /dev/console, non-blocking')
need('alarm(limit)' in code, 'alarm limit')
need('complete ? "yes" : "no"' in code and 'snapshot=non_atomic' in code and 'int known = reads_ok && stable && self >= 0;' in code
     and 'strcpy(selfs, "unknown")' in code, 'non-atomic reads: self_busy unknown on failure/truncation/instability/negative')
for pl in ('org.unixnu.diag.iokitholder385.plist', 'org.unixnu.diag.iokitholder385.interval.plist'):
    p = plistlib.loads((root / 'launchd' / pl).read_bytes())
    need(p.get('KeepAlive') is False and p.get('UserName') == 'root', pl + ': no KeepAlive, root')
    args = p.get('ProgramArguments', [])
    need(args[:1] == ['/usr/libexec/unixnu-iokitholder385'] and '-f' not in args, pl + ': program / no -f')
    if 'interval' in pl:
        need(p.get('StartInterval') == 30 and '-u' in args and p.get('LaunchOnlyOnce') is not True, pl + ': periodic short runs inside a window')
        lo, hi = int(args[args.index('-u') + 1]), int(args[args.index('-u') + 2])
        runs = (hi - lo) // 30 + 2
        need(runs * (1 + 1 + 1 + 12 + 1) <= 400, pl + ': bounded total rows %d' % (runs * 16))
    else:
        need(p.get('LaunchOnlyOnce') is True, pl + ': once')
e = plistlib.loads((root / 'launchd/entitlements.plist').read_bytes())
need(e == {'com.apple.private.security.no-container': True}, 'entitlements exactly no-container')
if fail:
    print('POLICY FAIL'); [print(' -', f) for f in fail]; sys.exit(1)
print('POLICY PASS calls=%s' % sorted(calls))
