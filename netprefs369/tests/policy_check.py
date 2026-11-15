from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/unixnu-netprefs369.c').read_text();h=(root/'src/netprefs369_cf.h').read_text()
for token in ('SCPreferencesSet','SCPreferencesCommit','SCPreferencesApply','SCDynamicStoreSet','SCNetworkInterfaceCopyAll','CFShow','IOService','proc_info'):
 assert token not in s,token
assert set(re.findall(r'SCPreferences\w+(?=\()',s))=={'SCPreferencesCreate','SCPreferencesGetValue'}
assert s.count('SCDynamicStoreCopyValue(')==1
assert 'O_NONBLOCK' in s and 'rows>=255' in s and 'alarm((unsigned)((n-1)*interval+25))' in s
f=h[h.index('static int np_model_bytes_ok'):h.index('\n#endif')];a=s[s.index('static int argnum'):s.index('\nstatic void sample')]
program='#include <stddef.h>\n#include <string.h>\n#include <stdlib.h>\n#include <errno.h>\n#include <assert.h>\n'+f+'\n'+a+'\n'+(root/'tests/helpers_body.c').read_text()
with tempfile.TemporaryDirectory(prefix='np369-') as td:
 p=Path(td);(p/'test.c').write_text(program)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('NETPREFS369 POLICY PASS actual helpers 16 assertions; readonly API allowlist; finite budget; CF fixtures require genuine SDK')
