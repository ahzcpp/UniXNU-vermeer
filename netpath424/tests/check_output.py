from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();lines=s.splitlines()
assert 1<=len(lines)<=96 and all(x.startswith('NETPATH424 ') for x in lines)
assert 'result=complete tested=1' in s
for bad in (r'https?://',r'(?<![\w.])\d{1,3}(?:\.\d{1,3}){3}(?![\w.])',r'(?i)[0-9a-f]{2}(?::[0-9a-f]{2}){5}',r'/Users/',r'/private/',r'[<>]'):
 assert not re.search(bad,s),bad
print('NETPATH424 runner output: finite tagged metadata, no URLs/addresses/bodies/paths PASS')
