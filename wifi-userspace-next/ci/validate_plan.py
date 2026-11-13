#!/usr/bin/env python3
"""Source-only checks. Does not build, sign, fetch, open a socket or invoke CI."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(ok, text):
    if not ok:
        raise SystemExit(text)


def validate(component=None, allow_crlf=False):
    lock = json.loads((ROOT/'sources.lock.json').read_text())
    check(lock['policy']['entitlements'] == {'com.apple.private.security.no-container': True}, 'permission expansion')
    ent = plistlib.loads((ROOT/'policy/supplicant-entitlements.plist').read_bytes())
    check(ent == lock['policy']['entitlements'], 'policy mismatch')
    job = plistlib.loads((ROOT/'service/org.unixnu.wifi.supplicant.plist').read_bytes())
    expected = dict(Label='org.unixnu.wifi.supplicant', ProgramArguments=[
        '/usr/libexec/unixnu-wpa_supplicant', '-Dsheng', '-ien0',
        '-c/private/var/ngpu/etc/wifi/wpa_supplicant.conf', '-q', '-s'],
        RunAtLoad=True, KeepAlive=True, ThrottleInterval=3, UserName='root', Umask=0o77, ProcessType='Interactive')
    check(job == expected, 'existing job policy changed')
    check((ROOT/'service/wpa_supplicant.conf').read_text() == 'ap_scan=1\n', 'non-empty network configuration')
    for file in (ROOT/'ci').glob('*.py'):
        ast.parse(file.read_text(), filename=str(file))
    manifest = json.loads((ROOT/'plan-files.json').read_text())
    for path, digest in manifest.items():
        check(hashlib.sha256((ROOT/path).read_bytes()).hexdigest() == digest, 'plan file drift: '+path)
    if component:
        head = subprocess.check_output(['git','-C',str(component),'rev-parse','HEAD'], text=True).strip()
        check(head == lock['component']['commit'], 'component commit mismatch')
        for name, rec in lock['component']['files'].items():
            b = (component/name).read_bytes()
            if allow_crlf:
                b = b.replace(b'\r\n', b'\n')
            check(hashlib.sha256(b).hexdigest() == rec['sha256'], 'component source mismatch: '+name)
    return dict(source_only=True, checked=True, public_source_files=len(lock['component']['files']),
                native_sdk_build_executed=False, signed=False, hardware_tested=False)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--component', type=Path)
    ap.add_argument('--allow-local-crlf', action='store_true')
    args = ap.parse_args()
    print(json.dumps(validate(args.component, args.allow_local_crlf)))
