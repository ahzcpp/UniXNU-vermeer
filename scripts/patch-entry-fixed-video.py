#!/usr/bin/env python3
"""Diagnostic for the exact trace01 kernel: paint entry without boot_args loads.

Writes a new copy. Keep the trace01 firmware and owned-memory window unchanged.
This is a fetch/argument-read experiment, not a functional driver or OS fix.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

TRACE01_SHA256 = 'e86629989123dcedffd2dc764470fd5ddb9c36a36e46ffdde744405688c89530'

def moves(register, value):
    result = [f'  movz {register}, #{value & 0xffff}']
    result += [f'  movk {register}, #{(value >> shift) & 0xffff}, lsl #{shift}'
               for shift in (16, 32, 48) if (value >> shift) & 0xffff]
    return '\n'.join(result) + '\n'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--core', required=True, type=Path)
    parser.add_argument('--framebuffer', required=True, type=lambda v: int(v, 0))
    parser.add_argument('--row-bytes', required=True, type=int)
    parser.add_argument('--height', required=True, type=int)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Input must remain unchanged; choose a new output path')
    data = bytearray(args.input.read_bytes())
    original_hash = hashlib.sha256(data).hexdigest()
    if original_hash != TRACE01_SHA256:
        parser.error('Input is not the independently verified trace01 payload kernel')
    total = args.row_bytes * args.height
    band = (total >> 4) & ~7
    if (args.framebuffer <= 0 or args.framebuffer & 7 or args.row_bytes <= 0 or
            args.row_bytes & 3 or args.height < 16 or not 8 <= band <= 8 * 1024 * 1024 or
            args.framebuffer + total >= 1 << 64):
        parser.error('Invalid framebuffer geometry')
    spec = importlib.util.spec_from_file_location('entry_probe', args.core / 'scripts/patch-xnu-entry-probe.py')
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    probe.load_segments(data)
    # This input already has stage2 installed, so rebase()'s unpatched-SCTLR
    # pattern deliberately does not match. Decode its actual entry branch;
    # the exact input hash and original stage1 code remain mandatory guards.
    names = {name: va for va, name in probe.kernel_symbols(args.input).items()}
    entry = probe.unixthread_pc(data)
    word = struct.unpack_from('<I', data, probe.file_offset(entry))[0]
    assert word >> 26 == 0b000101, 'Entry is not an unconditional branch'
    delta = word & 0x3ffffff
    if delta & (1 << 25):
        delta -= 1 << 26
    cave = entry + delta * 4
    cave_offset = probe.file_offset(cave)
    assert struct.unpack_from('<I', data, probe.file_offset(entry))[0] == probe.branch(entry, cave)
    source = ('.text\n.global _start\n_start:\n' +
              moves('x1', args.framebuffer + band) + moves('x2', band) +
              '  movz w6, #0x00ff\n  movk w6, #0xff00, lsl #16\n'
              '1:\n  stp w6, w6, [x1], #8\n  subs x2, x2, #8\n'
              '  b.hi 1b\n  dsb sy\n  b target\n')
    with tempfile.TemporaryDirectory() as tmp:
        old_code = probe.assemble(probe.SOURCES[1], cave, {'target': names['_start_first_cpu']}, Path(tmp))
        assert data[cave_offset:cave_offset+len(old_code)] == old_code, 'Trace01 probe differs'
        assert not any(data[cave_offset+len(old_code):cave_offset+probe.CAVE_SIZE]), 'Cave tail is not empty'
        code = probe.assemble(source, cave, {'target': names['_start_first_cpu']}, Path(tmp))
    assert len(code) <= probe.CAVE_SIZE
    before = bytes(data)
    data[cave_offset:cave_offset+probe.CAVE_SIZE] = code + bytes(probe.CAVE_SIZE-len(code))
    assert data[:cave_offset] == before[:cave_offset]
    assert data[cave_offset+probe.CAVE_SIZE:] == before[cave_offset+probe.CAVE_SIZE:]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    args.output.with_suffix('.entry-fixed-video.json').write_text(json.dumps({
        'diagnostic_id': 'entry-fixed-video-02', 'input_sha256': original_hash,
        'output_sha256_before_crypto_reseal': hashlib.sha256(data).hexdigest(),
        'entry_va': hex(entry), 'cave_va': hex(cave), 'cave_file_offset': cave_offset,
        'code_bytes': len(code), 'framebuffer': hex(args.framebuffer),
        'row_bytes': args.row_bytes, 'height': args.height, 'band_start': hex(args.framebuffer+band),
        'band_bytes': band, 'only_stage1_cave_changed': True,
        'stage1_boot_args_loads': False, 'hardware_result': 'pending'
    }, indent=2)+'\n')
    args.output.with_suffix('.entry-fixed-video.S').write_text(source)
    print('Wrote diagnostic copy:', args.output, 'code bytes:', len(code))

if __name__ == '__main__':
    main()
