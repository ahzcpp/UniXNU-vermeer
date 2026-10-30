#!/usr/bin/env python3
"""Narrow board.toml and the defconfig to what a given CI runner can actually build.

Two separate reasons, two separate switches. Undo either with
`git checkout -- board.toml configs/<name>_defconfig`.

--drop-pmgr   For the first of CI's two kernel passes.
              build-sheng-platform-power.py reads the original ApplePMGR class ABI out
              of a kernel already prelinked against the same kernelcache, so the first
              build on a clean tree has nothing to read and stops with "No reference
              candidate available". The candidate carries Apple kext bytes and is never
              committed, so CI mints it: pass 1 without the archive, then the real build.

--baseline-boot  For the boot image job, which packs the payload.
              The device repository is configured for a ramdisk-root round: kernel_args
              carries rd=md0 and [payload] carries the stage 4 reset probe. Neither can
              mean anything here, because the payload step in CI is not given
              --data-ramdisk, and unixnu/payload.py only emits the ADT's
              --data-ramdisk/--packed-data-ramdisk flag `if data`. With no
              /chosen/memory-map/RAMDisk node, IOFindBSDRoot never reaches the mdevadd
              that the probe is placed after, so a CI image would report "did not get
              past _mdevadd" for the one reason that has nothing to do with the
              question. Rather than ship an image whose answer is a false negative,
              CI packs the plain baseline: no ramdisk root, no probe, panic screen on.

--drop-gpu    For both runners, in both passes.
              build-native-adreno-accelerator.py finds its tools through
              `brew --prefix llvm`, and there is no brew on that runner. It also needs
              --globalize-symbol, which llvm-objcopy does not implement for Mach-O, and
              unlike the PMGR archive that option is load-bearing: GpuPowerProbe.cpp:98
              declares the static initialiser in a different translation unit from the
              one that defines it, so dropping the option would leave the accelerator
              silently inert. The GPU is a stage 7 concern; the boot image this builds
              is for reaching the root volume, so the archives come out instead.
              The macos-15 runner has brew and a working --globalize-symbol, but the
              component itself does not survive -Werror there: drivers/Adreno740/
              NativeAdrenoService.cpp:337-348 declares nine variables it never uses, so
              component-Adreno740 ends the macOS kernel job with 9 unused-variable
              errors. Silencing them would have to go through build-arm-driver.py, which
              five components we do care about share, so the component comes out there
              too until the GPU is the subject.

--verify      Asserts the committed board.toml and defconfig are in their source
              state, which is to say NOT the output of this script. It exists because
              the output got committed once: a local test ran --drop-pmgr --drop-gpu,
              only board.toml was restored afterwards, and the defconfig went in with
              CONFIG_XNU_SHENG_PMGR and CONFIG_XNU_NATIVE_ADRENO switched off. That is
              a bad commit twice over -- CI's own --drop-pmgr then exits because the
              switch is already off, so the kernel job dies nine steps in, and anyone
              building from that commit gets a kernel with no PMGR and walks back into
              the AMFI wall. CI runs this first so the failure is one line at the top
              instead of a mangled kernel at the bottom.

Each switch touches every file that has a say, because different code reads each one:

  board.toml  [components] include -- archive_plan() raises "needs component entries
              for" when a selected component has no component: line.
  board.toml  [kernel] archives    -- archive_plan() appends script entries
              unconditionally; the kernel configuration does not gate them.
  configs/*   CONFIG_XNU_*         -- build-xnu-4k-baseline.sh test -f's the archive at
              the final link, and the objects reference its symbols.

board.toml is stored with CRLF, so it is edited as bytes: a read_text round trip would
rewrite every line ending and bury the change in the diff.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

PMGR_ARCHIVES = [b'"build-sheng-platform-power.py",']
PMGR_CONFIG = ['CONFIG_XNU_SHENG_PMGR']
GPU_ARCHIVES = [b'"component:Adreno740",',
                b'"build-native-adreno-kits.py",',
                b'"build-native-adreno-accelerator.py",']
GPU_COMPONENTS = [b'"Adreno740", ', b'"Adreno740",', b'"Adreno740"']
GPU_CONFIG = ['CONFIG_XNU_NATIVE_ADRENO']
RAMDISK_ROOT_ARGS = [b'"rd=md0",']
PROBE_KEYS = [b'entry_probe_reset_delay', b'entry_probe_reset_after',
              b'entry_probe_reset_in', b'entry_probe_reset_occurrence']


def drop_lines(data, markers, what):
    lines = data.split(b'\n')
    kept = [line for line in lines if not any(m in line for m in markers)]
    if len(kept) == len(lines):
        sys.exit(f'board.toml: no {what} archive lines to remove')
    return b'\n'.join(kept), len(lines) - len(kept)


def drop_component(data, markers):
    for marker in markers:
        if marker in data:
            return data.replace(marker, b'', 1), True
    return data, False


def set_scalar(data, key, old, new):
    marker = b'%s = %s' % (key, old)
    if marker not in data:
        sys.exit('board.toml: %s is not %s' % (key.decode(), old.decode()))
    return data.replace(marker, b'%s = %s' % (key, new), 1)


def turn_off(name, config):
    text = config.read_text()
    on = f'{name}=y'
    if on not in text:
        sys.exit(f'{config.name}: {name} is not on')
    config.write_text(text.replace(on, f'# {name} is not set'))


def verify(data, config):
    """The invariants a commit must hold: every switch this script turns off is on."""
    text = config.read_text()
    problems = [f'{config.name}: {name} is not =y' for name in PMGR_CONFIG + GPU_CONFIG
                if f'{name}=y' not in text]
    problems += [f'board.toml: {m.decode()} missing from [kernel] archives'
                 for m in PMGR_ARCHIVES + GPU_ARCHIVES if m not in data]
    if not any(m in data for m in GPU_COMPONENTS):
        problems.append('board.toml: Adreno740 missing from [components] include')
    problems += [f'board.toml: {m.decode()} missing from [payload] kernel_args'
                 for m in RAMDISK_ROOT_ARGS if m not in data]
    problems += [f'board.toml: {k.decode()} missing from [payload]'
                 for k in PROBE_KEYS if k not in data]
    if problems:
        for line in problems:
            print('  ' + line)
        sys.exit('The committed board configuration looks like the output of this script, '
                 'not its input. Restore board.toml and the defconfig from git and commit that.')
    print('board configuration is in its source state')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--drop-pmgr', action='store_true')
    parser.add_argument('--drop-gpu', action='store_true')
    parser.add_argument('--baseline-boot', action='store_true')
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--defconfig', default='vermeer_defconfig')
    args = parser.parse_args()
    if not (args.drop_pmgr or args.drop_gpu or args.baseline_boot or args.verify):
        parser.error('nothing to do: pass --drop-pmgr, --drop-gpu, --baseline-boot, --verify or several')

    board = ROOT / 'board.toml'
    data = board.read_bytes()
    config = ROOT / 'configs' / args.defconfig

    if args.verify:
        verify(data, config)
        if not (args.drop_pmgr or args.drop_gpu or args.baseline_boot):
            return

    if args.drop_pmgr:
        data, n = drop_lines(data, PMGR_ARCHIVES, 'PMGR')
        print(f'board.toml: removed {n} PMGR archive line')
        for name in PMGR_CONFIG:
            turn_off(name, config)
            print(f'{config.name}: {name} off')

    if args.drop_gpu:
        data, n = drop_lines(data, GPU_ARCHIVES, 'GPU')
        print(f'board.toml: removed {n} GPU archive lines')
        data, done = drop_component(data, GPU_COMPONENTS)
        if not done:
            sys.exit('board.toml: Adreno740 is not in [components] include')
        print('board.toml: Adreno740 out of [components] include')
        for name in GPU_CONFIG:
            turn_off(name, config)
            print(f'{config.name}: {name} off')

    if args.baseline_boot:
        data, n = drop_lines(data, RAMDISK_ROOT_ARGS, 'ramdisk root')
        print(f'board.toml: removed {n} rd=md0 line from [payload] kernel_args')
        data, n = drop_lines(data, PROBE_KEYS, 'entry probe')
        print(f'board.toml: removed {n} entry_probe_reset_* lines')
        data = set_scalar(data, b'panic_screen', b'false', b'true')
        print('board.toml: panic_screen on (the probe cave it conflicts with is gone)')

    board.write_bytes(data)


if __name__ == '__main__':
    main()
