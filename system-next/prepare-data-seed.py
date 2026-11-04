#!/usr/bin/env python3
"""Inspect an exact read-only iPadOS OS image; optionally create a FIRST Data seed.

No device operations or uploads. Inspection alone creates no APFS volume. First
installation is explicit and refuses replacement; its actual UUID must later be
bound into a rebuilt kernel and its exact seed preserved for that installation.
"""
import argparse
import contextlib
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import plistlib
import stat
import subprocess
import sys
import tempfile
import tomllib
from types import SimpleNamespace
import uuid

sys.dont_write_bytecode = True

CORE_REVISION = 'c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5'
DARWINBOOT_REVISION = '3c75734014cdab0acd9541e88057b7606ee7ae08'
OS_MEMBER = '044-76040-038.dmg'
OS_BYTES = 5157406069
OS_SHA256 = '3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d'
VERSION = '17.7.11'
BUILD = '21H461'
OS_URL = ('https://updates.cdn-apple.com/2025SpringFCS/fullrestores/122-77166/'
          'DCA6CFE8-90E9-4B92-880D-3DE171798E40/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(8 << 20), b''):
            result.update(data)
    return result.hexdigest()


def git_bytes(root, revision, relative):
    return subprocess.check_output(['git', '-C', str(root), 'show', revision + ':' + relative])


def validate_sources(core):
    require(subprocess.check_output(['git', '-C', str(core), 'rev-parse', 'HEAD'], text=True).strip() == CORE_REVISION,
            'unexpected core revision')
    paths = ('scripts/make-data-seed.py', 'scripts/seed-data-grow.py',
             'scripts/unixnu/ipsw.py', 'scripts/unixnu/runtime.py')
    records = []
    for relative in paths:
        actual = (core / relative).read_bytes()
        require(actual == git_bytes(core, CORE_REVISION, relative), 'modified upstream source: ' + relative)
        records.append(dict(path=relative, sha256=hashlib.sha256(actual).hexdigest()))
    db = core / 'drivers/DarwinBoot'
    require(subprocess.check_output(['git', '-C', str(db), 'rev-parse', 'HEAD'], text=True).strip() == DARWINBOOT_REVISION,
            'unexpected DarwinBoot revision')
    relative = 'Test/packed_ramdisk.py'
    actual = (db / relative).read_bytes()
    require(actual == git_bytes(db, DARWINBOOT_REVISION, relative), 'modified packed-ramdisk source')
    records.append(dict(path='drivers/DarwinBoot/' + relative, sha256=hashlib.sha256(actual).hexdigest()))
    return records


def open_os_source(core, device, cache):
    """Use the pinned IPSW reader without loading unrelated build/SoC feeds."""
    config = tomllib.loads((device / 'board.toml').read_text(encoding='utf-8')).get('ipsw')
    require(isinstance(config, dict), 'device board has no [ipsw]')
    expected = dict(product='iPad7,1', version=VERSION, build=BUILD, device_class='j120ap',
                    chip_id=0x8011, board_id=12, url=OS_URL)
    for name, value in expected.items():
        require(config.get(name) == value, 'board selected a different IPSW ' + name)
    require(config.get('sha256', {}).get(OS_MEMBER) == OS_SHA256, 'board OS SHA-256 differs from pin')
    sys.path.insert(0, str(core / 'scripts'))
    from unixnu.ipsw import Ipsw
    # Ipsw needs only board.ipsw and out_root when spec is explicit. A full
    # Context also resolves feeds/profiles and fails on a fresh seed-only job.
    ctx = SimpleNamespace(board={'ipsw': config}, out_root=cache)
    return Ipsw(ctx, spec=OS_URL)


def find_readonly_existing_attachment(info, image):
    matches = []
    for entry in info.get('images', []):
        if Path(entry.get('image-path', '')).resolve() != image.resolve():
            continue
        require(entry.get('writeable', True) is False,
                'this OS image has a writable or unqualified existing attachment')
        matches.extend(Path(entity['mount-point']) for entity in entry.get('system-entities', [])
                       if entity.get('mount-point') and
                       (Path(entity['mount-point']) / 'sbin/launchd').is_file())
    require(len(matches) <= 1, 'multiple OS mounts match the exact input')
    return matches[0] if matches else None


@contextlib.contextmanager
def readonly_os_mount(image, scratch):
    info = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
    existing = find_readonly_existing_attachment(info, image)
    if existing:
        yield existing
        return
    mount = scratch / 'os-mount'
    mount.mkdir()
    response = plistlib.loads(subprocess.check_output([
        'hdiutil', 'attach', '-readonly', '-owners', 'off', '-nobrowse', '-mountpoint', str(mount), '-plist', str(image)]))
    devices = [entity['dev-entry'] for entity in response.get('system-entities', []) if entity.get('dev-entry')]
    require(devices, 'hdiutil returned no device to detach')
    device = devices[0]
    try:
        # Re-query the real attachment record: command arguments alone are not
        # sufficient proof that we are inspecting this exact image read-only.
        check = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
        actual = find_readonly_existing_attachment(check, image)
        require(actual is not None and actual.resolve() == mount.resolve(), 'read-only mount verification failed')
        yield mount
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True)


