#!/usr/bin/env python3
"""Take the PMGR archive out of the build for the candidate-minting pass.

build-sheng-platform-power.py reads the original ApplePMGR class ABI out of a kernel
already prelinked against the same kernelcache, so the first build on a clean tree has
nothing to read. The candidate is a build product carrying Apple kext bytes and is never
committed, so CI mints it: one pass without the archive, then the real build.

Two files have to change, because they are read by different code:

  board.toml              [kernel] archives -- archive_plan() in scripts/unixnu/kernel.py
                          appends every script entry unconditionally; the kernel
                          configuration does not gate it.
  configs/*_defconfig     CONFIG_XNU_SHENG_PMGR -- build-xnu-4k-baseline.sh:744 test -f's
                          the archive when it is on, and the link needs _sheng_pmgr_run.

board.toml is stored with CRLF line endings, so it is edited as bytes.
Undo with: git checkout -- board.toml configs/<name>_defconfig
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
ENTRY = b'"build-sheng-platform-power.py",'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--defconfig', default='vermeer_defconfig')
    args = parser.parse_args()

    board = ROOT / 'board.toml'
    data = board.read_bytes()
    kept = [line for line in data.split(b'\n') if ENTRY not in line]
    if len(kept) == len(data.split(b'\n')):
        sys.exit(f'{board.name}: no {ENTRY.decode()} line to remove')
    board.write_bytes(b'\n'.join(kept))

    config = ROOT / 'configs' / args.defconfig
    text = config.read_text()
    if 'CONFIG_XNU_SHENG_PMGR=y' not in text:
        sys.exit(f'{config.name}: CONFIG_XNU_SHENG_PMGR is not on')
    config.write_text(text.replace('CONFIG_XNU_SHENG_PMGR=y',
                                   '# CONFIG_XNU_SHENG_PMGR is not set'))
    print(f'{board.name}: removed the PMGR archive entry')
    print(f'{config.name}: CONFIG_XNU_SHENG_PMGR off')


if __name__ == '__main__':
    main()
