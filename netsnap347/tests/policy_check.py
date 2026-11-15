#!/usr/bin/env python3
"""Static read-only policy checks on the own snapshot probe."""
import plistlib, re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
src = (root / 'src/unixnu-netsnap347.c').read_text()
code = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
fail = []
def need(c, m):
    if not c: fail.append(m)
for f in ('socket(', 'connect(', 'sendto(', 'kill(', 'ptrace', 'task_for_pid', 'PROC_PIDPATHINFO', 'proc_pidpath', 'KERN_PROCARGS',
          'insi_faddr', 'insi_laddr', 'ina_46', 'ina_6', 'getifaddrs', 'fopen(', 'unlink', 'rename(', 'ioctl(', 'setsockopt', 'system(',
          'exec', 'popen', 'fork('):
    need(f not in code, 'forbidden: ' + f)
for m in re.finditer(r'sysctlbyname\(([^;]*)\);', code):
    need(m.group(1).replace(' ', '').endswith(',NULL,0)'), 'sysctl must be read-only: ' + m.group(1))
opens = re.findall(r'\bopen\("([^"]+)"', code)
need(opens == ['/dev/console'], 'only /dev/console may be opened: %r' % opens)
need('O_NONBLOCK' in code and 'CONSOLE_MAX_ROWS' in code and 'alarm(limit)' in code, 'console/alarm bounds missing')
p = plistlib.loads((root / 'launchd/org.unixnu.diag.netsnap347.plist').read_bytes())
need(p.get('KeepAlive') is False and p.get('LaunchOnlyOnce') is True, 'launchd must run once, no KeepAlive')
e = plistlib.loads((root / 'launchd/entitlements.plist').read_bytes())
need(e == {'com.apple.private.security.no-container': True}, 'entitlements must be exactly no-container')
if fail:
    print('POLICY FAIL'); [print(' -', f) for f in fail]; sys.exit(1)
print('POLICY PASS read-only sysctls=%d' % len(re.findall(r'sysctlbyname\(', code)))
