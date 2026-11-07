#!/usr/bin/env python3
"""Compile kits/vermeer/k70_gpu_glue.c with the kernel's C record (startup.o) and archive the K70 GPU board objects.

Output: build/k70-gpu/libK70Gpu.a = GpuPowerProbe.o + k70_gpu_glue.o. Link it with -force_load next to
build/adreno740/libAdreno740.a and build/adreno740-accelerator/libAdreno740Accelerator.late.a.
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from kernel_target import check_object

OUT = ROOT / 'build/k70-gpu'
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
src, obj = ROOT / 'kits/vermeer/k70_gpu_glue.c', OUT / 'k70_gpu_glue.o'
cmd, i, args = [], 0, record['arguments']
while i < len(args):
    v = args[i]
    if v == record['file']:
        cmd.append(str(src))
    elif v in ('-o', '-MF', '-MJ', '-MT', '-MQ'):
        if v == '-o':
            cmd += ['-o', str(obj)]
        i += 1
    elif v == '-Xclang' and args[i + 1].startswith('-W'):
        i += 1
    elif not v.startswith('-W') and v not in ('-MD', '-MP', '-MMD'):
        cmd.append(v)
    i += 1
cmd += ['-Wall', '-Wextra', '-Werror', '-mgeneral-regs-only']
r = subprocess.run(cmd, cwd=record['directory'], capture_output=True, text=True)
if r.returncode:
    sys.exit(r.stderr[-3000:])
check_object(obj.read_bytes(), 'k70_gpu_glue')
archive = OUT / 'libK70Gpu.a'
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', archive, OUT / 'GpuPowerProbe.o', obj], check=True)
print('libK70Gpu.a: GpuPowerProbe.o + k70_gpu_glue.o')
