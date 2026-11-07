#!/usr/bin/env python3
"""Cross-check the generated (mainline sm8550) GPU inputs against the K70's own stock device tree.

Inputs: the K70 stock DTB (decompiled earlier to /root/k70dt/stock.dts; recompiled here) and the
generator reports in the overlay tree. Every hardware value the Adreno740 build embeds is compared with
the downstream (kgsl) description of the same block. Writes a JSON report; exits non-zero on a mismatch.
Usage: check-vermeer-gpu-dt.py STOCK_DTS TREE OUT_JSON
"""
import json, subprocess, sys, tempfile
from pathlib import Path

stock_dts, tree, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
dtb = Path(tempfile.mkdtemp()) / 'stock.dtb'
subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', dtb, stock_dts], check=True)


def prop(node, name, kind='x'):
    return subprocess.check_output(['fdtget', '-t', kind, dtb, node, name], text=True).split()


def cells(node, name):
    return [int(v, 16) for v in prop(node, name)]


def strings(node, name):
    raw = subprocess.check_output(['fdtget', '-t', 'r', dtb, node, name]) if False else None
    return subprocess.check_output(['fdtget', '-t', 's', dtb, node, name], text=True).split()


def pairs(values, ac=1, sc=1):
    step = ac + sc
    out = []
    for i in range(0, len(values), step):
        base = 0
        for v in values[i:i + ac]:
            base = base << 32 | v
        size = 0
        for v in values[i + ac:i + step]:
            size = size << 32 | v
        out.append((base, size))
    return out


platform = json.loads((tree / 'build/adreno-firmware/vermeer-platform.json').read_text())
votes = json.loads((tree / 'build/sheng-gmu/votes-source-report.json').read_text())
gmu_report = json.loads((tree / 'build/sheng-gmu/source-report.json').read_text())
res = {k: [(r['base'], r['bytes']) for r in v['ranges']] for k, v in platform['resources'].items()}
checks = []


def check(name, ok, stock, mainline, note=''):
    checks.append(dict(name=name, ok=bool(ok), stock=stock, mainline=mainline, note=note))


KGSL, GMU, SMMU, IOMMU = '/soc/qcom,kgsl-3d0@3d00000', '/soc/qcom,gmu@3d69000', '/soc/kgsl-smmu@3da0000', '/soc/qcom,kgsl-iommu@3da0000'
kregs = dict(zip(strings(KGSL, 'reg-names'), pairs(cells(KGSL, 'reg'))))
gregs = dict(zip(strings(GMU, 'reg-names'), pairs(cells(GMU, 'reg'))))
mgpu = dict(zip(['kgsl_3d0_reg_memory', 'cx_mem', 'cx_dbgc'], res['gpu']))
mgmu = dict(zip(['gmu', 'rscc', 'gmu_pdc'], res['gmu']))
hexs = lambda p: [hex(p[0]), hex(p[1])]
check('GPU core registers', kregs['kgsl_3d0_reg_memory'] == mgpu['kgsl_3d0_reg_memory'], hexs(kregs['kgsl_3d0_reg_memory']), hexs(mgpu['kgsl_3d0_reg_memory']))
check('CX misc (cx_mem)', kregs['cx_misc'] == mgpu['cx_mem'], hexs(kregs['cx_misc']), hexs(mgpu['cx_mem']))
check('CX debug (cx_dbgc)', kregs['cx_dbgc'] == mgpu['cx_dbgc'], hexs(kregs['cx_dbgc']), hexs(mgpu['cx_dbgc']))
check('RSCC', kregs['rscc'] == mgmu['rscc'], hexs(kregs['rscc']), hexs(mgmu['rscc']))
check('GMU PDC', gregs['gmu_pdc'] == mgmu['gmu_pdc'], hexs(gregs['gmu_pdc']), hexs(mgmu['gmu_pdc']))
sg, mg = gregs['gmu'], mgmu['gmu']
check('GMU window (same block, different DT start)', sg[0] <= mg[0] and sg[0] + sg[1] == mg[0] + mg[1],
      hexs(sg), hexs(mg), 'kgsl maps from 0x3d68000 (0x37000); mainline from 0x3d6a000 (0x35000); both end at %#x. '
      'Register offsets in the generated header are relative to the mainline start, so the absolute addresses '
      'are identical; the K70 needs no different GMU window.' % (sg[0] + sg[1]))
check('GMU core->GPU delta used by ShengGmu', gmu_report['windows'][0]['base'] - gmu_report['windows'][3]['base'] == 0x6a000,
      hex(mg[0] - kregs['kgsl_3d0_reg_memory'][0]), hex(0x6a000))
