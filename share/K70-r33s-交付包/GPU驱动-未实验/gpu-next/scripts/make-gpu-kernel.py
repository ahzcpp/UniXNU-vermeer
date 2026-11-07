#!/usr/bin/env python3
"""Derive a GPU kernel build script from a K70 usb-next/build-rNN-kernel.py, by exact replacements only.

Changes against the base, nothing else:
 1. outputs: OUT=/root/k70-gpu-work/kernel-rNN-gpu, result JSONs in gpu-next/kernel/ (the base's usb-next files
    and storage-candidate rNN directory are never written);
 2. bsd_init.c: sheng_gpu_power_probe() right after native_keystore_probe() (sheng calls it at the same point of
    its late probe); it does nothing unless sheng-gpu-power is set;
 3. link: gpu-absent.o is dropped and libAdreno740.a (driver, kits, firmware, staged accelerator) and libK70Gpu.a
    are force-loaded (they define the fence/trace API the display archive imports);
 4. the final symbol check also requires the GPU probe and the embedded GMU/SQE/ZAP images.
Usage: make-gpu-kernel.py usb-next/build-r32-kernel.py OUT_SCRIPT
"""
import re, sys
from pathlib import Path

base, out = Path(sys.argv[1]), Path(sys.argv[2])
text = base.read_text()
n = re.search(r"OUT=WORK/'(r(\d+)-[^']+)'", text)
if not n:
    raise SystemExit('no OUT=WORK/... in base')
rnn = 'r' + n[2]


def sub(old, new, count=1):
    global text
    if text.count(old) != count:
        raise SystemExit('expected %d of %r, found %d' % (count, old, text.count(old)))
    text = text.replace(old, new)


sub('HERE=Path(__file__).resolve().parent\n',
    "GPU_OUT=Path(__file__).resolve().parent;HERE=GPU_OUT.parent.parent/'usb-next'\n"
    "GPU_TREE=Path('/root/k70-gpu-work/tree')\n"
    # build-adreno740-accelerator.py also appends its late objects to libAdreno740.a; its own archive is not linked again.
    "GPU_ARCHIVES=[GPU_TREE/'build/adreno740/libAdreno740.a',GPU_TREE/'build/k70-gpu/libK70Gpu.a']\n")
sub("OUT=WORK/'%s'" % n[1], "OUT=Path('/root/k70-gpu-work/kernel-%s-gpu')" % rnn)
sub("(HERE/'%s-kernel-result.json')" % rnn, "(GPU_OUT/'%s-gpu-kernel-result.json')" % rnn)
sub("HERE/'%s-final-storage-verification.json'" % rnn, "GPU_OUT/'%s-gpu-final-storage-verification.json'" % rnn, 2)
sub(r'\t\tnative_keystore_probe();\n',
    r'\t\tnative_keystore_probe();\n\t\t/* GPU build: Adreno 740 power-up, ZAP, CP and Metal publication (sheng-gpu-power=N). */\n'
    r'\t\textern void sheng_gpu_power_probe(void);\n\t\tsheng_gpu_power_probe();\n')
sub('explicit.append(absent_obj)\n',
    "for archive in GPU_ARCHIVES:\n    assert archive.is_file(),archive\n"
    "selected+=['-Wl,-force_load,'+str(a) for a in GPU_ARCHIVES]  # GPU build: real fence/trace API, no gpu-absent.o\n")
sub("'_boot_progress_ext'):", "'_boot_progress_ext','_sheng_gpu_power_probe','_native_adreno_firmware_image',"
    "'_native_adreno_sqe_firmware_image','_native_adreno_zap_image'):")
sub("('CPU-only GPU stub missing',s)", "('GPU fence/trace API missing',s)")
text = text.replace('"""%s kernel:' % rnn, '"""%s-gpu kernel (derived by gpu-next/scripts/make-gpu-kernel.py from build-%s-kernel.py):' % (rnn, rnn), 1)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(text)
print('wrote', out)
