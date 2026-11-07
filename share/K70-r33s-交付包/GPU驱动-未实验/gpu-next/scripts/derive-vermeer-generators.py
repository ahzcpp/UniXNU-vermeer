#!/usr/bin/env python3
"""Derive the K70 (vermeer) GPU input generators from sheng's, by exact replacements only.

Reads the four sheng generators (survey clone of UniXNU-sheng), replaces the board DTS, DTB, platform
report and ZAP names, and writes them into the overlay tree. Each replacement must match the expected
number of times, so a changed upstream script fails here instead of silently generating sheng values.
Usage: derive-vermeer-generators.py SHENG_REPO TREE
"""
import sys
from pathlib import Path

sheng, tree = Path(sys.argv[1]), Path(sys.argv[2])
plan = {
    'package/audit-adreno-platform.py': ('kits/vermeer/audit-adreno-platform.py', [
        ("'arch/arm64/boot/dts/qcom/sm8550-xiaomi-sheng.dts'", "'arch/arm64/boot/dts/qcom/sm8550-xiaomi-vermeer.dts'", 1),
        ("'sheng.preprocessed.dts'", "'vermeer.preprocessed.dts'", 1),
        ("output / 'sheng.dtb'", "output / 'vermeer.dtb'", 1),
        ("'qcom/sm8550/sheng/a740_zap.mbn'", "'qcom/sm8550/vermeer/a740_zap.mbn'", 1),
        ("'sheng-platform.json'", "'vermeer-platform.json'", 1),
        ('Check pinned sheng GPU resources', 'Check pinned vermeer (K70) GPU resources', 1),
    ]),
    'scripts/prepare-sheng-gmu.py': ('scripts/prepare-vermeer-gmu.py', [
        ("ROOT/'kits/sheng/audit-adreno-platform.py'", "ROOT/'kits/vermeer/audit-adreno-platform.py'", 1),
        ("'build/adreno-firmware/sheng-platform.json'", "'build/adreno-firmware/vermeer-platform.json'", 1),
        ("ROOT/'build/adreno-firmware/sheng.dtb'", "ROOT/'build/adreno-firmware/vermeer.dtb'", 1),
    ]),
    'scripts/prepare-sheng-gmu-votes.py': ('scripts/prepare-vermeer-gmu-votes.py', [
        ("ROOT/'kits/sheng/audit-adreno-platform.py'", "ROOT/'kits/vermeer/audit-adreno-platform.py'", 1),
        ("ROOT/'build/adreno-firmware/sheng.dtb'", "ROOT/'build/adreno-firmware/vermeer.dtb'", 1),
    ]),
    'scripts/build-adreno740-kits.py': ('scripts/build-vermeer-adreno740-kits.py', [
        ("[ROOT / 'kits/sheng/ShengGmuVotes.c']", "[ROOT / 'kits/vermeer/ShengGmuVotes.c']", 1),
        ("'-I', str(ROOT / 'kits/sheng')", "'-I', str(ROOT / 'kits/vermeer')", 1),
        ("ROOT / '.cache/firmware/sheng-gpu'", "ROOT / '.cache/firmware/vermeer-gpu'", 1),
        ("'qcom/sm8550/sheng/a740_zap.mbn'", "'qcom/sm8550/vermeer/a740_zap.mbn'", 1),
        # K70 XNU (iOS 17 source) has no per-task GPU role; see board/K70XnuCompat.h.
        ("                '-Wno-unnecessary-virtual-specifier']\n",
         "                '-Wno-unnecessary-virtual-specifier', '-include', str(ROOT / 'kits/vermeer/K70XnuCompat.h')]\n", 1),
    ]),
    'scripts/prepare-sheng-gpu-smmu.py': ('scripts/prepare-vermeer-gpu-smmu.py', [
        ("ROOT/'kits/sheng/audit-adreno-platform.py'", "ROOT/'kits/vermeer/audit-adreno-platform.py'", 1),
        ("'build/adreno-firmware/sheng-platform.json'", "'build/adreno-firmware/vermeer-platform.json'", 2),
    ]),
}
for src, (dst, subs) in plan.items():
    text = (sheng / src).read_text()
    for old, new, count in subs:
        if text.count(old) != count:
            raise SystemExit('%s: expected %d of %r, found %d' % (src, count, old, text.count(old)))
        text = text.replace(old, new)
    if 'sheng.dtb' in text or 'sheng-platform' in text or 'sheng/a740_zap' in text:
        raise SystemExit(src + ': a sheng board input is still referenced')
    out = tree / dst
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    print('derived', dst)