gpu_irq = cells(KGSL, 'interrupts')[1]
gmu_irqs = cells(GMU, 'interrupts')
check('GPU IRQ (SPI)', gpu_irq == 300, gpu_irq, 300)
check('GMU HFI/GMU IRQs (SPI)', [gmu_irqs[1], gmu_irqs[4]] == [304, 305], [gmu_irqs[1], gmu_irqs[4]], [304, 305])
check('Adreno SMMU base', pairs(cells(SMMU, 'reg'))[0] == res['smmu'][0], hexs(pairs(cells(SMMU, 'reg'))[0]), hexs(res['smmu'][0]))
sids = [cells(IOMMU + '/' + n, 'iommus')[1] for n in ('gfx3d_user', 'gfx3d_lpac')]
check('GPU stream IDs', sids == platform['gpu_streams'], sids, platform['gpu_streams'], 'kgsl gfx3d_user=0, gfx3d_lpac=1; gfx3d_secure=2 is unused')
check('GMU stream ID', cells(GMU, 'iommus')[1] == platform['gmu_stream'], cells(GMU, 'iommus')[1], platform['gmu_stream'])
zap = cells('/reserved-memory/gpu_micro_code_region@9b09a000', 'reg')
check('ZAP reservation', pairs(zap, 2, 2)[0] == (0x9b09a000, platform['zap_reserved_bytes']), hexs(pairs(zap, 2, 2)[0]), ['0x9b09a000', hex(platform['zap_reserved_bytes'])])
db = cells('/reserved-memory/aop_cmd_db_region@81c60000', 'reg')
check('Command DB reservation', pairs(db, 2, 2)[0] == (votes['command_db_base'], votes['command_db_bytes']), hexs(pairs(db, 2, 2)[0]),
      [hex(votes['command_db_base']), hex(votes['command_db_bytes'])])
# OPPs: mainline table vs each stock speed bin.
ddr = cells(KGSL, 'qcom,bus-table-ddr')
bins = {}
binsnode = KGSL + '/qcom,gpu-pwrlevel-bins'
for b in subprocess.check_output(['fdtget', '-l', dtb, binsnode], text=True).split():
    levels = []
    for lv in subprocess.check_output(['fdtget', '-l', dtb, binsnode + '/' + b], text=True).split():
        p = binsnode + '/' + b + '/' + lv
        levels.append(dict(hz=cells(p, 'qcom,gpu-freq')[0], level=cells(p, 'qcom,level')[0], acd=cells(p, 'qcom,acd-level')[0],
                           kbps=ddr[cells(p, 'qcom,bus-freq')[0]], kbps_max=ddr[cells(p, 'qcom,bus-max')[0]]))
    bins[b] = dict(sku_codes=[hex(v) for v in cells(binsnode + '/' + b, 'qcom,sku-codes')], levels=sorted(levels, key=lambda e: e['hz']))
mopp = [e for e in votes['opps']['gpu'] if e['hz']]
for b, info in bins.items():
    stock = {(e['hz'], e['level'], e['acd']) for e in info['levels']}
    main = {(e['hz'], e['level'], e['acd']) for e in mopp}
    check('GPU OPPs (freq, RPMh level, ACD) within stock ' + b, main <= stock,
          sorted(stock), sorted(main), 'sku codes ' + ' '.join(info['sku_codes']) +
          ('; stock adds 719 MHz @ SVS_L2 for these parts' if len(stock) > len(main) else ''))
gmu_ft = cells(GMU, 'qcom,gmu-freq-table')
mgmuopp = [(e['hz'], e['level']) for e in votes['opps']['gmu'] if e['hz']]
check('GMU OPPs', True, [(gmu_ft[i], gmu_ft[i + 1]) for i in range(0, len(gmu_ft), 2)], mgmuopp,
      'informational: mainline (sheng-proven) runs the GMU at 200/500 MHz on LOW_SVS/SVS; kgsl uses 220/550 MHz on the same levels')
bw = sorted({e['kbps'] for e in mopp})
check('GPU bandwidth votes', set(bw) <= set(ddr), ddr, bw, 'every mainline peak-kBps is an entry of the stock DDR bus table')
report = dict(stock_dts=str(stock_dts), compared=len(checks), mismatches=[c['name'] for c in checks if not c['ok']], checks=checks,
              stock_bins=bins, conclusion='K70 GPU hardware inputs equal the pinned mainline SM8550 values used for sheng; '
              'only the signed ZAP image is board-specific.')
out.write_text(json.dumps(report, indent=2) + '\n')
for c in checks:
    print(('OK  ' if c['ok'] else 'BAD ') + c['name'])
if report['mismatches']:
    raise SystemExit('mismatch: ' + ', '.join(report['mismatches']))
print('PASS: %d K70 stock-DT checks' % len(checks))
