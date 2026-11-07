#!/bin/bash
# Build the kernel-side GPU objects for the K70 iPadOS 17.7.11 kernel in the overlay tree (offline only).
#   1. NativeAdreno.o (Adreno740 service)        -> build/adreno740/libAdreno740.a
#   2. GMU/HFI/SMMU/PM4 kits + driver + firmware   -> appended to build/adreno740/libAdreno740.a
#   3. Staged AGXAccelerator class                 -> build/adreno740-accelerator/
#   4. GpuPowerProbe.o (sheng board glue, iOS 17)  -> build/k70-gpu/GpuPowerProbe.o
set -euo pipefail
G=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/root/k70-gpu-work}
T=$W/tree
LOG=$W/logs; mkdir -p $LOG
bash $G/scripts/setup-gpu-tree.sh
python3 -I $G/scripts/derive-vermeer-generators.py /root/survey-20261105/UniXNU_UniXNU-sheng $T
cp $G/board-dts/sm8550-xiaomi-vermeer.dts $T/.cache/linux-dts/arch/arm64/boot/dts/qcom/
cd $T
for s in prepare-vermeer-gmu prepare-vermeer-gmu-votes prepare-vermeer-gpu-smmu; do python3 scripts/$s.py; done
step() { local name=$1; shift; echo "== $name"; if python3 $G/scripts/k70env.py "$@" >$LOG/$name.log 2>&1; then tail -1 $LOG/$name.log; else echo "FAILED $name (log $LOG/$name.log)"; tail -40 $LOG/$name.log; return 1; fi; }
step arm-driver-adreno740 python3 scripts/build-arm-driver.py Adreno740
step adreno740-kits python3 scripts/build-vermeer-adreno740-kits.py
step adreno740-accelerator python3 scripts/build-adreno740-accelerator.py
step gpu-power-probe python3 scripts/build-k70-gpu-power-probe.py
step k70-gpu-glue python3 scripts/build-k70-gpu-glue.py
KERNEL=${KERNEL:-$(ls -t /root/k70-clean-20261104/storage-candidate-20261104/r*-20261*/mach.r*.unstripped.noctf | head -1)}
python3 -I $G/scripts/gpu-link-closure.py "$KERNEL" $T $G/reports/gpu-link-closure.json
