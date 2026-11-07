#!/usr/bin/env python3
"""Write data/adreno-inputs.vermeer.json: UniXNU's Adreno pins with the K70 firmware set.

Source pins (linux, mesa, sm8550 DTS closure) are unchanged. The firmware section names the K70 files:
the generic GMU/SQE images UniXNU qualified, and the K70's own signed ZAP squashed from its vendor image.
Usage: make-vermeer-pins.py CORE_PINS_JSON FIRMWARE_DIR OUT_JSON [VERMEER_DTS]
"""
import hashlib, json, sys
from pathlib import Path

core, fw, out = json.loads(Path(sys.argv[1]).read_text()), Path(sys.argv[2]), Path(sys.argv[3])
files = {}
for key in ('qcom/gmu_gen70200.bin', 'qcom/a740_sqe.fw', 'qcom/sm8550/vermeer/a740_zap.mbn'):
    data = (fw / key).read_bytes()
    files[key] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
for key in ('qcom/gmu_gen70200.bin', 'qcom/a740_sqe.fw'):
    if files[key]['sha256'] != core['firmware']['files'][key]['sha256']:
        raise SystemExit(key + ' differs from the UniXNU-qualified generic image')
core['firmware'] = {
    'provenance': {
        'qcom/gmu_gen70200.bin': 'K70 vendor_a /firmware/gmu_gen70200.bin (OS1.0.7.0 V816.0.7.0.UNKCNXM); identical to linux-firmware and the UniXNU pin',
        'qcom/a740_sqe.fw': 'UniXNU-pinned generic SQE (git blob ac5758bb97c70c0d8d401cd46530456ff9eb3607); the K70 vendor ships an older 74804-byte SQE',
        'qcom/sm8550/vermeer/a740_zap.mbn': 'K70 vendor_a /firmware/a740_zap.mdt + .b00-.b02 squashed (squash-mdt.py), OS1.0.7.0 V816.0.7.0.UNKCNXM',
    },
    'files': files,
}
if len(sys.argv) > 4:
    dts = Path(sys.argv[4])
    name = 'arch/arm64/boot/dts/qcom/sm8550-xiaomi-vermeer.dts'
    core['board']['sha256'][name] = hashlib.sha256(dts.read_bytes()).hexdigest()
    core['board']['local'] = {name: 'gpu-next/board-dts/sm8550-xiaomi-vermeer.dts (written from the K70 stock DT, see README-gpu.md)'}
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(core, indent=2) + '\n')
print('wrote', out)
