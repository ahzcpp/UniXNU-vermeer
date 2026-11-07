#!/usr/bin/env python3
"""Fetch the SHA-pinned mainline Linux GPU sources and SM8550 DTS include closure (source text only).

Same pins as UniXNU scripts/data/adreno-inputs.json (linux, mesa, board.sha256). Unlike UniXNU's
fetch-adreno-inputs.py this never downloads firmware: the K70 GPU firmware comes from the K70's own
vendor image (see README-gpu.md), and no other board's ZAP is ever fetched.
"""
import base64, concurrent.futures, hashlib, json, os, re, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
pins = json.loads((ROOT / 'scripts/data/adreno-inputs.json').read_text())


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'k70-offline-adreno-inputs'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def job(item):
    dest, url, sha, size, gitiles = item
    if dest.is_symlink():
        data = os.readlink(dest).encode()
    else:
        data = dest.read_bytes() if dest.exists() else get(url)
        if gitiles and not dest.exists():
            data = base64.b64decode(data)
    if hashlib.sha256(data).hexdigest() != sha or (size and len(data) != size):
        raise SystemExit('pinned input differs: ' + str(dest))
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_symlink() or dest.exists():
        return len(data)
    # GitHub serves a source symlink as its target text; recreate the link (the pin is over that text).
    if re.fullmatch(rb'(\.\./)+[\w/.,+-]+\s*', data):
        dest.symlink_to(data.decode().strip())
    else:
        dest.write_bytes(data)
    return len(data)


jobs = []
linux = pins['linux']
raw = linux['repository'].replace('https://github.com/', 'https://raw.githubusercontent.com/') + '/' + linux['commit'] + '/'
for name, e in linux['files'].items():
    jobs.append((ROOT / '.cache/adreno-source' / name, raw + name, e['sha256'], e.get('bytes'), False))
mesa = pins['mesa']
for name, e in mesa['files'].items():
    jobs.append((ROOT / '.cache/adreno-source/mesa' / name, mesa['mirror'] + '/+/' + mesa['commit'] + '/' + name + '?format=TEXT',
                 e['sha256'], e.get('bytes'), True))
board = pins['board']
braw = board['repository'].replace('https://github.com/', 'https://raw.githubusercontent.com/') + '/' + board['commit'] + '/'
for name, sha in board['sha256'].items():
    if name in board.get('local', {}):
        continue
    jobs.append((ROOT / '.cache/linux-dts' / name, braw + name, sha, None, False))
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
    sizes = list(ex.map(job, jobs))
print('PASS: %d pinned source files, %d bytes; no firmware fetched' % (len(sizes), sum(sizes)))
