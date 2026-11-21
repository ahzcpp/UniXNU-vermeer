#!/usr/bin/env python3
"""Static read-only policy checks on the own snapshot probe."""
import plistlib, re, sys, json, hashlib
from pathlib import Path
root = Path(__file__).resolve().parents[1]
src = (root / 'src/unixnu-proc389.c').read_text() + '\n' + (root / 'src/netsnap_memory351.h').read_text() + '\n' + (root / 'src/proc389_watch.h').read_text() + '\n' + (root / 'src/proc450_webkit.h').read_text()
code = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
fail = []
def need(c, m):
    if not c: fail.append(m)
for f in ('socket(', 'connect(', 'sendto(', 'kill(', 'ptrace', 'task_for_pid', 'PROC_PIDPATHINFO', 'KERN_PROCARGS',
          'insi_faddr', 'insi_laddr', 'ina_46', 'ina_6', 'getifaddrs', 'fopen(', 'unlink', 'rename(', 'ioctl(', 'setsockopt', 'system(',
          'exec', 'popen', 'fork('):
    need(f not in code, 'forbidden: ' + f)
calls = list(re.finditer(r'\bsysctlbyname\s*\(([^;]*)\)\s*;', code))
need(len(calls) == len(re.findall(r'\bsysctlbyname\s*\(', code)), 'every sysctl call must be checked')
def readonly_arguments(args):
    return re.sub(r'\s+', '', args).endswith(',NULL,0')
assert readonly_arguments('name, &v, &len, NULL, 0')
assert not readonly_arguments('name, &v, &len, &new_value, sizeof(new_value)')
for m in calls:
    need(readonly_arguments(m.group(1)), 'sysctl must be read-only: ' + m.group(1))
opens = re.findall(r'\bopen\("([^"]+)"', code)
need(opens == ['/dev/console'], 'only /dev/console may be opened: %r' % opens)
need('O_NONBLOCK' in code and 'CONSOLE_MAX_ROWS' in code and 'alarm(limit)' in code, 'console/alarm bounds missing')
p = plistlib.loads((root / 'launchd/org.unixnu.diag.proc389.plist').read_bytes())
need(p.get('KeepAlive') is False and p.get('LaunchOnlyOnce') is True, 'launchd must run once, no KeepAlive')
e = plistlib.loads((root / 'launchd/entitlements.plist').read_bytes())
need(e == {'com.apple.private.security.no-container': True}, 'entitlements must be exactly no-container')

base=json.loads((root / 'BASE-INPUTS.json').read_text())['files']
for header in ('netsnap_procinfo.h','netsnap_memory351.h'):
    need(hashlib.sha256((root / 'src' / header).read_bytes()).hexdigest()==base['src/'+header], 'ABI/error observer header changed: '+header)
need('int count = 4, interval = 30, delay = 0;' in code, 'default finite4/30 config missing')
need('parse_int(argv[++i], 1, 4, &count)' in code, 'maximum4 samples not enforced')
need('#define NS389_DATA_ROWS 319u' in code and '#define NS389_CONSOLE_ROWS 320u' in code, 'output budget changed')
need('ns389_row_admit(&g_output_rows, NS389_DATA_ROWS)' in code, 'stdout bound missing')
need(p['ProgramArguments']==['/usr/libexec/unixnu-proc389','-p','-d','0','-n','4','-i','30'], 'expected finite diagnostic job arguments differ')

need(len(re.findall(r'\bproc_pidpath\s*\(', code)) == 2, 'exactly one path call plus declaration required')
need('p.ret=proc_pidpath(pid,path,(uint32_t)sizeof path);p.error=errno;' in code, 'path read must remain bounded and local')
need('#define NS450_SAMPLES 16' in code and '#define NS450_PIDS 8' in code and 'alarm(65)' in code, '450 bounds changed')
need('if (g_webkit450 && (g_memory_only || g_custom_schedule)) return 2;' in code, '450 options must fail closed')
p450=plistlib.loads((root/'launchd/org.unixnu.diag.proc450.plist').read_bytes())
need(p450.get('ProgramArguments')==['/usr/libexec/unixnu-proc450','-w','-p'], '450 must explicitly opt in to finite role mode')
need(p450.get('RunAtLoad') is False and p450.get('LaunchOnlyOnce') is True and p450.get('KeepAlive') is False, '450 job must be manual once-only')
need('StartInterval' not in p450 and 'StartCalendarInterval' not in p450, '450 recurring schedule forbidden')
if fail:
    print('POLICY FAIL'); [print(' -', f) for f in fail]; sys.exit(1)
print('POLICY PASS read-only sysctls=%d' % len(re.findall(r'sysctlbyname\(', code)))
