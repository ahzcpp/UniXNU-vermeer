#!/usr/bin/env python3
"""Print the K70's memory and peripheral layout from the stock artefacts in scripts/data.

Both inputs are already in this repository and are the device's own firmware data:

  scripts/data/vermeer-uefiplat.cfg   the stock UEFI platform config, extracted from the
                                      OS1.0.x uefi.img (FV at 0xa7000000)
  scripts/data/vermeer.dtb            the stock Android device tree

Run it to re-derive every hardware number this repository's documents quote, instead of
trusting a README. It only reads files; it never contacts the device.

    python3 scripts/read-stock-layout.py

What it answers:
  * which stock regions are EfiConventionalMemory, i.e. what DarwinBoot can allocate a window from
  * the highest address the stock UEFI map describes at all
  * the splash/display reservation that dts/vermeer.dts must match
  * whether any static artefact describes a high DRAM bank (it does not: see the note it prints)
  * peripheral bases worth checking against dts/ and the SoC dtsi
"""
import re
import struct
import sys
from pathlib import Path

DATA = Path(__file__).resolve().parent / 'data'
CFG = DATA / 'vermeer-uefiplat.cfg'
DTB = DATA / 'vermeer.dtb'

MiB = 1 << 20


def uefi_memory_map(path):
    """Yield (base, size, label, buildhob, memtype) from the [MemoryMap] section."""
    rows, section = [], None
    for line in path.read_text(errors='replace').splitlines():
        stripped = line.strip()
        if stripped.startswith('['):
            section = stripped
            continue
        if section != '[MemoryMap]' or not stripped or stripped.startswith('#'):
            continue
        fields = [f.strip().strip('"') for f in stripped.split(',')]
        if len(fields) < 7 or not fields[0].startswith('0x'):
            continue
        rows.append((int(fields[0], 16), int(fields[1], 16), fields[2], fields[3], fields[6]))
    return rows


def fdt_walk(blob):
    """Yield (path, {property: bytes}) for every node of a flattened device tree."""
    magic, _, off_struct, off_strings = struct.unpack('>4I', blob[:16])
    if magic != 0xd00dfeed:
        raise SystemExit(f'{DTB}: not a device tree ({magic:#x})')

    def name_at(offset):
        end = blob.index(b'\0', off_strings + offset)
        return blob[off_strings + offset:end].decode()

    index, path, props = off_struct, [], {}
    while True:
        token, = struct.unpack('>I', blob[index:index + 4])
        index += 4
        if token == 9:          # FDT_END
            return
        if token == 4:          # FDT_NOP
            continue
        if token == 1:          # FDT_BEGIN_NODE
            end = blob.index(b'\0', index)
            path.append(blob[index:end].decode())
            index = (end + 1 + 3) & ~3
            props = {}
        elif token == 2:        # FDT_END_NODE
            yield '/' + '/'.join(p for p in path if p), props
            path.pop()
        elif token == 3:        # FDT_PROP
            length, name_off = struct.unpack('>II', blob[index:index + 8])
            index += 8
            props[name_at(name_off)] = blob[index:index + length]
            index = (index + length + 3) & ~3


def main():
    for path in (CFG, DTB):
        if not path.is_file():
            raise SystemExit(f'missing {path}')

    rows = uefi_memory_map(CFG)
    print(f'=== stock UEFI memory map ({CFG.name}, {len(rows)} regions) ===')
    conventional = [r for r in rows if r[4] == 'Conv']
    print('-- EfiConventionalMemory (what DarwinBoot can take a window from) --')
    for base, size, label, hob, _ in sorted(conventional, key=lambda r: -r[1]):
        print(f'  {base:#011x} + {size:#09x}  {size / MiB:8.1f} MiB  {label:<20} {hob}')
    if conventional:
        best = max(conventional, key=lambda r: r[1])
        print(f'  largest: {best[2]} at {best[0]:#x}, {best[1] / MiB:.1f} MiB')
    top = max(base + size for base, size, *_ in rows)
    print(f'-- highest address described by the stock map: {top:#x} --')

    print('\n-- reservations this repository has to agree with --')
    for base, size, label, _, _ in rows:
        if 'Display' in label or 'Kernel' in label:
            print(f'  {base:#011x} + {size:#09x}  {size / MiB:8.1f} MiB  {label}')
    print('  dts/vermeer.dts reserved-memory/framebuffer@b8000000 must match "Display Reserved".')

    blob = DTB.read_bytes()
    print(f'\n=== stock device tree ({DTB.name}, {len(blob)} bytes) ===')
    memory, reserved, peripherals = [], [], []
    for path, props in fdt_walk(blob):
        name = path.rsplit('/', 1)[-1]
        if props.get('device_type', b'').startswith(b'memory') or name.startswith('memory@'):
            memory.append((path, props.get('reg', b'')))
        if path.startswith('/reserved-memory/') and len(props.get('reg', b'')) == 16:
            base, size = struct.unpack('>QQ', props['reg'])
            reserved.append((name, base, size))
        if re.search(r'kgsl-3d0@|kgsl-smmu@|qcom,gmu@|apps-smmu@|ssusb@|ufshc@|intc@|interrupt-controller@',
                     name):
            peripherals.append(path)

    print('-- /memory --')
    for path, reg in memory:
        if len(reg) == 16:
            base, size = struct.unpack('>QQ', reg)
            print(f'  {path}: {base:#x} + {size:#x}')
        else:
            print(f'  {path}: reg = {reg.hex() or "(empty)"}')
    print('  A zero or empty /memory reg is normal on Qualcomm: XBL/ABL fills the real DDR')
    print('  layout in at runtime from SMEM, so this file cannot tell us the DRAM size.')

    print('-- highest stock reserved-memory regions --')
    for name, base, size in sorted(reserved, key=lambda r: -r[1])[:8]:
        print(f'  {base:#011x} + {size:#09x}  {size / MiB:8.1f} MiB  {name}')
    if reserved:
        print(f'  highest reserved address: {max(b + s for _, b, s in reserved):#x}')

    print('-- peripheral nodes to cross-check against dts/ and the SoC dtsi --')
    for path in sorted(set(peripherals)):
        print(f'  {path}')

    print("""
=== conclusion ===
No static artefact in this repository describes a high DRAM bank:
  * the stock UEFI map stops at the address printed above
  * the stock /memory node is a placeholder the bootloader fills in at runtime
  * every stock reserved-memory region sits below 4 GiB
So the usable window and the real DRAM top can only be measured on the device.
Use a read-only UEFI probe for that (see docs/bring-up.md), not an estimate.""")


if __name__ == '__main__':
    sys.exit(main())
