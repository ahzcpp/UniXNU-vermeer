#!/usr/bin/env python3
"""REVIEWED FUTURE CI ONLY: real SDK build/sign of the own supplicant.

No launchd-cache merge, trust-cache edits, firmware staging, installation or device
operations. No signing keys: preserve the existing consumer's ad-hoc signature.
Never run the iOS binary on the macOS runner as a substitute for a device test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

from validate_plan import ROOT, check, validate
from verify_native import verify_binary


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--component', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    check(sys.platform == 'darwin', 'A genuine macOS/iPhoneOS SDK runner is required; no Linux shim')
    check(not os.environ.get('UNIXNU_WIFI_JOIN'), 'Credential/autojoin environment is prohibited')
    component = args.component.resolve(); out = args.out.resolve()
    check(component != out and component not in out.parents, 'output must be outside the source checkout')
    validate(component)
    lock = json.loads((ROOT/'sources.lock.json').read_text())
    # A fresh checkout is required. Never apply to a user's shared working tree.
    check(os.environ.get('GITHUB_ACTIONS') == 'true', 'This reviewed script is scoped to a disposable CI checkout')
    check(not subprocess.check_output(['git','-C',str(component),'status','--porcelain'], text=True), 'dirty component checkout')
    out.mkdir(parents=True, exist_ok=False)
    def run(argv, cwd=None):
        return subprocess.check_output([str(x) for x in argv], cwd=cwd, stderr=subprocess.STDOUT, text=True)
    report = dict(native_sdk_build_executed=False, signed=False, hardware_verified=False,
                  component_commit=lock['component']['commit'], source_only_inputs=True,
                  no_firmware_or_os_payload=True, launchd_cache_merged=False, installed=False)
    try:
        report['xcode'] = run(['xcodebuild','-version']).strip()
        report['sdk_path'] = run(['xcrun','--sdk','iphoneos','--show-sdk-path']).strip()
        report['sdk_version'] = run(['xcrun','--sdk','iphoneos','--show-sdk-version']).strip()
        report['clang'] = run(['xcrun','--sdk','iphoneos','clang','--version']).splitlines()[0]
        real_clang = Path(run(['xcrun','--sdk','iphoneos','--find','clang']).strip())
        build_env = dict(os.environ)
        build_env['PATH'] = str(real_clang.parent)+os.pathsep+os.environ['PATH']
        check(Path(shutil.which('clang', path=build_env['PATH'])).resolve() == real_clang.resolve(),
              'component builder must select the genuine Xcode clang')
        report['actual_compiler'] = str(real_clang)
        patch = ROOT/'patches/0001-ios16.7-target.patch'
        run(['git','apply','--check',patch], component)
        run(['git','apply',patch], component)
        builder = component/'tools/build-supplicant.py'
        check(hashlib.sha256(builder.read_bytes()).hexdigest() == lock['deployment']['patched_builder_sha256'], 'unexpected patched build script')
        fetch_patch = ROOT/'patches/0002-fetch-noncone.patch'
        fetch_meta = json.loads((ROOT/'patches/0002-fetch-noncone.json').read_text())
        fetcher = component/'tools/fetch-sources.py'
        fetch_before = hashlib.sha256(fetcher.read_bytes()).hexdigest()
        check(fetch_before == lock['component']['files']['tools/fetch-sources.py']['sha256'], 'unexpected original fetch script')
        run(['git','apply','--check',fetch_patch], component)
        run(['git','apply',fetch_patch], component)
        fetch_after = hashlib.sha256(fetcher.read_bytes()).hexdigest()
        check(fetch_after == fetch_meta['patched_fetcher_sha256'], 'unexpected patched fetch script')
        report['source_fetch_patch'] = dict(path='patches/0002-fetch-noncone.patch',
                                          sha256=hashlib.sha256(fetch_patch.read_bytes()).hexdigest(),
                                          original_fetcher_sha256=fetch_before,
                                          patched_fetcher_sha256=fetch_after)
        # Preserve upstream's source checks. It currently also resolves locked
        # Linux/FreeBSD inputs; neither is linked into the userspace executable.
        with (out/'source-resolution.log').open('w') as log:
            subprocess.run([sys.executable, str(component/'tools/fetch-sources.py')],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        archive = component/'deps'/lock['supplicant']['archive']
        check(hashlib.sha256(archive.read_bytes()).hexdigest() == lock['supplicant']['sha256'], 'public supplicant tarball digest')
        report['native_sdk_build_executed'] = True
        with (out/'native-build.log').open('w') as log:
            subprocess.run([sys.executable, str(builder),'--target','ios'],
                           stdout=log, stderr=subprocess.STDOUT, env=build_env, check=True)
        source_binary = component/'out/wpa-ios/bin/wpa_supplicant'
        delivery = out/'delivery'; (delivery/'bin').mkdir(parents=True)
        binary = delivery/'bin/unixnu-wpa_supplicant'
        shutil.copy2(source_binary, binary); binary.chmod(0o755)
        ent = ROOT/'policy/supplicant-entitlements.plist'
        run(['codesign','--force','--sign','-','--pagesize','4096','--identifier',
             lock['policy']['identifier'],'--entitlements',ent,binary])
        run(['codesign','--verify','--strict',binary])
        actual = plistlib.loads(subprocess.check_output(['codesign','-d','--entitlements',':-',str(binary)],
                               stderr=subprocess.DEVNULL))
        check(actual == lock['policy']['entitlements'], 'signed permissions differ from baseline')
        audit = verify_binary(binary, actual)  # Every real code-page digest is checked.
        detail = run(['codesign','--display','--verbose=4',binary])
        match = re.search(r'^CDHash=([0-9a-f]{40})$', detail, re.M)
        check(match and match[1] in [x['cdhash'] for x in audit['code_directories']], 'CDHash disagreement')
        (delivery/'daemons').mkdir(); (delivery/'etc/wifi').mkdir(parents=True)
        shutil.copy2(ROOT/'service/org.unixnu.wifi.supplicant.plist', delivery/'daemons')
        shutil.copy2(ROOT/'service/wpa_supplicant.conf', delivery/'etc/wifi')
        report.update(signed=True, audit=audit,
                      build_report=json.loads((component/'out/wpa-ios/report.json').read_text()),
                      permission_source=lock['consumer_reference'],
                      deployment_patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest())
        report['delivery_files_sha256'] = {str(p.relative_to(delivery)): hashlib.sha256(p.read_bytes()).hexdigest()
                                          for p in delivery.rglob('*') if p.is_file()}
        # Only own binary, the unchanged own job, empty profile and this report
        # are uploadable. SDK, upstream examples and source logs stay on runner.
        (delivery/'native-build-report.json').write_text(json.dumps(report, indent=2)+'\n')
    except Exception as e:
        report['error'] = str(e)
        # Surface genuine compiler failures; no CoreServices/fork/sandbox stubs.
        for name in ['source-resolution.log', 'native-build.log']:
            p = out/name
            if p.exists():
                print(p.read_text()[-10000:], file=sys.stderr)
        raise
    finally:
        (out/'status.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
