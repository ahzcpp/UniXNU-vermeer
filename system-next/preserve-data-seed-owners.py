#!/usr/bin/env python3
"""Repair ownership on a copy of one exact new seed before its first device use.

macOS/root only. No new APFS volume is created. The source seed and source OS
are never mounted writable or modified. Only a fresh reconstructed sparse copy
is attached writable, and its actual UUID must match the original report before
and after. Source inode metadata is pinned separately from owners-ignored stat.
The copy also receives a per-volume no_log marker and narrowly scoped cleanup
of host-generated hex-named event logs, with an exact declared content delta.
"""
import argparse
import collections
import contextlib
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True
INPUT_SEED_SHA = '455bd0547664b86c41a884b17aaa5e57a00571cde49f31dd7b5308260712ebad'
OWNER_PLAN_SHA = '8954e2656be7d83df53d539a55eade691e8b9d16d871d446039a399b893a943c'
PACKER_SHA = 'efb5fac21ae94dcb1aecd47da2b73a7112b487e43692ac6396858552d27a0e48'
OS_SHA = '3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d'
LOGICAL_BYTES = 3 << 30


def require(value, message):
    if not value:
        raise RuntimeError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(8 << 20), b''):
            result.update(data)
    return result.hexdigest()


def canonical_uuid(value):
    require(isinstance(value, str), 'missing real volume UUID')
    result = str(uuid.UUID(value)).upper()
    require(result != '00000000-0000-0000-0000-000000000000', 'zero volume UUID')
    return result


def components(relative):
    require(isinstance(relative, str) and '\0' not in relative and '\\' not in relative,
            'invalid ownership path')
    if relative == '':
        return ()
    path = PurePosixPath(relative)
    require(not path.is_absolute() and all(part not in ('', '.', '..') for part in relative.split('/')),
            'ownership path is not canonical relative')
    return path.parts


def source_plan(path):
    require(digest(path) == OWNER_PLAN_SHA, 'owner-plan SHA-256 mismatch')
    raw = json.loads(path.read_text())
    require(raw['source_os_sha256_pin'] == OS_SHA and raw['entry_count'] == 206,
            'owner plan is for a different source')
    result = {}
    for row in raw['entries']:
        components(row['path'])
        require(row['kind'] in ('directory', 'file', 'symlink'), 'unsupported source inode kind')
        require(all(isinstance(row[key], int) and 0 <= row[key] < (1 << 31) for key in ('uid', 'gid')),
                'source owner outside numeric range')
        require(0 <= row['mode'] <= 0o7777 and row['path'] not in result, 'source mode/duplicate path invalid')
        result[row['path']] = row
    require(len(result) == 206 and result['']['kind'] == 'directory' and
            (result['']['uid'], result['']['gid']) == (0, 0), 'source root metadata differs')
    return result


def inherit_owner(relative, source):
    """Only added entries inherit UID/GID from a real source ancestor directory.

This is a stated policy for generated paths, not a claim about an Apple inode.
Source paths always use their own exact inode metadata instead.
"""
    parts = components(relative)
    for length in range(len(parts) - 1, -1, -1):
        parent = '/'.join(parts[:length])
        if parent in source and source[parent]['kind'] == 'directory':
            row = source[parent]
            return row['uid'], row['gid'], parent
    raise RuntimeError('added entry has no qualified source directory ancestor')


def kind(st):
    if stat.S_ISDIR(st.st_mode):
        return 'directory'
    if stat.S_ISREG(st.st_mode):
        return 'file'
    if stat.S_ISLNK(st.st_mode):
        return 'symlink'
    raise RuntimeError('special file in a new seed; no ownership changes attempted')


