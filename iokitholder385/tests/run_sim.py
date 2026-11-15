#!/usr/bin/env python3
"""Host simulation test for unixnu-iokitholder385 (synthetic registries only; NOT K70 evidence).
Builds the unmodified probe source against tests/mock_iokit.c and checks the rows for each scenario."""
import os, re, subprocess, sys, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[1]
cc = os.environ.get('CC', 'cc')
tmp = Path(tempfile.mkdtemp(prefix='ih385-'))
exe = tmp / 'probe-sim'
# An explicit Xcode clang path does not inherit xcrun's SDK environment.
# Darwin tests still use real SDK headers; the Linux-only Mach fixture stays out.
host = ['-isysroot', subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()] if sys.platform == 'darwin' else ['-I', str(root / 'tests/host')]
subprocess.run([cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                *host, '-o', str(exe), str(root / 'src/unixnu-iokitholder385.c'), str(root / 'tests/mock_iokit.c')],
               check=True)
fail = []
def run(scn, *args):
    env = dict(os.environ, MOCK_SCENARIO=scn)
    p = subprocess.run([str(exe), *args], capture_output=True, text=True, env=env, timeout=60)
    out = p.stdout
    if p.returncode != 0: fail.append('%s: rc=%d %s' % (scn, p.returncode, p.stderr[-400:]))
    if 'MOCK live_refs=0' not in p.stderr: fail.append('%s: leaked references: %s' % (scn, p.stderr.strip()[-200:]))
    if any(not l.startswith('IOKITHOLDER385 ') for l in out.splitlines()): fail.append('%s: untagged output' % scn)
    return out
def need(c, m):
    if not c: fail.append(m)

o = run('k372')
h = [l for l in o.splitlines() if ' candidate ' in l]
need(len(h) == 1, 'k372: exactly one holder expected, got %r' % h)
need(h and 'class=IOPlatformDevice parent_class=ArmDTPlatform busy=1 busy_after=1 busy_children=0 foreign_busy_children=0 self_busy=1' in h[0] and 'child_classes=AppleImage4*1' in h[0]
     and 'reads_ok=yes stable=yes' in h[0], 'k372: holder row %r' % h)
need(re.search(r'walk n=0 .*busy_entries=2 .*candidates=1 candidates_unprinted=0 unknown_self_entries=0 complete=yes', o), 'k372: walk summary\n' + o)
need('root n=0 busy=1 quiet=no' in o, 'k372: root row')

o = run('quiet')
need('root n=0 busy=0 quiet=yes' in o and ' walk ' not in o and ' candidate ' not in o, 'quiet: no walk when root quiet\n' + o)
o = run('quiet', '-f')
need(re.search(r'walk n=0 .*busy_entries=0 .*candidates=0 .*complete=yes', o) and ' candidate ' not in o, 'quiet -f: forced empty walk\n' + o)

o = run('nested')
h = [l for l in o.splitlines() if ' candidate ' in l]
need(len(h) == 1 and 'class=NativeHidInput parent_class=IOPlatformDevice' in h[0] and 'self_busy=1' in h[0] and
     'child_classes=IOHIDEventDriver*1' in h[0], 'nested: %r' % h)

o = run('multiparent')
h = [l for l in o.splitlines() if ' candidate ' in l]
need(len(h) == 1 and 'class=IOService parent_class=IOPlatformDevice' in h[0], 'multiparent: only the real candidate, got %r' % h)
need('complete=yes' in o, 'multiparent: complete')

o = run('race')
h = [l for l in o.splitlines() if ' candidate ' in l]
need(h and all('self_busy=unknown' in l for l in h) and any('stable=no' in l for l in h) and re.search(r'walk n=0 snapshot=non_atomic .*complete=no', o),
     'race: a changing busy count must give self_busy=unknown and complete=no\n' + o)

o = run('readfail')
need(re.search(r'walk n=0 .*unknown_reads=1 .*complete=no', o), 'readfail: unknown read must make complete=no\n' + o)

o = run('many')
need(len([l for l in o.splitlines() if ' candidate ' in l]) == 12 and re.search(r'candidates=14 candidates_unprinted=2 .*complete=no', o),
     'many: holder rows capped at 12, complete=no\n' + o)

o = run('badname')
need('class=class_unknown' in o and 'Bad Name' not in o, 'badname: invalid class names must not be printed\n' + o)
need('class_reads_ok=no' in o and 'complete=no' in o, 'badname: invalid metadata cannot be complete')

for scenario in ('classfail', 'parentclassfail', 'childclassfail', 'secondscanfail', 'secondscanexpand', 'classoverflow'):
    o = run(scenario)
    h = [l for l in o.splitlines() if ' candidate ' in l]
    need(len(h) == 1 and 'class_reads_ok=no' in h[0] and 'complete=no' in o,
         scenario + ': failed/truncated metadata must mark incomplete\n' + o)
    if scenario == 'secondscanfail':
        need('child_classes=unknown' in o and 'class_unknown_reads=1' in o and 'class_iterator_valid=0' in o,
             'secondscanfail: unknown classes cannot be reported as none\n' + o)
    if scenario == 'secondscanexpand':
        need('class_children_seen=64 class_children_truncated=yes' in o, 'secondscanexpand must preserve the second-pass cap')
    if scenario == 'classoverflow': need('child_class_overflow=1' in o, 'classoverflow: retained overflow evidence')

o = run('longclasses')
h = [l for l in o.splitlines() if ' candidate ' in l]
need(len(h) == 1 and len(h[0]) > 1000 and len(h[0]) < 1536 and 'class_reads_ok=yes' in h[0] and
     h[0].endswith('class_iterator_valid=1') and 'complete=yes' in o, 'max-length valid class row must be complete\n' + o)
for char in 'ABCDEF': need(char * 127 in o, 'longclasses: every full fixed class label retained')

o = run('huge')
need(re.search(r'walk n=0 snapshot=non_atomic seen=1024 cap=1024 truncated=yes .*complete=no', o), 'huge: walk cap + look-ahead\n' + o)

o = run('k372', '-n', '2', '-i', '10')
need(o.count(' walk n=') == 2 and 'RESULT result=done samples=2' in o, 'loop mode two samples')
o = run('k372', '-u', '0', '1')
need(o == '', 'window: outside the uptime window the run must be silent, got %r' % o)

if fail:
    print('SIM FAIL'); [print(' -', f) for f in fail]; sys.exit(1)
fmt = tmp / 'format-limit'
subprocess.run([cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                *host, '-o', str(fmt), str(root / 'tests/format_limit.c'), str(root / 'tests/mock_iokit.c')],check=True)
r=subprocess.run([str(fmt)],capture_output=True,text=True,check=True,timeout=60)
assert r.stdout=='IOKITHOLDER385 output result=unknown reason=format_limit\n',r.stdout
print('SIM PASS scenarios=20 original+classfail+parentclassfail+childclassfail+secondscanfail+secondscanexpand+classoverflow+longclasses+formatfallback; refs and ASan/UBSan PASS')
