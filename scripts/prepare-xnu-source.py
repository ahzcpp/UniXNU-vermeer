#!/usr/bin/env python3
"""Restore omitted DriverKit headers from Apple's earlier Darwin 23 source.

vermeer override of scripts/prepare-xnu-source.py (a device repository replaces a
script of the same name, scripts/unixnu/tree.py). The upstream version lists the
files through the unauthenticated GitHub contents API, which on a shared CI
runner is rate limited to 60 requests per hour per IP; the first vermeer CI run
stopped there with HTTP 403. This version reads the same files out of a shallow
git checkout of the pinned Apple tag, so it needs no API at all, and keeps the
API as a fallback for hosts where git cannot reach GitHub.

Both paths verify the git blob hash of every header they write, and the record
in .cache/sources/xnu-restored-headers.json keeps the same shape.
"""
import concurrent.futures
import hashlib
import json
import os
import subprocess
import threading
import urllib.error
import urllib.request
from pathlib import Path

# Tags of the Apple sources that still carry the headers newer xnu omits; feeds.toml sets them for the unixnu driver.
TAG = os.environ.get('UNIXNU_DRIVERKIT_TAG', 'xnu-10063.121.3')
TRUST_TAG = os.environ.get('UNIXNU_TRUSTCACHE_TAG', 'xnu-8796.101.5')
DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')
TRUST_DESTINATION = Path('.cache/xnu-work/EXTERNAL_HEADERS/TrustCache')
REPOSITORY = 'https://github.com/apple-oss-distributions/xnu.git'
SOURCES = Path('.cache/sources')
# The three restores run concurrently; only one of them may set up the reference checkout.
_CHECKOUT_LOCK = threading.Lock()


def blob_hash(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def reference_checkout(tag):
    """A shallow clone that carries `tag`; the same checkout is reused for every tag.

    The three restores run in a thread pool, so two of them can enter here at the
    same time. `git init` refuses a directory that already exists and then fails
    with "could not lock config file ... File exists", which pushed the whole run
    onto the rate-limited API. Serialise the setup with a lock and make every step
    idempotent.
    """
    directory = SOURCES / 'xnu-reference'
    with _CHECKOUT_LOCK:
        directory.parent.mkdir(parents=True, exist_ok=True)
        if not (directory / '.git').is_dir():
            subprocess.run(['git', 'init', '-q', str(directory)], check=True)
        remotes = subprocess.run(['git', '-C', str(directory), 'remote'], capture_output=True, text=True, check=True).stdout
        if 'origin' not in remotes.split():
            subprocess.run(['git', '-C', str(directory), 'remote', 'add', 'origin', REPOSITORY], check=True)
        have = subprocess.run(['git', '-C', str(directory), 'rev-parse', '--verify', '--quiet', f'refs/tags/{tag}^{{commit}}'],
                              capture_output=True, text=True)
        if have.returncode:
            # A shallow tag fetch of apple-oss-distributions/xnu is a few hundred KiB of trees and blobs.
            result = subprocess.run(['git', '-C', str(directory), 'fetch', '-q', '--depth', '1', 'origin', f'tag', tag],
                                    capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr.strip() or f'git fetch {tag} failed')
    return directory


def git_entries(directory, tag, path):
    """(name, git blob id) of the .h files under `path` at `tag`."""
    listing = subprocess.run(['git', '-C', str(directory), 'ls-tree', '-r', tag, '--', path],
                             capture_output=True, text=True, check=True).stdout
    entries = []
    for line in listing.splitlines():
        meta, _, name = line.partition('\t')
        mode, kind, sha = meta.split()
        if kind == 'blob' and name.endswith('.h'):
            entries.append((name, sha))
    return entries


def git_restore(directory, tag, path, destination, relative_to=None):
    """Copy the headers of `path` at `tag` into `destination`; returns the record of what was written."""
    restored = []
    for name, sha in git_entries(directory, tag, path):
        relative = name[len(relative_to) + 1:] if relative_to and name.startswith(relative_to + '/') else Path(name).name
        target = destination / relative
        if target.exists():
            continue
        data = subprocess.run(['git', '-C', str(directory), 'cat-file', 'blob', sha],
                              capture_output=True, check=True).stdout
        if blob_hash(data) != sha:
            raise ValueError(f'git blob hash mismatch: {name}')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        restored.append({'name': relative, 'source_tag': tag, 'git_blob': sha,
                         'sha256': hashlib.sha256(data).hexdigest()})
    return restored


def api_request(url):
    """The contents API, authenticated when a token is present (CI runners share a rate-limit bucket)."""
    request = urllib.request.Request(url, headers={'User-Agent': 'unixnu-prepare-xnu-source'})
    token = os.environ.get('GITHUB_TOKEN') or os.environ.get('GH_TOKEN')
    if token:
        request.add_header('Authorization', 'Bearer ' + token)
    request.add_header('Accept', 'application/vnd.github+json')
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def api_restore(tag, path, destination, relative_to=None):
    entries = json.loads(api_request(
        f'https://api.github.com/repos/apple-oss-distributions/xnu/contents/{path}?ref={tag}'))
    restored = []
    for entry in entries:
        if entry['type'] != 'file' or not entry['name'].endswith('.h'):
            continue
        # The API lists a subdirectory by file name only; keep the crypto/ prefix that the
        # DriverKit headers include as <DriverKit/crypto/sha2.h>.
        subdirectory = Path(path).name if relative_to else ''
        relative = f'{subdirectory}/{entry["name"]}' if subdirectory else entry['name']
        target = destination / relative
        if target.exists():
            continue
        data = api_request(entry['download_url'])
        if blob_hash(data) != entry['sha']:
            raise ValueError(f'Git blob hash mismatch: {entry["name"]}')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        restored.append({'name': relative, 'source_tag': tag, 'git_blob': entry['sha'],
                         'sha256': hashlib.sha256(data).hexdigest()})
    return restored


def restore(path, tag, destination, relative_to=None):
    """Git first; the API only when git cannot reach GitHub."""
    try:
        directory = reference_checkout(tag)
        return git_restore(directory, tag, path, destination, relative_to)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f'prepare-xnu-source: git reference unavailable ({error}); falling back to the contents API')
        return api_restore(tag, path, destination, relative_to)


if __name__ == '__main__':
    if not DESTINATION.parent.is_dir():
        raise SystemExit('Create the local XNU work source before preparing headers')
    DESTINATION.mkdir(parents=True, exist_ok=True)
    TRUST_DESTINATION.mkdir(parents=True, exist_ok=True)
    SOURCES.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        jobs = [executor.submit(restore, 'iokit/DriverKit', TAG, DESTINATION),
                executor.submit(restore, 'iokit/DriverKit/crypto', TAG, DESTINATION, 'iokit/DriverKit'),
                executor.submit(restore, 'EXTERNAL_HEADERS/TrustCache', TRUST_TAG, TRUST_DESTINATION)]
        restored = [entry for job in jobs for entry in job.result()]
    previous = SOURCES / 'xnu-restored-headers.json'
    existing = json.loads(previous.read_text()) if previous.exists() else []
    by_name = {entry['name']: entry for entry in existing + restored}
    previous.write_text(json.dumps(list(by_name.values()), indent=2) + '\n')
    print(f'Restored {len(restored)} omitted public headers from Apple; blob hashes verified')
