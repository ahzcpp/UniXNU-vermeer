from pathlib import Path
import re,sys
rows=Path(sys.argv[1]).read_text().splitlines()
assert 54<=len(rows)<=310
assert all(r.startswith('PROC389 ') and len(r.encode())<480 for r in rows)
assert rows[-1]=='PROC389 RESULT result=done mode=wk450 snapshots=16'
for token in ['wk450_sample','fs456']:
 assert [int(re.search(r'\bn=(\d+)\b',r)[1]) for r in rows if ' '+token+' ' in r]==list(range(16))
cats=[r for r in rows if ' fs456_cat ' in r];assert len(cats)==12
for n in (0,15):
 assert [int(re.search(r'\bcat=(\d+)\b',r)[1]) for r in cats if ' n='+str(n)+' ' in r]==list(range(1,7))
for r in cats:
 assert int(re.search(r'\bentries=(\d+)\b',r)[1])<=256 and 'census=0 coherent=0' in r
for r in rows:
 assert not re.search(r'https?://|/Users/|/private/|/System/|token=|password=|ssid=|\d+\.\d+\.\d+\.\d+',r,re.I)
assert len([r for r in rows if ' memory_vm ' in r])==2
print('PASS finite456 smoke: metadata-only fixed numeric rows; no device validation')