def inspect_template(mount):
    require((mount / 'sbin/launchd').is_file(), 'no sbin/launchd in selected OS volume')
    source = mount / 'private/var'
    require(source.is_dir() and not source.is_symlink(), 'private/var is not a real template directory')
    version_file = mount / 'System/Library/CoreServices/SystemVersion.plist'
    require(version_file.is_file(), 'missing SystemVersion.plist')
    values = plistlib.loads(version_file.read_bytes())
    require((values.get('ProductVersion'), values.get('ProductBuildVersion')) == (VERSION, BUILD),
            'mounted OS version/build differs from pinned target')
    records = []
    files, directories, symlinks, regular_bytes = 0, 0, 0, 0
    def fail_walk(error):
        raise error
    for current, dirs, names in os.walk(source, followlinks=False, onerror=fail_walk):
        dirs[:] = [name for name in dirs if name != 'staged_system_apps']
        for name in list(dirs) + names:
            path = Path(current) / name
            s = path.lstat()
            kind = 'symlink' if stat.S_ISLNK(s.st_mode) else 'directory' if stat.S_ISDIR(s.st_mode) else 'file'
            if kind == 'symlink':
                symlinks += 1
                if name in dirs:
                    dirs.remove(name)
            elif kind == 'directory':
                directories += 1
            elif stat.S_ISREG(s.st_mode):
                files += 1
                regular_bytes += s.st_size
            else:
                raise RuntimeError('special file in template: ' + str(path.relative_to(source)))
            require(files + directories + symlinks <= 100000, 'template inventory exceeds safety bound')
            records.append(dict(path=path.relative_to(source).as_posix(), kind=kind,
                                mode=oct(stat.S_IMODE(s.st_mode)), uid=s.st_uid, gid=s.st_gid,
                                size=s.st_size if kind == 'file' else None,
                                target=os.readlink(path) if kind == 'symlink' else None))
    return dict(product_version=VERSION, product_build=BUILD, files=files, directories=directories,
                symlinks=symlinks, regular_bytes=regular_bytes,
                excluded_directory='staged_system_apps', entries=sorted(records, key=lambda row: row['path']),
                generated_directories=['keybags', 'mobile/tmp', 'tmp', 'run', 'containers/Bundle',
                                       'containers/Data/System', 'containers/Shared/SystemGroup', 'containers/Temp'],
                ownership_limitation='OS mount ignores ownership to read the template. Upstream copy2/chmod does not explicitly preserve uid/gid/ACL; source stat owners may be virtualized and service permissions remain a separate qualification.')


