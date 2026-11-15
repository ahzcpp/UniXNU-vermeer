#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Static source policy for netstate425 (text checks only; not a build, not native proof).
import plistlib, re, sys
from pathlib import Path

P = Path(__file__).resolve().parents[1]
src = (P / 'src/unixnu-netstate425.c').read_text()
out = (P / 'src/netstate425_out.h').read_text()
sch = (P / 'src/netstate425_sc.h').read_text()
strip = lambda s: re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))
code, outc, scc = strip(src), strip(out), strip(sch)

# 1. SystemConfiguration surface: exactly three functions, read-only
sc_calls = set(re.findall(r'\b(SC[A-Za-z]+)\s*\(', code))
assert sc_calls == {'SCDynamicStoreCreate', 'SCDynamicStoreCopyValue', 'SCError'}, sc_calls
assert set(re.findall(r'\b(SC[A-Za-z]+)\s*\(', scc)) == {'SCDynamicStoreCreate', 'SCDynamicStoreCopyValue', 'SCError'}
for bad in ('SCDynamicStoreSet', 'SCDynamicStoreRemove', 'SCDynamicStoreNotify', 'SetNotificationKeys', 'CopyKeyList',
            'CopyMultiple', 'SCPreferences', 'SCNetworkInterface', 'SCNetworkSet', 'SCNetworkService', 'IOService',
            'nwi_', 'network_information', 'socket(', 'connect(', 'getaddrinfo', 'gethostbyname', 'inet_ntop', 'inet_ntoa',
            'getnameinfo', 'ether_ntoa', 'sockaddr_dl', 'CFCopyDescription', 'CFStringGetCString', 'CFShow', 'printf(',
            'puts(', 'fopen', 'setuid', 'setgid', 'seteuid', 'fork(', 'exec', 'system(', 'posix_spawn', 'O_CREAT', 'unlink',
            'ifa_addr->sa_data', 'sin_addr', 'ioctl('):
    assert bad not in code, bad

# 2. fixed keys: exactly this set, all string literals (no runtime key construction)
keys = re.findall(r'CFSTR\("((?:State|Setup|Plugin):[^"]*)"', code)
svc = re.search(r'#define OWN_SERVICE "([0-9A-F-]{36})"', code).group(1)
assert svc == 'A7C30323-4E45-5444-4941-470000000323'
keys = [k for k in keys if not k.endswith('/')]          # service keys are checked below (literal + OWN_SERVICE)
svc_keys = ['State:/Network/Service/' + svc + s for s in re.findall(r'CFSTR\("State:/Network/Service/" OWN_SERVICE "(/[A-Za-z0-9]+)"\)', code)]
want = {'State:/Network/Global/IPv4', 'State:/Network/Global/DNS', 'State:/Network/Global/Proxies',
        'State:/Network/Interface/en0/Link', 'State:/Network/Interface/en0/IPv4', 'Plugin:InterfaceNamer'}
assert set(keys) == want, set(keys) ^ want
assert sorted(svc_keys) == sorted(['State:/Network/Service/%s/IPv4' % svc, 'State:/Network/Service/%s/DNS' % svc]), svc_keys
assert len(re.findall(r'SCDynamicStoreCopyValue\(', code)) == 1          # only inside copy_key()
assert len(re.findall(r'copy_key\(st, K_[A-Z0-9_]+, ', code)) == 8

# 3. output: integer-only formats, no %s / %p / %c anywhere in printf-style formats
fmts = re.findall(r'ns425_emit\(\s*("(?:[^"\\]|\\.)*"(?:\s*(?:NS425_KEYFMT|"(?:[^"\\]|\\.)*"))*)', outc + code)
assert fmts, 'no formats found'
for f in fmts:
    conv = re.findall(r'%[-+ #0]*\d*(?:\.\d+)?(hh|h|ll|l|z)?([a-zA-Z%])', f)
    for _, c in conv:
        assert c in 'dui', (f, c)
assert '%s' not in outc and '%p' not in outc and '%s' not in code and '%p' not in code
assert 'nwi=unknown' in outc and 'nwi_probe=0' in outc

# 4. bounds
for need in ('NS425_MAX_FRAMES 12u', 'NS425_PERIOD_S 5u', 'NS425_WATCHDOG_S 90u', 'NS425_MAX_DELAY_S 600u', 'NS425_MAX_ROWS 160u',
             'O_NOFOLLOW', 'O_NONBLOCK', 'O_CLOEXEC', 'S_ISCHR'):
    assert need in outc, need
for need in ('alarm(o.delay_s + NS425_WATCHDOG_S)', 'ns425_console_enable("/dev/console")', 'F_SETFL, fl | O_NONBLOCK',
             'strcmp(a->ifa_name, "en0")', 'IFF_UP', 'IFF_RUNNING', 'AF_INET'):
    assert need in code, need

# 5. suggested signing entitlements: only no-container (read-only SC needs no write entitlement)
ent = plistlib.loads((P / 'launchd/entitlements.plist').read_bytes())
assert ent == {'com.apple.private.security.no-container': True}, ent
print('NETSTATE425 policy: SC read-only (Create/CopyValue/SCError), 8 fixed keys, integer-only rows, bounded frames/watchdog/rows/sinks PASS')
