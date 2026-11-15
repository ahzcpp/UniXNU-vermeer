from pathlib import Path
import re,hashlib,json,plistlib
R=Path(__file__).resolve().parents[1];B=R/'reference';s=(R/'src/unixnu-netpath424.m').read_text();code=re.sub(r'/\*.*?\*/','',s,flags=re.S)
for bad in ('didReceiveChallenge','SecTrust','NSURLCredential credential','allHeaderFields','HTTPBody','absoluteString','localizedDescription','MGCopyAnswer','SCDynamicStoreSet','SCPreferencesSet','getpwnam','getpwuid','initgroups(','setenv(','setpersona','posix_spawn(','fork(','execv','dlsym(','weak_import','NSURLSessionResponseAllow','dataTaskWithURL:','stringWithData:','initWithData:'):
 assert bad not in code,bad
dropbody=code[code.index('static void drop_mobile424'):code.index('int main(')];assert dropbody.index('launch.ruid!=0||launch.euid!=0')<dropbody.index('open_console424')
assert code.count('setgroups(0,NULL)')==1 and code.count('setuid((uid_t)NW424_MOBILE_ID)')==1 and code.count('setgid((gid_t)NW424_MOBILE_ID)')==1
assert sorted(set(re.findall(r'@"(https?://[^"]+)"',code)))==sorted(['https://captive.apple.com/hotspot-detect.html','https://www.baidu.com/','https://www.google.com/generate_204','https://init.itunes.apple.com/bag.xml'])
for need in ('r.HTTPMethod=@"HEAD"','h(NSURLSessionResponseCancel)','h(nil)','c.HTTPCookieStorage=nil','c.HTTPShouldSetCookies=NO','c.URLCredentialStorage=nil','c.URLCache=nil','c.waitsForConnectivity=waits','nw_path_monitor_cancel(m)','netpath_http_reply(got,status,e!=nil,intended)','alarm(PROCESS_SECONDS)','O_NONBLOCK','MAX_ROWS 96','PROCESS_SECONDS 60'):
 assert need in code,need
assert plistlib.loads((R/'launchd/entitlements.plist').read_bytes())=={'com.apple.private.security.no-container':True}
main=code[code.index('int main('):];assert main.index('alarm(PROCESS_SECONDS)')<main.index('if(drop)drop_mobile424();')<main.index('@autoreleasepool')<main.index('inspect_path()')
assert main[:main.index('@autoreleasepool')].count('@')==0
assert 'if(stdout_enabled)' in code[code.index('static void rawline'):code.index('static void emit')]
assert 'if(rc)_exit(40+(int)id.stage)' in code and 'if(fd<0)_exit(20+step)' in code
assert code.index('nw424_drop_identity(&ops,&id)')<code.index('null_stdio424(&step,&saved)')<code.index('nw424_verify_final_fds(&fops,fd,&fr)')
assert (R/'src/netpath_classify.h').read_bytes()==(B/'src/netpath_classify.h').read_bytes()
# Normal path/delegate/TLS/metrics code exact after namespace-only376->424 replacement.
old=(B/'src/unixnu-netpath376.m').read_text();expected=old[old.index('static void inspect_path'):old.index('int main(')].strip();actual=s[s.index('static void inspect_path'):s.index('/* Real libSystem call')].strip().replace('424','376');assert actual==expected
assert s.count('--drop-to-mobile')==1 and 'atoi(' not in code and 'strtol(' not in code
watch=(R/'src/Watchdog424.h').read_text();assert 'if(stdout_enabled)' in watch and 'if(fd>=0)' in watch and '_exit(124)' in watch and 'snprintf' not in watch
report={'status':'PASS424_SOURCE_POLICY_ONLY','original_Network_URLSession_delegate_TLS_metrics_code_exact_namespace_change':True,'original_classifier_bytes_exact':True,'fixed_four_HEADs_unchanged':True,'credential_cookie_body_redirect_policy_unchanged':True,'watchdog_armed_before_drop_or_pool':True,'UID_guard_before_pool_and_network':True,'only_fixed501_no_account_lookup_or_arbitrary_UID_URL':True,'no_root_network_fallback':True,'logging_normal_rows_cap':96,'watchdog_reserved_rows':1,'max_line_bytes':639,'dropped_console_upper_bound_bytes':96*639+64,'native_compiled':False,'actual_UID_drop_tested':False}
(R/'SOURCE-POLICY-PROOF.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS424 source policy: original network path retained; fixed ID/drop before pool, no fallback, bounded console')
