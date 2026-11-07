#!/bin/bash
# Build an isolated GPU overlay tree for the K70 (vermeer) iPadOS 17.7.11 kernel build.
# Everything it creates lives under $W (default /root/k70-gpu-work); the K70 kernel tree, the UniXNU
# checkout and the survey clones are only read (symlinked), never written.
set -euo pipefail
W=${W:-/root/k70-gpu-work}
K70=/root/k70-clean-20261104
TREE=$K70/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/tree
CORE=$K70/UniXNU
SHENG=/root/survey-20261105/UniXNU_UniXNU-sheng
SM8550=/root/survey-20261105/UniXNU_UniXNU-sm8550-common
GPU_NEXT=$(cd "$(dirname "$0")/.." && pwd)
T=$W/tree
mkdir -p $T/{scripts/data,drivers,kits,build,.cache/firmware}

# Read-only inputs.
ln -sfn $CORE/components.toml $T/components.toml
ln -sfn $TREE/build/xnu $T/build/xnu
ln -sfn $TREE/.cache/xnu-work $T/.cache/xnu-work
ln -sfn $TREE/.cache/fakeroot $T/.cache/fakeroot
for c in Adreno740 AdrenoMetal GopFramebuffer; do ln -sfn $CORE/drivers/$c $T/drivers/$c; done
for c in AdrenoFirmware AdrenoPM4 ArmSmmu4K AirNir XnuPrelink; do ln -sfn $CORE/kits/$c $T/kits/$c; done
ln -sfn $SM8550/kits/Sm8550Platform $T/kits/Sm8550Platform

# Board package: a copy of the sheng package (same SoC), with the K70 files of gpu-next on top.
if [ ! -d $T/kits/vermeer ]; then cp -a $SHENG/package $T/kits/vermeer; fi
cp $GPU_NEXT/board/* $T/kits/vermeer/ 2>/dev/null || true
# The core recipes name the board package kits/sheng.
ln -sfn vermeer $T/kits/sheng
# Scripts copy (scripts resolve ROOT from their own path, so they must be real files here).
for s in kernel_target.py build-arm-driver.py build-adreno740-accelerator.py; do cp $CORE/scripts/$s $T/scripts/$s; done
cp $GPU_NEXT/scripts/*.py $T/scripts/
cp -n $CORE/scripts/data/* $T/scripts/data/ 2>/dev/null || true
cp $GPU_NEXT/data/adreno-inputs.vermeer.json $T/scripts/data/adreno-inputs.json

# K70 firmware (OEM/Qualcomm binaries, never in git): qcom/gmu_gen70200.bin, qcom/a740_sqe.fw,
# qcom/sm8550/vermeer/a740_zap.mbn.
ln -sfn $W/private/firmware $T/.cache/firmware/vermeer-gpu
mkdir -p $W/bin $W/llvm/bin && install -m 755 $GPU_NEXT/scripts/brew-shim.sh $W/bin/brew
install -m 755 $GPU_NEXT/scripts/llvm-objcopy-shim.py $W/llvm/bin/llvm-objcopy
for t in llvm-nm llvm-ar llvm-otool llvm-objdump llvm-strip; do ln -sfn /usr/lib/llvm-18/bin/$t $W/llvm/bin/$t; done
echo "overlay tree ready: $T"