def inventory(mount):
    root_stat = mount.lstat()
    rows = {'': dict(kind='directory', mode=stat.S_IMODE(root_stat.st_mode),
                     uid=root_stat.st_uid, gid=root_stat.st_gid)}
    def fail(error):
        raise error
    root_fd = os.open(mount, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for current, dirs, files in os.walk(mount, followlinks=False, onerror=fail):
            for name in list(dirs) + files:
                path = Path(current) / name
                relative = path.relative_to(mount).as_posix()
                components(relative)
                s = path.lstat()
                which = kind(s)
                if which == 'symlink' and name in dirs:
                    dirs.remove(name)
                row = dict(kind=which, mode=stat.S_IMODE(s.st_mode), uid=s.st_uid, gid=s.st_gid)
                with safe_parent_fd(root_fd, relative) as (parent, leaf):
                    if which == 'symlink':
                        require(stat.S_ISLNK(os.stat(leaf, dir_fd=parent, follow_symlinks=False).st_mode),
                                'symlink kind changed while inventorying')
                        row['symlink_target'] = os.readlink(leaf, dir_fd=parent)
                    elif which == 'file':
                        fd = os.open(leaf, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent)
                        try:
                            before = os.fstat(fd)
                            require(stat.S_ISREG(before.st_mode), 'file kind changed while inventorying')
                            sha, count = hashlib.sha256(), 0
                            while True:
                                data = os.read(fd, 1 << 20)
                                if not data:
                                    break
                                sha.update(data)
                                count += len(data)
                            after = os.fstat(fd)
                            require(count == before.st_size == after.st_size and
                                    before.st_mtime_ns == after.st_mtime_ns,
                                    'file content changed during inventory: ' + relative)
                            row.update(file_bytes=count, content_sha256=sha.hexdigest())
                        finally:
                            os.close(fd)
                rows[relative] = row
                require(len(rows) <= 100000, 'new seed inventory too large')
    finally:
        os.close(root_fd)
    return rows


def content_manifest(rows):
    return {path: {name: row[name] for name in ('kind', 'file_bytes', 'content_sha256', 'symlink_target')
                   if name in row} for path, row in rows.items()}


def content_differences(before, after):
    original, corrected = content_manifest(before), content_manifest(after)
    added = [dict(path=path, after=corrected[path]) for path in sorted(corrected.keys() - original.keys())]
    removed = [dict(path=path, before=original[path]) for path in sorted(original.keys() - corrected.keys())]
    changed = []
    for path in sorted(original.keys() & corrected.keys()):
        fields = {name: dict(before=original[path].get(name), after=corrected[path].get(name))
                  for name in sorted(original[path].keys() | corrected[path].keys())
                  if original[path].get(name) != corrected[path].get(name)}
        if fields:
            changed.append(dict(path=path, fields=fields))
    return dict(added=added, removed=removed, changed=changed,
                added_count=len(added), removed_count=len(removed), changed_count=len(changed))


def save_snapshot(path, rows):
    path.write_text(json.dumps(rows, indent=2, sort_keys=True) + '\n')
    print(json.dumps(dict(stage='inventory-saved', file=path.name, entries=len(rows))), flush=True)


def verify_contents_unchanged(before, after, diff_path=None):
    original, corrected = content_manifest(before), content_manifest(after)
    if original != corrected:
        differences = content_differences(before, after)
        if diff_path is not None:
            diff_path.write_text(json.dumps(differences, indent=2) + '\n')
        # Print exact changed field values (hashes/lengths/link targets), not file
        # payloads. The full JSON artifact retains every path if log is bounded.
        summary = {key: differences[key] for key in ('added_count', 'removed_count', 'changed_count')}
        summary.update(stage='content-comparison-failed',
                       added=differences['added'][:40], removed=differences['removed'][:40],
                       changed=differences['changed'][:40],
                       full_diff=diff_path.name if diff_path is not None else None)
        print(json.dumps(summary, sort_keys=True), flush=True)
        raise RuntimeError('filesystem paths, file contents, or symlink targets changed during ownership fix: '
                           + json.dumps({key: differences[key] for key in
                                         ('added_count', 'removed_count', 'changed_count')}))
    manifest_sha = hashlib.sha256(json.dumps(original, sort_keys=True).encode()).hexdigest()
    return dict(unchanged=True, content_manifest_sha256=manifest_sha,
                regular_files=sum(row['kind'] == 'file' for row in after.values()),
                regular_file_bytes=sum(row.get('file_bytes', 0) for row in after.values()),
                symlinks=sum(row['kind'] == 'symlink' for row in after.values()))


def is_macos_event_log(path):
    return re.fullmatch(r'\.fseventsd/[0-9a-f]{16}', path) is not None


def expected_without_host_logs(original, source):
    """Declare the exact build-time content policy; do not ignore differences.

Apple documents .fseventsd/no_log as the per-volume storage-disable marker.
Only hex-named regular event logs are removed. All Apple template paths and
every other original file/link remain in the exact expected content manifest.
"""
    require('.fseventsd' in original and original['.fseventsd']['kind'] == 'directory',
            'the pinned input seed lacks its expected macOS event directory')
    require('.fseventsd/fseventsd-uuid' in original and
            original['.fseventsd/fseventsd-uuid']['kind'] == 'file',
            'the pinned input seed lacks its expected event UUID file')
    require(not any(path == '.fseventsd' or path.startswith('.fseventsd/') for path in source),
            'source template owns an event path; cleanup policy must be reviewed')
    require('.fseventsd/no_log' not in original, 'input already has a no_log marker; inspect its policy')
    expected, removed = {}, []
    for path, row in original.items():
        if is_macos_event_log(path):
            require(row['kind'] == 'file', 'event-log-shaped source path is not a regular file')
            removed.append(dict(path=path, facts=content_manifest({path: row})[path]))
        else:
            expected[path] = dict(row)
    expected['.fseventsd/no_log'] = dict(kind='file', mode=0o600, uid=0, gid=0,
                                       file_bytes=0, content_sha256=hashlib.sha256(b'').hexdigest())
    return expected, removed


def install_volume_no_log(mount):
    root_fd = os.open(mount, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        with safe_parent_fd(root_fd, '.fseventsd/no_log') as (parent, leaf):
            fd = os.open(leaf, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=parent)
            try:
                require(os.fstat(fd).st_size == 0, 'new no_log marker is not empty')
                os.fsync(fd)
            finally:
                os.close(fd)
    finally:
        os.close(root_fd)


def cleanup_macos_event_logs(mount, rows, source):
    """Unlink only explicitly recognized generated logs in the scratch copy."""
    removed = []
    root_fd = os.open(mount, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for path, row in rows.items():
            if not is_macos_event_log(path):
                continue
            require(path not in source and row['kind'] == 'file', 'refuse to clean a protected or nonregular event path')
            with safe_parent_fd(root_fd, path) as (parent, leaf):
                s = os.stat(leaf, dir_fd=parent, follow_symlinks=False)
                require(stat.S_ISREG(s.st_mode), 'event path changed kind before unlink')
                os.unlink(leaf, dir_fd=parent)
            removed.append(dict(path=path, facts=content_manifest({path: row})[path]))
    finally:
        os.close(root_fd)
    return removed


def expected_targets(actual, source):
    require(all(path in actual and actual[path]['kind'] == row['kind'] for path, row in source.items()),
            'new seed is missing or has changed source template path kinds')
    expected = {}
    for path, target in actual.items():
        if path in source:
            row = source[path]
            expected[path] = dict(uid=row['uid'], gid=row['gid'], mode=row['mode'],
                                  kind=row['kind'], policy='exact_source_inode')
        else:
            uid, gid, ancestor = inherit_owner(path, source)
            expected[path] = dict(uid=uid, gid=gid, mode=target['mode'], kind=target['kind'],
                                  policy='generated_entry_inherits_source_ancestor', ancestor=ancestor)
    return expected


@contextlib.contextmanager
def safe_parent_fd(root_fd, relative):
    parts = components(relative)
    current = os.dup(root_fd)
    try:
        for name in parts[:-1]:
            following = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=current)
            os.close(current)
            current = following
        yield current, parts[-1] if parts else None
    finally:
        os.close(current)


def apply_metadata(mount, targets):
    root_fd = os.open(mount, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for path in sorted(targets, key=lambda value: (len(components(value)), value)):
            row = targets[path]
            with safe_parent_fd(root_fd, path) as (parent, leaf):
                if leaf is None:
                    fd = os.dup(parent)
                elif row['kind'] == 'symlink':
                    require(stat.S_ISLNK(os.stat(leaf, dir_fd=parent, follow_symlinks=False).st_mode),
                            'target kind changed before symlink chown')
                    os.chown(leaf, row['uid'], row['gid'], dir_fd=parent, follow_symlinks=False)
                    continue
                else:
                    flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
                    if row['kind'] == 'directory':
                        flags |= os.O_DIRECTORY
                    fd = os.open(leaf, flags, dir_fd=parent)
                try:
                    require(kind(os.fstat(fd)) == row['kind'], 'target kind changed before metadata update')
                    os.fchown(fd, row['uid'], row['gid'])
                    os.fchmod(fd, row['mode'])
                finally:
                    os.close(fd)
    finally:
        os.close(root_fd)


def verify_metadata(mount, targets):
    checked = []
    for path, row in targets.items():
        s = (mount / path).lstat()
        require(kind(s) == row['kind'] and (s.st_uid, s.st_gid) == (row['uid'], row['gid']),
                'owner/kind verification failed: ' + path)
        if row['kind'] != 'symlink':
            require(stat.S_IMODE(s.st_mode) == row['mode'], 'mode verification failed: ' + path)
        checked.append(dict(path=path, uid=s.st_uid, gid=s.st_gid, mode=stat.S_IMODE(s.st_mode),
                            policy=row['policy'], ancestor=row.get('ancestor')))
    return checked


@contextlib.contextmanager
def attached_copy(raw, mount, readonly, expected_uuid):
    command = ['hdiutil', 'attach', '-imagekey', 'diskimage-class=CRawDiskImage',
               '-owners', 'on', '-nobrowse', '-mountpoint', str(mount), '-plist']
    if readonly:
        command.append('-readonly')
    command.append(str(raw))
    response = plistlib.loads(subprocess.check_output(command))
    devices = [entry['dev-entry'] for entry in response.get('system-entities', []) if entry.get('dev-entry')]
    require(devices, 'copy attachment has no device')
    try:
        records = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
        matches = [entry for entry in records.get('images', [])
                   if Path(entry.get('image-path', '')).resolve() == raw.resolve()]
        require(len(matches) == 1 and matches[0].get('writeable') is (not readonly),
                'copy attachment write policy differs')
        require(any(Path(entry.get('mount-point', '/')).resolve() == mount.resolve()
                    for entry in matches[0].get('system-entities', []) if entry.get('mount-point')),
                'copy mountpoint not attached from the fresh reconstructed raw image')
        info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
        require(info.get('FilesystemType') == 'apfs' and Path(info.get('MountPoint', '/')).resolve() == mount.resolve(),
                'copy is not the expected APFS mount')
        require(canonical_uuid(info.get('VolumeUUID')) == expected_uuid, 'copy volume UUID changed')
        yield
    finally:
        subprocess.run(['hdiutil', 'detach', devices[0]], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--seed-report', type=Path, required=True)
    parser.add_argument('--owner-plan', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--pre-first-boot', action='store_true', required=True)
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and os.geteuid() == 0, 'requires macOS sudo, only for the new seed copy')
    require(digest(args.seed) == INPUT_SEED_SHA, 'original seed differs from the exact successful cloud seed')
    report = json.loads(args.seed_report.read_text())
    require(report['os_sha256'] == OS_SHA and report['seed']['encoded_sha256'] == INPUT_SEED_SHA,
            'original report input identity mismatch')
    require(report['seed']['logical_bytes'] == LOGICAL_BYTES and report['seed_created'] is True,
            'report is not the expected first-install seed')
    original_uuid = canonical_uuid(report['seed']['data_volume_uuid'])
    source = source_plan(args.owner_plan)
    packer_path = args.core / 'drivers/DarwinBoot/Test/packed_ramdisk.py'
    require(digest(packer_path) == PACKER_SHA, 'packed-seed helper differs from fixed source')
    spec = importlib.util.spec_from_file_location('owner_fix_packer', packer_path)
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    logical, extents = packer.regions(args.seed.read_bytes())
    require(logical == LOGICAL_BYTES, 'unexpected logical seed size')
    require(not args.out.exists(), 'output directory exists; refuse to overwrite installation inputs')
    args.out.mkdir(parents=False)
    output = args.out / 'data-owners-fixed.dbr'
    with tempfile.TemporaryDirectory(prefix='new-seed-copy-', dir=args.out) as temporary:
        scratch = Path(temporary)
        raw = scratch / 'seed-copy.raw'
        with raw.open('xb') as target:
            target.truncate(logical)
            for offset, data in extents:
                target.seek(offset)
                target.write(data)
        mount = scratch / 'mount'
        mount.mkdir()
        # Record the exact original payload before any writable mount. Then
        # install the documented per-volume guard and remount, so the host event
        # daemon observes it before the ownership transaction starts.
        with attached_copy(raw, mount, True, original_uuid):
            original_contents = inventory(mount)
            save_snapshot(args.out / 'owner-fix-original-ro.json', original_contents)
        expected_contents, original_host_logs = expected_without_host_logs(original_contents, source)
        with attached_copy(raw, mount, False, original_uuid):
            install_volume_no_log(mount)
        with attached_copy(raw, mount, False, original_uuid):
            before_cleanup = inventory(mount)
            save_snapshot(args.out / 'owner-fix-before-log-cleanup-rw.json', before_cleanup)
            removed_host_logs = cleanup_macos_event_logs(mount, before_cleanup, source)
            before = inventory(mount)
            save_snapshot(args.out / 'owner-fix-before-rw.json', before)
            verify_contents_unchanged(expected_contents, before, args.out / 'owner-fix-guard-content-diff.json')
            targets = expected_targets(before, source)
            apply_metadata(mount, targets)
            verify_metadata(mount, targets)
            immediate = inventory(mount)
            save_snapshot(args.out / 'owner-fix-after-metadata-rw.json', immediate)
            (args.out / 'owner-fix-before-detach-diff.json').write_text(
                json.dumps(content_differences(before, immediate), indent=2) + '\n')
        with attached_copy(raw, mount, True, original_uuid):
            after = inventory(mount)
            save_snapshot(args.out / 'owner-fix-after-remount-ro.json', after)
            content_check = verify_contents_unchanged(before, after, args.out / 'owner-fix-content-diff.json')
            original_payload_check = verify_contents_unchanged(
                expected_contents, after, args.out / 'owner-fix-original-payload-diff.json')
            checked = verify_metadata(mount, targets)
        packed = packer.pack(raw, output)
    require(digest(args.seed) == INPUT_SEED_SHA, 'original seed changed unexpectedly')
    result = dict(status='new_seed_copy_ownership_fixed_and_readonly_rechecked',
                  original_encoded_sha256=INPUT_SEED_SHA, owner_plan_sha256=OWNER_PLAN_SHA,
                  output=output.name, seed=packed, data_volume_uuid=original_uuid,
                  volume_uuid_preserved=True, new_apfs_volume_created=False,
                  original_seed_modified=False, source_os_modified=False, phone_written=False,
                  hardware_tested=False, desktop_boot_verified=False,
                  exact_source_template_entries=206, total_verified_entries=len(checked),
                  contents_and_symlink_targets=content_check,
                  original_payload_content_check=original_payload_check,
                  macos_event_storage_policy=dict(
                      no_log_marker='.fseventsd/no_log', marker_bytes=0,
                      per_volume_only=True, global_daemon_changed=False,
                      source_template_contents_changed=False,
                      original_generated_event_logs=original_host_logs,
                      generated_logs_removed_from_scratch_copy=removed_host_logs,
                      cleanup_name_rule=r'\.fseventsd/[0-9a-f]{16}',
                      event_uuid_file_preserved=True,
                      documentation='https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/FileSystemEventSecurity/FileSystemEventSecurity.html'),
                  file_content_facts={path: row for path, row in content_manifest(after).items()
                                      if row['kind'] in ('file', 'symlink')},
                  generated_entry_policy='UID/GID inherit nearest actual source directory; modes retained for added entries',
                  seed_identity_changed=packed['sha256'] != report['seed']['sha256'],
                  preserve_corrected_seed_as_first_install_input=True,
                  entries=checked,
                  limitations=['POSIX ownership and mode restoration plus declared per-volume host event-log cleanup; ACLs/xattrs/BSD flags/data protection are not qualified.',
                               'Generated-entry owner inheritance is explicit policy, not an original Apple inode fact.',
                               'Use only before this seed is first used on a device. An existing persistent overlay must not be reset.',
                               'A successful Data metadata fix does not establish root-device publication or userland boot.'])
    (args.out / 'data-owners-fixed-report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(status=result['status'], output_sha256=packed['encoded_sha256'],
                          verified_entries=len(checked), uuid_preserved=True)), flush=True)


if __name__ == '__main__':
    main()
