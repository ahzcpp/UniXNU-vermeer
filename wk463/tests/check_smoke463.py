#!/usr/bin/env python3
# Validate a runner smoke log of unixnu-wk463 (macOS kernel, renamed helper processes). Prints SMOKE PASS / SMOKE FAIL.
# Checks: finite completion, privacy charset, role discovery for the helper pids, a role-1 TCP row with >= 65536 rx bytes,
# and, when the stackshot syscall succeeded, a clean parse with a main-thread report for the role-4 helper.
import re, sys
log, sleeper, client = sys.argv[1], sys.argv[2], sys.argv[3]
rows = [l for l in open(log, encoding='utf-8', errors='replace').read().splitlines() if l.startswith('WK463 ')]
fails = []
def need(cond, what):
    if not cond: fails.append(what)
need(any(r.startswith('WK463 start version=463.1') for r in rows), 'start row')
need(any(r.startswith('WK463 result=done samples=16') for r in rows), 'finite completion')
need(all(len(r) < 512 and re.fullmatch(r'[A-Za-z0-9_=.:/,?\- ]+', r) for r in rows), 'row charset/length')
need(not any(re.search(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b', r) for r in rows), 'no IPv4 text')
need(any(f'role=4' in r and f'pid={sleeper} ' in r + ' ' for r in rows if r.startswith('WK463 role ')), 'role4 helper discovered')
need(any(f'pid={client} ' in r + ' ' and 'role=1' in r for r in rows if r.startswith('WK463 role ')), 'role1 helper discovered')
socks = [r for r in rows if r.startswith('WK463 sock ') and f'pid={client} ' in r]
rx = [int(m.group(1)) for r in socks for m in [re.search(r' rx=(\d+)', r)] if m]
need(any(' st=4 ' in r and ' proto=tcp ' in r for r in socks), 'role1 TCP ESTABLISHED row')
need(rx and max(rx) >= 65536, 'role1 rx >= 65536 bytes')
ss = [r for r in rows if r.startswith('WK463 ss n=')]
need(len(ss) == 5, 'five stackshot attempts')
ok = [r for r in ss if ' rc=0 ' in r]
for r in ok:
    need(' parse=0 ' in r and ' errors=0 ' in r and int(re.search(r'tasks=(\d+)', r).group(1)) > 10, 'clean stackshot parse: ' + r[:80])
if ok:
    need(any(f'pid={sleeper} ' in r and ' main=1 ' in r for r in rows if r.startswith('WK463 ssth ')), 'role4 main thread reported')
    need(any(f'pid={sleeper} ' in r for r in rows if r.startswith('WK463 ssfr ')), 'role4 frames reported')
print('stackshot_rc0=%d/%d sock_rows=%d max_rx=%s' % (len(ok), len(ss), len(socks), max(rx) if rx else '-'))
for f in fails: print('FAIL', f)
print('SMOKE PASS' if not fails else 'SMOKE FAIL')
sys.exit(1 if fails else 0)