def load_packer(core):
    spec = importlib.util.spec_from_file_location('upstream_packed_ramdisk',
                                                 core / 'drivers/DarwinBoot/Test/packed_ramdisk.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inspect_seed(core, seed):
    packer = load_packer(core)
    metadata, header = packer.inspect(seed)
    require(metadata['packed'] and header[32:36] == b'NXSB', 'seed is not a packed APFS container')
    block_size = int.from_bytes(header[36:40], 'little')
    blocks = int.from_bytes(header[40:48], 'little')
    require(block_size == 4096 and 0 < blocks * block_size <= metadata['bytes'],
            'APFS container geometry outside logical seed')
    logical, _ = packer.regions(seed.read_bytes())
    return dict(**metadata,
                apfs_block_size=block_size, apfs_container_bytes=blocks * block_size,
                logical_bytes=logical)


def mounted_volume_identity(info, mount):
    """Use macOS's active APFS mount parser, never an APSB-magic scan."""
    require(info.get('FilesystemType') == 'apfs', 'mounted seed is not reported as APFS')
    require(Path(info.get('MountPoint', '/')).resolve() == mount.resolve(),
            'diskutil information is for a different mount')
    value = info.get('VolumeUUID')
    require(isinstance(value, str), 'diskutil did not report the mounted APFS VolumeUUID')
    try:
        actual = str(uuid.UUID(value)).upper()
    except ValueError as error:
        raise RuntimeError('diskutil VolumeUUID is malformed') from error
    require(actual != '00000000-0000-0000-0000-000000000000', 'diskutil returned a zero volume UUID')
    return actual


def verify_created_seed_mount(core, seed, template, scratch):
    """Mount the reconstructed new seed read-only; never repairs a filesystem."""
    packer = load_packer(core)
    logical, extents = packer.regions(seed.read_bytes())
    raw = scratch / 'verify-seed.raw'
    with raw.open('xb') as target:
        target.truncate(logical)
        for offset, data in extents:
            target.seek(offset)
            target.write(data)
    mount = scratch / 'verify-seed-mount'
    mount.mkdir()
    response = plistlib.loads(subprocess.check_output([
        'hdiutil', 'attach', '-imagekey', 'diskimage-class=CRawDiskImage', '-readonly',
        '-owners', 'on', '-nobrowse', '-mountpoint', str(mount), '-plist', str(raw)]))
    devices = [entity['dev-entry'] for entity in response.get('system-entities', []) if entity.get('dev-entry')]
    require(devices, 'seed attachment has no device')
    try:
        info = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
        matches = [entry for entry in info.get('images', [])
                   if Path(entry.get('image-path', '')).resolve() == raw.resolve()]
        require(len(matches) == 1 and matches[0].get('writeable', True) is False,
                'new seed is not proven attached read-only')
        require(any(Path(entity.get('mount-point', '/')).resolve() == mount.resolve()
                    for entity in matches[0].get('system-entities', []) if entity.get('mount-point')),
                'new APFS Data volume did not mount')
        volume_info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
        actual_uuid = mounted_volume_identity(volume_info, mount)
        for name in template['generated_directories']:
            require((mount / name).is_dir(), 'new seed lacks generated directory: ' + name)
        checked = 0
        for row in template['entries']:
            path = mount / row['path']
            if row['kind'] == 'symlink':
                require(path.is_symlink() and os.readlink(path) == row['target'],
                        'seed template symlink differs: ' + row['path'])
            elif row['kind'] == 'directory':
                require(path.is_dir(), 'seed template directory absent: ' + row['path'])
            else:
                require(path.is_file(), 'seed template file absent: ' + row['path'])
                # Upstream intentionally merges four preference files.
                if not row['path'].endswith(('/Library/Preferences/backboardd.plist',
                                            '/Library/Preferences/.GlobalPreferences.plist')):
                    require(path.stat().st_size == row['size'], 'seed template file size differs: ' + row['path'])
            checked += 1
        root_stat = mount.stat()
        return dict(actual_readonly_apfs_mount_succeeded=True, checked_template_entries=checked,
                    generated_directories_present=True, root_uid=root_stat.st_uid,
                    root_gid=root_stat.st_gid, full_filesystem_check_performed=False,
                    data_volume_uuid=actual_uuid,
                    volume_uuid_source='diskutil info -plist of the exact proven read-only seed mount',
                    arbitrary_apsb_magic_scan_used=False,
                    limitation='Read-only APFS mount and expected paths were checked; this does not validate launchd services or hardware boot.')
    finally:
        subprocess.run(['hdiutil', 'detach', devices[0]], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--os-image', type=Path)
    parser.add_argument('--fetch-os', action='store_true', help='fetch only pinned OS member from Apple, on this machine')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--first-install', action='store_true', help='explicitly create and preserve a new Data seed')
    parser.add_argument('--gib', type=int, default=3)
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'APFS creation/mount requires macOS hdiutil and diskutil; no Linux substitute is assumed')
    require(3 <= args.gib <= 1024, 'Data logical size outside upstream bounds')
    sources = validate_sources(args.core)
    args.out.mkdir(parents=True, exist_ok=True)
    report_path = args.out / 'data-seed-report.json'
    require(not report_path.exists(), 'report exists; use a fresh output directory')
    seed = args.out / 'data.dbr'
    if args.first_install:
        require(not seed.exists(), 'refuse to replace installation Data seed')
    image = args.os_image
    if image is None:
        require(args.fetch_os, 'provide --os-image or explicitly allow --fetch-os')
        source = open_os_source(args.core, args.device, args.out / 'cache')
        require((source.config['product'], source.config['version'], source.config['build']) ==
                ('iPad7,1', VERSION, BUILD), 'board selected a different target')
        member, size, _ = source.info('OS')
        require(Path(member).name == OS_MEMBER and size == OS_BYTES, 'unexpected OS member')
        image = source.disk_image('OS')
    require(image.is_file() and image.stat().st_size == OS_BYTES, 'OS input missing or wrong-sized')
    require(digest(image) == OS_SHA256, 'OS SHA-256 differs from pin')
    result = dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  target=dict(product='iPad7,1', version=VERSION, build=BUILD),
                  os_member=OS_MEMBER, os_sha256=OS_SHA256, core_revision=CORE_REVISION,
                  darwinboot_revision=DARWINBOOT_REVISION, sources=sources,
                  first_install=args.first_install, seed_created=False,
                  phone_written=False, hardware_tested=False, desktop_boot_verified=False)
    with tempfile.TemporaryDirectory(prefix='seed-inspect-', dir=args.out) as temporary:
        with readonly_os_mount(image, Path(temporary)) as mount:
            result['template'] = inspect_template(mount)
            if args.first_install:
                # Upstream grow script uses ROOT/build for its temporary image.
                (args.core / 'build').mkdir(exist_ok=True)
                subprocess.run([sys.executable, str(args.core / 'scripts/make-data-seed.py'), str(mount),
                                str(seed.resolve()), '--gib', str(args.gib), '--build', BUILD], check=True)
                result['new_seed_mount'] = verify_created_seed_mount(args.core, seed, result['template'], Path(temporary))
    if args.first_install:
        result['seed'] = inspect_seed(args.core, seed)
        result['seed']['data_volume_uuid'] = result['new_seed_mount']['data_volume_uuid']
        result['seed']['volume_uuid_source'] = result['new_seed_mount']['volume_uuid_source']
        result['seed_created'] = True
        result['preserve_exact_seed_for_installation'] = True
        result['kernel_rebuild_required_to_bind_actual_uuid'] = True
        result['seed_ownership_and_system_service_validation_pending'] = True
    report_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    public = dict(result)
    public['template'] = {key: value for key, value in result['template'].items() if key != 'entries'}
    if 'seed' in public:
        public['seed'] = {key: value for key, value in public['seed'].items() if key != 'data_volume_uuid'}
        public['new_seed_mount'] = {key: value for key, value in public['new_seed_mount'].items()
                                    if key != 'data_volume_uuid'}
    (args.out / 'data-seed-public.json').write_text(json.dumps(public, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(report=str(report_path), seed_created=result['seed_created'],
                          phone_written=False, desktop_boot_verified=False)), flush=True)


if __name__ == '__main__':
    main()
