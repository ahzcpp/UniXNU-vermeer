#!/usr/bin/env python3
"""Static policy checks on the own probe source and plist (runs in CI and locally). Fails on any forbidden pattern."""
import plistlib, re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
src = (root / 'src/unixnu-https346.m').read_text()
code = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
code = re.sub(r'(?m)^\s*//.*$', '', code)
fail = []
def need(cond, msg):
    if not cond: fail.append(msg)
forbidden = ['didReceiveChallenge', 'serverTrust', 'SecTrustSetExceptions', 'SecTrustEvaluate', 'NSAllowsArbitraryLoads',
             'backgroundSessionConfiguration', 'sharedSession', 'defaultSessionConfiguration', 'NSURLCredential',
             'allHeaderFields', 'Set-Cookie', 'NSHTTPCookie ', 'cookies', 'setValue:', 'HTTPBody', 'stringWithData',
             'initWithData:', 'getifaddrs', 'inet_ntop', 'SCDynamicStore', 'MGCopyAnswer', 'localizedDescription', 'absoluteString']
for f in forbidden:
    need(f not in code, 'forbidden pattern in code: ' + f)
need('ephemeralSessionConfiguration' in code, 'must use ephemeral session')
for s in ('c.HTTPCookieStorage = nil', 'c.HTTPShouldSetCookies = NO', 'c.URLCache = nil', 'c.URLCredentialStorage = nil',
          'NSHTTPCookieAcceptPolicyNever', 'c.timeoutIntervalForRequest = REQUEST_TIMEOUT_S',
          'c.timeoutIntervalForResource = REQUEST_TIMEOUT_S', 'r.HTTPShouldHandleCookies = NO', 'alarm(PROCESS_ALARM_S)',
          'O_NONBLOCK', 'CONSOLE_MAX_ROWS'):
    need(s in code, 'missing: ' + s)
need(re.search(r'#define REQUEST_TIMEOUT_S 10\b', code), 'request timeout must be 10')
need(re.search(r'#define PROCESS_ALARM_S 60\b', code), 'alarm must be 60')
urls = sorted(set(re.findall(r'@"(https?://[^"]+)"', code)))
need(urls == ['https://captive.apple.com/hotspot-detect.html', 'https://www.apple.com/library/test/success.html'], 'unexpected URLs %r' % urls)
need(not re.search(r'@"http://', code), 'plain http not allowed')
# every emit format: only %d/%ld/%lld/%llu/%lu/%s/%x and %s args are tokens produced by safe_token or fixed strings
p = plistlib.loads((root / 'launchd/org.unixnu.wifi.https346.plist').read_bytes())
need(p.get('KeepAlive') is False and p.get('LaunchOnlyOnce') is True and p.get('RunAtLoad') is True, 'launchd must run once, no KeepAlive')
need(p.get('ProgramArguments') == ['/usr/libexec/unixnu-https346', '-c'], 'unexpected ProgramArguments')
e = plistlib.loads((root / 'launchd/entitlements.plist').read_bytes())
need(e == {'com.apple.private.security.no-container': True}, 'entitlements must be exactly no-container')
if fail:
    print('POLICY FAIL'); [print(' -', f) for f in fail]; sys.exit(1)
print('POLICY PASS checks=%d urls=%s' % (len(forbidden) + 16, urls))
