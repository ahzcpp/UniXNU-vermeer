import subprocess,sys
for mode in ('no-sink','uid-callback-hang'):
 p=subprocess.run([sys.argv[1],mode],capture_output=True,text=True,timeout=5)
 assert p.returncode==124,(mode,p.returncode,p.stderr)
 assert not p.stderr,(mode,p.stderr)
 if mode=='no-sink':assert not p.stdout
 else:assert p.stdout=='NETBODY437 deadline=1 exit_code=124\n'
print('437 watchdog no-sink and injected UID callback hang: exit124 PASS; no real IDs changed')
