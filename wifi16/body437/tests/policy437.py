from pathlib import Path
import hashlib,re,json,plistlib
R=Path(__file__).resolve().parents[1];B=R/'reference424'
s=(R/'src/unixnu-netbody437.m').read_text();old=(B/'src/unixnu-netpath424.m').read_text();code=re.sub(r'/\*.*?\*/','',s,flags=re.S);H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for n in ('Identity424.h','FdGate424.h'):assert (R/'src'/n).read_bytes()==(B/'src'/n).read_bytes()
assert s[s.index('/* Real libSystem call'):s.index('int main(')]==old[old.index('/* Real libSystem call'):old.index('int main(')]
a=s[s.index('static void inspect_path'):s.index('/* Each delegate')];b=old[old.index('static void inspect_path'):old.index('/* Each delegate')];assert a.replace('org.unixnu.diag.netbody437.path','org.unixnu.diag.netpath424.path')==b
assert (R/'src/Watchdog437.h').read_text()==(B/'src/Watchdog424.h').read_text().replace('WATCHDOG424_H','WATCHDOG437_H').replace('NETPATH424','NETBODY437')
for bad in ('didReceiveChallenge','SecTrust','NSURLCredential credential','allHeaderFields','HTTPBody','absoluteString','localizedDescription','MGCopyAnswer','SCDynamicStoreSet','SCPreferencesSet','getpwnam','getpwuid','initgroups(','setenv(','setpersona','posix_spawn(','fork(','execv','dlsym(','weak_import','stringWithData:','initWithData:','NSMutableData','appendData:','[data bytes]','data.bytes','CC_SHA','SHA256','connectionProxyDictionary'):
 assert bad not in code,bad
assert sorted(set(re.findall(r'@"(https?://[^"]+)"',code)))==sorted(['https://captive.apple.com/hotspot-detect.html','https://www.baidu.com/','https://init.itunes.apple.com/bag.xml'])
for need in ('r.HTTPMethod=@"GET"','h(NSURLSessionResponseAllow)','h(nil)','body437_receive(&v,(uint64_t)data.length)','if(cancel)[t cancel]','body437_finish(&v,kind)','body437_classify(&v,w!=0)','c.HTTPCookieStorage=nil','c.HTTPShouldSetCookies=NO','c.URLCredentialStorage=nil','c.URLCache=nil','c.waitsForConnectivity=waits','MAX_ROWS 96','REQUEST_SECONDS 8','PROCESS_SECONDS 60'):
 assert need in code,need
assert 'NSURLSessionResponseCancel' not in code and 'cancelledAfterHeaders' not in code and 'expected_cancel' not in code
main=code[code.index('int main('):];assert main.index('alarm(PROCESS_SECONDS)')<main.index('if(drop)drop_mobile424();')<main.index('@autoreleasepool')<main.index('inspect_path()');assert main[:main.index('@autoreleasepool')].count('@')==0
assert main.index('@autoreleasepool')<main.index('alarm(0)')
assert plistlib.loads((R/'launchd/entitlements.plist').read_bytes())==plistlib.loads((B/'launchd/entitlements.plist').read_bytes())=={'com.apple.private.security.no-container':True}
j=plistlib.loads((R/'launchd/example-root-drop501.plist').read_bytes());assert j['ProgramArguments']==['/usr/libexec/unixnu-netbody437','--drop-to-mobile'] and j['StartInterval']==180 and j['RunAtLoad'] is False and j['LaunchOnlyOnce'] is True and j['KeepAlive'] is False and j['UserName']=='root'
assert not any('delay' in k.lower() for k in j) and '-d' not in j['ProgramArguments']
# Generate formatting fixtures directly from actual source format strings.
formats=re.findall(r'emit\("((?:[^"\\]|\\.)*)"',s);lines=['#include <stdio.h>','#include <assert.h>','#include <limits.h>','int main(void){char b[4096];int n;unsigned maximum=0;']
for f in formats:
 vals=[]
 for spec in re.findall(r'%(?:ll|l)?[sdu]',f):
  vals.append({'%s':'"'+'a'*70+'"','%d':'INT_MIN','%u':'UINT_MAX','%ld':'LONG_MIN','%lld':'LLONG_MIN','%llu':'ULLONG_MAX'}[spec])
 fmt=json.dumps('NETBODY437 '+f+'\n');lines.append('n=snprintf(b,sizeof b,'+fmt+(','+','.join(vals) if vals else '')+');assert(n>0&&n<640);if((unsigned)n>maximum)maximum=(unsigned)n;')
lines.append('printf("NETBODY437 format rows=%u max=%u pass\\n",'+str(len(formats))+',maximum);return 0;}');(R/'tests/test_formats437.c').write_text('\n'.join(lines)+'\n')
report={'status':'PASS437_SOURCE_POLICY_ONLY','baseline_source_sha256':H(B/'src/unixnu-netpath424.m'),'candidate_source_sha256':H(R/'src/unixnu-netbody437.m'),'four_ID_rawgroups_and_FD_drop_code_byte_exact424':True,'Identity424_and_FdGate424_headers_byte_exact':True,'Network_framework_path_code_namespace_only':True,'watchdog_label_only_change':True,'three_fixed_HTTPS_GETs':True,'no_response_body_read_store_or_hash':True,'no_certificate_auth_redirect_cookie_proxy_policy_relaxation':True,'cap_bytes':16384,'request_seconds':8,'process_watchdog_seconds':60,'output_normal_rows_cap':96,'normal_row_storage_bytes':640,'watchdog_reserved_row':1,'job_StartInterval':180,'job_RunAtLoad':False,'job_process_delay_seconds':0,'native_compiled_or_identity_tested':False,'source_format_fixture_rows':len(formats)}
(R/'SOURCE-POLICY-PROOF.json').write_text(json.dumps(report,indent=2)+'\n');print('437 source policy PASS; original424 guards exact; formats='+str(len(formats)))
