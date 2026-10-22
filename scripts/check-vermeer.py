#!/usr/bin/env python3
"""Check the vermeer platform intervals against the bundled device tree and stock uefiplat.cfg.

Robust port of check-sheng.py: the DTB and the K70 stock uefiplat.cfg (extracted from the
OS1.0.x uefi.img, FV at 0xa7000000) are bundled in scripts/data, so the check is
self-contained wherever the build driver runs it. The Mu-Silicium checkout is located
through $UNIXNU_UEFI or well-known relative paths.
"""
import csv
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / 'data'
DTB = DATA / 'vermeer.dtb'
STOCK_CFG = DATA / 'vermeer-uefiplat.cfg'


def find_uefi():
    candidates = [os.environ.get('UNIXNU_UEFI'),
                  HERE / '../../uefi-vermeer',
                  HERE / '../../../uefi-vermeer',
                  HERE / 'uefi-vermeer',
                  Path('uefi-vermeer')]
    for candidate in candidates:
        if candidate and (Path(candidate) / 'Platforms/Xiaomi/vermeerPkg').is_dir():
            return Path(candidate)
    return None


def cells(node, prop):
    result = subprocess.run(['fdtget', '-t', 'x', str(DTB), node, prop],
                            capture_output=True, text=True)
    if result.returncode:
        return []
    return [int(word, 16) for word in result.stdout.split()]


if __name__ == '__main__':
    uefi = find_uefi()
    platform = uefi / 'Platforms/Xiaomi/vermeerPkg' if uefi else None
    if platform is None or not (platform / 'Library/MemoryMapLib/MemoryMapLib.c').is_file():
        print('check-vermeer: Mu-Silicium vermeerPkg not found; set UNIXNU_UEFI (memory-map cross-check skipped)')
        sys.exit(0)
    nodes = subprocess.check_output(['fdtget', '-l', str(DTB), '/reserved-memory'], text=True).split()
    reservations = []
    for node in nodes:
        values = cells('/reserved-memory/' + node, 'reg')
        for index in range(0, len(values), 4):
            base = (values[index] << 32) | values[index + 1]
            size = (values[index + 2] << 32) | values[index + 3]
            reservations.append((node, base, size))
    source = (platform / 'Library/MemoryMapLib/MemoryMapLib.c').read_text()
    rows = [(m[1], int(m[2], 16), int(m[3], 16), m[4]) for m in re.finditer(
        r'\{"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),([^}]+)\}', source)]
    errors = []
    if not rows or len(rows) > 128:
        errors.append('Empty map or descriptor capacity exceeded')
    for name, base, size in reservations:
        # A descriptor may be page-rounded past the DT extent (dump_display_region is 0x800), so
        # containment counts as covered; the covering descriptor must still be a reservation.
        if not any(a <= base and base + size <= a + s and re.search(r'\bReserv\b', attrs)
                   for _, a, s, attrs in rows):
            errors.append(f'Missing reservation: {name}')
    for index, (name, base, size, _) in enumerate(rows):
        if base % 4096 or size % 4096 or not size:
            errors.append(f'Invalid UEFI page alignment: {name}')
        for other, other_base, other_size, _ in rows[index + 1:]:
            if max(base, other_base) < min(base + size, other_base + other_size):
                errors.append(f'Overlapping intervals: {name}, {other}')
    config_text = (uefi / 'Resources/Configs/vermeer.toml').read_text()
    import tomllib
    config = tomllib.loads(config_text)
    fd = config['uefi_fd']
    if not any(n == 'UEFI FD' and a == fd['base'] and s == fd['size'] for n, a, s, _ in rows):
        errors.append('FD configuration does not match memory descriptor')
    dsc = (platform / 'vermeer.dsc').read_text()
    stack = re.search(r'PcdCPUCoresStackBase\|(0x[0-9a-fA-F]+)', dsc)
    if not stack or not any(n == 'UEFI Stack' and a == int(stack[1], 16) for n, a, _, _ in rows):
        errors.append('Stack configuration does not match memory descriptor')
    dependencies = set()
    for inc in (platform / 'Include').glob('*.inc'):
        dependencies.update(re.findall(r'Binaries/vermeer/[^\s}]+', inc.read_text()))
    missing = sorted(p for p in dependencies if not (uefi / p).is_file())
    stock_count = 0
    text = STOCK_CFG.read_text().split('[MemoryMap]', 1)[1].split('[RegisterMap]', 1)[0]
    for line in text.splitlines():
        if not line.strip().startswith('0x'):
            continue
        record = next(csv.reader([line], skipinitialspace=True))
        base, size = int(record[0], 16), int(record[1], 16)
        remaining = [(base, base + size)]
        stock_count += 1
        for name, address, extent, attributes in rows:
            remaining = [piece for lo, hi in remaining
                         for piece in [(lo, min(hi, address)), (max(lo, address + extent), hi)]
                         if piece[0] < piece[1]]
            if max(base, address) < min(base + size, address + extent):
                if record[3] == 'NoMap' and 'HobOnlyNoCacheSetting' not in attributes:
                    errors.append(f'Stock NoMap range is mapped: {name}')
                if record[6] == 'Reserv' and not re.search(r'\bReserv\b', attributes):
                    errors.append(f'Stock reserved range is allocatable: {name}')
        if remaining:
            errors.append(f'Uncovered stock region: {record[2]}')
    result = {'descriptors': len(rows), 'source_reservations': len(reservations),
              'stock_regions_checked': stock_count,
              'static_errors': errors, 'missing_vendor_inputs': missing,
              'runtime_ram_verified': False, 'hardware_boot_demonstrated': False}
    out = Path('build/vermeer')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'check.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'{len(reservations)} DTS reservations checked against {len(rows)} descriptors')
    print(f'{len(errors)} static errors; {len(missing)} vendor inputs still missing (unused at VERMEER_VENDOR_HW=0); hardware unverified')
    for error in errors:
        print(error)
    raise SystemExit(bool(errors))
