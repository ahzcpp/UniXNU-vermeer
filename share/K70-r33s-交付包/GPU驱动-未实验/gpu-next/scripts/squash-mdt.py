#!/usr/bin/env python3
"""Squash a split Qualcomm firmware (NAME.mdt + NAME.bNN) into one .mbn, like pil-squasher.

Each program segment with file bytes is taken from its .bNN file and placed at its p_offset;
the result keeps the signed hash segment, so the TrustZone signature check is unchanged.
Usage: squash-mdt.py DIR/a740_zap.mdt OUT.mbn
"""
import hashlib, struct, sys
from pathlib import Path


def main():
    mdt, out = Path(sys.argv[1]), Path(sys.argv[2])
    m = mdt.read_bytes()
    if m[:4] != b'\x7fELF':
        raise SystemExit('not an ELF .mdt')
    if m[4] == 1:
        phoff, = struct.unpack_from('<I', m, 0x1c)
        phnum, = struct.unpack_from('<H', m, 0x2c)
        ph = [struct.unpack_from('<IIIIIIII', m, phoff + 32 * i) for i in range(phnum)]
        segs = [(p[1], p[4]) for p in ph]                      # (p_offset, p_filesz)
    else:
        phoff, = struct.unpack_from('<Q', m, 0x20)
        phnum, = struct.unpack_from('<H', m, 0x38)
        ph = [struct.unpack_from('<IIQQQQQQ', m, phoff + 56 * i) for i in range(phnum)]
        segs = [(p[2], p[5]) for p in ph]
    img = bytearray(max(o + n for o, n in segs))
    for i, (off, size) in enumerate(segs):
        if not size:
            continue
        part = mdt.with_suffix('.b%02d' % i)
        blob = part.read_bytes() if part.is_file() else (m[off:off + size] if off + size <= len(m) else b'')
        if len(blob) != size:
            raise SystemExit('segment %d: %s has %d bytes, header says %d' % (i, part.name, len(blob), size))
        img[off:off + size] = blob
    out.write_bytes(img)
    print('%s %d bytes sha256 %s' % (out, len(img), hashlib.sha256(img).hexdigest()))


if __name__ == '__main__':
    main()
