"""Validate a finite macOS runner smoke log; never infer device success."""
from pathlib import Path
import re,sys
rows=Path(sys.argv[1]).read_text().splitlines()
assert 20<=len(rows)<=276
assert all(r.startswith('PROC389 ') and len(r.encode())<480 for r in rows)
assert 'wk450_start version=450.1' in rows[0]
assert rows[-1]=='PROC389 RESULT result=done mode=wk450 snapshots=16'
samples=[int(re.search(r'\bn=(\d+)\b',r)[1]) for r in rows if ' wk450_sample ' in r]
assert samples==list(range(16))
assert all('absence_proven=0' in r for r in rows if ' wk450_sample ' in r)
for r in rows:
    assert not re.search(r'https?://|/Users/|/private/|/System/|token=|password=|ssid=|\d+\.\d+\.\d+\.\d+',r,re.I)
print('PASS finite wk450 runner smoke: %d bounded numeric/fixed-token rows; not device validation'%len(rows))
