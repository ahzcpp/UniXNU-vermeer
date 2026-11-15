from pathlib import Path
import re,plistlib
P=Path(__file__).resolve().parents[1];s=(P/'src/unixnu-netpath376.m').read_text();code=re.sub(r'/\*.*?\*/','',s,flags=re.S)
for bad in ('didReceiveChallenge','SecTrust','NSURLCredential credential','allHeaderFields','HTTPBody','absoluteString','localizedDescription','MGCopyAnswer','SCDynamicStoreSet','SCPreferencesSet','setuid(','setgid(','system(','popen(','posix_spawn(','fork(','execv','NSURLSessionResponseAllow','dataTaskWithURL:','stringWithData:','initWithData:'):
 assert bad not in code,bad
assert sorted(set(re.findall(r'@"(https?://[^"]+)"',code)))==sorted(['https://captive.apple.com/hotspot-detect.html','https://www.baidu.com/','https://www.google.com/generate_204','https://init.itunes.apple.com/bag.xml'])
for need in ('r.HTTPMethod=@"HEAD"','h(NSURLSessionResponseCancel)','h(nil)','c.HTTPCookieStorage=nil','c.HTTPShouldSetCookies=NO','c.URLCredentialStorage=nil','c.URLCache=nil','c.waitsForConnectivity=waits','nw_path_monitor_cancel(m)','netpath_http_reply(got,status,e!=nil,intended)','alarm(PROCESS_SECONDS)','O_NONBLOCK','MAX_ROWS 96','PROCESS_SECONDS 60'):
 assert need in code,need
assert plistlib.loads((P/'launchd/entitlements.plist').read_bytes())=={'com.apple.private.security.no-container':True}
print('NETPATH376 fixed HEAD targets, no redirects/body/auth overrides/config writes; bounded callbacks/output/process PASS')
