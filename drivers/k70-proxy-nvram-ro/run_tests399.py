#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess,os,re
r=Path(__file__).resolve().parent;o=r/'host-out';o.mkdir(exist_ok=True)
src=(r/'src/NativeProxyNVRAM399.cpp').read_text()
# No substitutions of implementation statements: only unavailable kernel includes.
lines=src.splitlines(keepends=True)
removed=[l for l in lines if l.startswith('#include <IOKit/') or l.startswith('#include <libkern/')]
body=''.join(l for l in lines if l not in removed)
(o/'actual-native-glue.inc').write_text(body)
flags=['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(r/'src'),'-I'+str(r/'tests'),'-I'+str(o)]
logs=[]
for source,binary in [('test_core399.cpp','core'),('test_native_glue399.cpp','glue')]:
 cmd=flags+[str(r/'tests'/source),'-o',str(o/binary)]
 p=subprocess.run(cmd,capture_output=True,text=True);logs.append({'cmd':cmd,'rc':p.returncode,'stderr':p.stderr})
 if p.returncode:print(p.stderr);raise SystemExit(p.returncode)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
cases=['new-fail','iteration-cap','activation-denied','prebind-no-read','hidden-controller','null-provider','platform-parent','other-root-parent','wrong-class','off-plane','optin-off','optin-short','optin-type','geometry-short','multibank','bad-checksum','proxy-type','already-resource','already-controller','controller-at-recheck','iterator-fail','iterator-invalid','topology-change','copy-fail','metadata-fail','init-fail','attach-fail','start-fail','success']
for cmd in [[str(o/'core')]]+[[str(o/'glue'),case] for case in cases]:
 p=subprocess.run(cmd,capture_output=True,text=True,env=env);logs.append({'cmd':cmd,'rc':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
 if p.returncode:print(p.stdout,p.stderr);(o/'test-results.json').write_text(json.dumps(logs,indent=2));raise SystemExit(p.returncode)
report={'status':'PASS_HOST_ASAN_UBSAN','native_sdk_compiled':False,'device_tested':False,'native_implementation_extraction':'only 8 IOKit/libkern include lines removed; body otherwise exact','removed_includes':removed,'source_sha256':hashlib.sha256((r/'src/NativeProxyNVRAM399.cpp').read_bytes()).hexdigest(),'core_sha256':hashlib.sha256((r/'src/ProxyBank399.h').read_bytes()).hexdigest(),'logs':logs}
(o/'test-results.json').write_text(json.dumps(report,indent=2));print(json.dumps({'status':report['status'],'runs':1+len(cases),'core':logs[2]['stdout'],'success':logs[-1]['stdout']}))
