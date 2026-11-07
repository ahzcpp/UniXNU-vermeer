#!/usr/bin/env python3
"""Which symbols the K70 GPU objects still need at kernel link time.

Defined set: the K70 native kernel image (an rNN *.unstripped.noctf, read only) + the GPU archives built in the
overlay tree. Needed set: undefined symbols of GpuPowerProbe.o, libAdreno740.a and the accelerator archive.
Prints the unresolved remainder and the CPU-only stub symbols the GPU archive now defines (the rNN gpu-absent.o
must be dropped when linking the GPU build). Usage: gpu-link-closure.py NATIVE_KERNEL TREE OUT_JSON
"""
import json, subprocess, sys
from pathlib import Path

NM = '/usr/lib/llvm-18/bin/llvm-nm'
kernel, tree, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])


def nm(path, *flags):
    text = subprocess.check_output([NM, *flags, str(path)], text=True, stderr=subprocess.DEVNULL)
    return [l.split() for l in text.splitlines() if l.strip() and not l.endswith(':')]


def defined(path, local=False):
    # The final kernel image keeps many symbols the static link resolves (proc_iterate, the UBSan minimal handlers)
    # as non-external; they are still global in the objects the link reads, so count every defined kernel symbol.
    return {f[-1] for f in nm(path, '--defined-only', *(() if local else ('-g',))) if len(f) >= 3}


def undefined(path):
    return {f[-1] for f in nm(path, '-u')}


# libAdreno740.a already carries the staged accelerator objects (build-adreno740-accelerator.py appends them).
gpu = {'libK70Gpu.a': tree / 'build/k70-gpu/libK70Gpu.a',
       'libAdreno740.a': tree / 'build/adreno740/libAdreno740.a'}
kdef = defined(kernel, local=True)
gdef = set().union(*(defined(p) for p in gpu.values()))
need = set().union(*(undefined(p) for p in gpu.values()))
missing = sorted(need - kdef - gdef)
stubs = sorted(s for s in ('_native_adreno_fences', '_native_adreno_fences_detail', '_native_gpu_trace_enabled',
                           '_native_gpu_trace_record') if s in gdef)
dup = sorted((gdef & kdef) - set(stubs))
report = dict(kernel=str(kernel), archives={k: str(v) for k, v in gpu.items()}, unresolved=missing,
              replaces_cpu_only_stubs=stubs, also_defined_in_kernel=dup)
out.write_text(json.dumps(report, indent=2) + '\n')
print('unresolved: %d' % len(missing))
for s in missing:
    print('  ' + s)
print('GPU archive defines the CPU-only stubs:', ', '.join(stubs) or 'none')
print('duplicates with the kernel image: %d' % len(dup))
for s in dup[:40]:
    print('  dup ' + s)
