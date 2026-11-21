#!/usr/bin/env python3
# Validate a runner smoke log of unixnu-wk469 (macOS kernel, renamed helper processes). Prints SMOKE PASS / SMOKE FAIL.
# 463 checks kept (finite completion, privacy charset, role discovery, role-1 TCP rx >= 65536, clean stackshot parse,
# role-4 main thread) plus 469: role-5 discovery, worker dump of the role-2 helper with tid/ti, and the REQUIRED
# owner-thread check: the helper's lock waiter must report wait_type=10 owner_kind=tid owner_tid=<holder tid> and
# owner_ti equal to the ti printed for the holder's own row in the same stackshot. The dispatch_sync relation is reported.
import re, sys
log, sleeper, client, gpu, appstore, ids_path = sys.argv[1:7]
rows = [l for l in open(log, encoding='utf-8', errors='replace').read().splitlines() if l.startswith('WK463 ')]
ids = dict(kv.split('=') for kv in open(ids_path).read().split())
fails = []
def need(cond, what):
    if not cond: fails.append(what)
def kv(r): return dict(m.groups() for m in re.finditer(r'(\w+)=(\S+)', r))
need(any(r.startswith('WK463 start version=469.0') for r in rows), 'start row')
res = [r for r in rows if r.startswith('WK463 result=done samples=16')]
need(len(res) == 1, 'finite completion')
if res: need(' rows_dropped=0 ' in res[0] + ' ' and ' out_short=0 ' in res[0] + ' ', 'no dropped/short rows: ' + res[0])
need(all(len(r) < 512 and re.fullmatch(r'[A-Za-z0-9_=.:/,?~\- ]+', r) for r in rows), 'row charset/length')
need(not any(re.search(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b', r) for r in rows), 'no IPv4 text')
def role_seen(pid, role): return any(f' pid={pid} ' in r + ' ' and f' role={role}' in r + ' ' for r in rows if r.startswith('WK463 role '))
need(role_seen(sleeper, 4), 'role4 helper discovered'); need(role_seen(client, 1), 'role1 helper discovered')
need(role_seen(gpu, 2), 'role2 helper discovered'); need(role_seen(appstore, 5), 'role5 helper discovered')
socks = [r for r in rows if r.startswith('WK463 sock ') and f'pid={client} ' in r]
rx = [int(m.group(1)) for r in socks for m in [re.search(r' rx=(\d+)', r)] if m]
need(any(' st=4 ' in r and ' proto=tcp ' in r for r in socks), 'role1 TCP ESTABLISHED row')
need(rx and max(rx) >= 65536, 'role1 rx >= 65536 bytes')
ss = [r for r in rows if r.startswith('WK463 ss n=')]
need(len(ss) == 5, 'five stackshot attempts')
ok = [kv(r)['k'] for r in ss if ' rc=0 ' in r]
for r in ss:
    if ' rc=0 ' in r:
        need(' parse=0 ' in r and ' errors=0 ' in r and int(kv(r)['tasks']) > 10, 'clean stackshot parse: ' + r[:90])
owner_ok = 0; wl = []
for k in ok:
    th = [kv(r) | {'_row': r.split()[1]} for r in rows if (r.startswith('WK463 ssth ') or r.startswith('WK463 ssthw ')) and f' k={k} ' in r]
    need(any(d['pid'] == sleeper and d['main'] == '1' and 'tid' in d and 'ti' in d for d in th), f'k={k} role4 main thread with tid/ti')
    wk = [kv(r) for r in rows if r.startswith('WK463 sswk ') and f' k={k} ' in r and f' pid={gpu} ' in r]
    need(len(wk) == 1 and int(wk[0].get('workers', 0)) >= 3 and wk[0].get('trunc') == '0', f'k={k} role2 worker summary')
    g = {d['tid']: d for d in th if d['pid'] == gpu}
    need(ids['hold'] in g and ids['wait'] in g and ids['main'] in g, f'k={k} helper threads reported by tid')
    frw = [kv(r) for r in rows if r.startswith('WK463 ssfrw ') and f' k={k} ' in r and f' pid={gpu} ' in r]
    need({d['ti'] for d in frw} == {d['ti'] for d in g.values() if d['_row'] == 'ssthw'}, f'k={k} ssfrw rows carry the worker ti')
    if ids['wait'] in g and ids['hold'] in g:
        w, h = g[ids['wait']], g[ids['hold']]
        if w['wait_type'] == '10' and w['owner_kind'] == 'tid' and w['owner_tid'] == ids['hold'] and w['owner_ti'] == h['ti'] \
           and w['owner_tpid'] == gpu:
            owner_ok += 1
        else:
            print(f'k={k} lock waiter row: wait_type={w["wait_type"]} owner_kind={w["owner_kind"]} owner_tid={w["owner_tid"]} '
                  f'owner_ti={w["owner_ti"]} holder_ti={h["ti"]}')
    if ids['main'] in g:
        m = g[ids['main']]
        wl.append(f'k={k} main wait_type={m["wait_type"]} owner_kind={m["owner_kind"]} owner_is_block={m["owner_tid"] == ids["block"]} '
                  f'owner_ti={m["owner_ti"]} block_ti={g[ids["block"]]["ti"] if ids["block"] in g else "-"} ts_kind={m["ts_kind"]} '
                  f'ts_is_block={m["ts_tid"] == ids["block"]}')
if ok: need(owner_ok == len(ok), f'ulock owner tid/ti resolved in {owner_ok}/{len(ok)} stackshots')
print('stackshot_rc0=%d/%d sock_rows=%d max_rx=%s ulock_owner_resolved=%d' % (len(ok), len(ss), len(socks), max(rx) if rx else '-', owner_ok))
for x in wl: print('dispatch_sync (informative):', x)
for f in fails: print('FAIL', f)
print('SMOKE PASS' if not fails else 'SMOKE FAIL')
sys.exit(1 if fails else 0)
