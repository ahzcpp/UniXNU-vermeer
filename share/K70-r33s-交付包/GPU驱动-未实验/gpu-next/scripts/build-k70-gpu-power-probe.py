#!/usr/bin/env python3
"""Compile sheng's GPU board glue (GpuPowerProbe.cpp: GMU cold start, ZAP, CP init, Metal publication) for the K70.

Same recipe as sheng's iOS 17 scripts/build-gop-legacy-display.py uses for GpuPowerProbe.o: the kernel's
IOPlatformExpert C++ record, -Werror, the board package and the Adreno740/SMMU/firmware/PM4 kit headers,
plus the K70 XNU compatibility header. Output: build/k70-gpu/GpuPowerProbe.o and its undefined-symbol list.
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from kernel_target import check_object

OUT = ROOT / 'build/k70-gpu'
OUT.mkdir(parents=True, exist_ok=True)
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
source = ROOT / 'kits/vermeer/GpuPowerProbe.cpp'
obj = OUT / 'GpuPowerProbe.o'
cmd, i, args = [], 0, record['arguments']
while i < len(args):
    v = args[i]
    if v == record['file']:
        cmd.append(str(source))
    elif v == '-o':
        cmd += ['-o', str(obj)]
        i += 1
    elif v in ('-MF', '-MJ'):
        i += 1
    elif v == '-Xclang' and args[i + 1].startswith('-W'):
        i += 1
    elif not v.startswith('-W') and v not in ('-MD', '-MP'):
        cmd.append(v)
    i += 1
cmd += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unnecessary-virtual-specifier', '-Wno-c99-designator',
        '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion', '-I', str(ROOT / '.cache/xnu-work/osfmk'),
        '-I', str(ROOT / 'kits/vermeer'), '-I', str(ROOT / 'kits/Sm8550Platform'), '-I', str(ROOT / 'drivers/Adreno740'),
        '-I', str(ROOT / 'kits/ArmSmmu4K'), '-I', str(ROOT / 'kits/AdrenoFirmware'), '-I', str(ROOT / 'kits/AdrenoPM4'),
        '-I', str(ROOT / 'build/sheng-gmu'), '-I', str(ROOT / 'build/sheng-gpu-smmu'), '-I', str(ROOT / 'build/adreno740/include'),
        '-include', str(ROOT / 'kits/vermeer/K70XnuCompat.h')]
(OUT / 'GpuPowerProbe-command.json').write_text(json.dumps(cmd, indent=1) + '\n')
r = subprocess.run(cmd, cwd=record['directory'], capture_output=True, text=True)
(OUT / 'GpuPowerProbe-compile.log').write_text(r.stdout + r.stderr)
if r.returncode:
    sys.exit(r.stderr[-4000:])
check_object(obj.read_bytes(), 'GpuPowerProbe')
undefined = subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm', '-u', str(obj)], text=True).split()
(OUT / 'GpuPowerProbe.undefined.txt').write_text('\n'.join(undefined) + '\n')
print('GpuPowerProbe.o built (%d undefined symbols listed in build/k70-gpu/GpuPowerProbe.undefined.txt)' % len(undefined))
