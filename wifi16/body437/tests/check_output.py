from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text();lines=s.splitlines();assert len(lines)<=97 and all(len(l.encode())<=639 for l in lines)
assert any(l.startswith('NETBODY437 start ') for l in lines)
assert any(l.startswith('NETBODY437 result=complete ') for l in lines)
assert not any(x in s for x in ('https://','<html','<!DOCTYPE','HTTP/1.','Set-Cookie:'))
print('437 bounded runner output only; no handset or stock-app claim')
