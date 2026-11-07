#!/usr/bin/env python3
"""Copy one logical partition (e.g. vendor_a) out of an Android sparse or raw super.img.

Offline only: reads the stock fastboot image the user already has on disk, never a phone.
Usage: extract-super-partition.py SUPER_IMG PARTITION OUT_IMG
"""
import bisect, struct, sys
from pathlib import Path

SPARSE_MAGIC = 0xED26FF3A
RAW, FILL, DONT_CARE, CRC = 0xCAC1, 0xCAC2, 0xCAC3, 0xCAC4


class Image:
    """Random-access view of a sparse or raw image."""

    def __init__(self, path):
        self.f = open(path, 'rb')
        head = self.f.read(28)
        magic, = struct.unpack_from('<I', head)
        self.sparse = magic == SPARSE_MAGIC
        if not self.sparse:
            return
        _, major, _, hdr, chdr, blk, total_blocks, chunks, _ = struct.unpack('<IHHHHIIII', head)
        assert major == 1, 'unsupported sparse version'
        self.blk = blk
        self.starts, self.entries = [], []
        pos, out = hdr, 0
        for _ in range(chunks):
            self.f.seek(pos)
            kind, _, count, size = struct.unpack('<HHII', self.f.read(12))
            data = pos + chdr
            if kind == FILL:
                self.f.seek(data)
                fill = self.f.read(4)
            else:
                fill = None
            if kind != CRC:
                self.starts.append(out)
                self.entries.append((out, count * blk, kind, data, fill))
                out += count * blk
            pos += size
        assert out == total_blocks * blk, 'sparse chunk table does not cover the image'
        self.size = out

    def read(self, offset, length):
        if not self.sparse:
            self.f.seek(offset)
            return self.f.read(length)
        parts = []
        while length:
            i = bisect.bisect_right(self.starts, offset) - 1
            start, size, kind, data, fill = self.entries[i]
            n = min(length, start + size - offset)
            if kind == RAW:
                self.f.seek(data + offset - start)
                parts.append(self.f.read(n))
            elif kind == FILL:
                rel = (offset - start) % 4
                parts.append((fill * ((n + rel) // 4 + 2))[rel:rel + n])
            else:
                parts.append(bytes(n))
            offset += n
            length -= n
        return b''.join(parts)


def main():
    super_img, wanted, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    img = Image(super_img)
    geo = img.read(4096, 52)
    magic, _, _, max_meta, slots, _ = struct.unpack_from('<I I 32s I I I', geo)
    assert magic == 0x616C4467, 'no LP geometry at 4096'
    meta = img.read(4096 * 3, max_meta)          # primary metadata, slot 0
    m_magic, major, minor, hsize = struct.unpack_from('<IHHI', meta)
    assert m_magic == 0x414C5030, 'bad LP metadata magic'
    tables_size, = struct.unpack_from('<I', meta, 4 + 2 + 2 + 4 + 32)
    # Table descriptors follow header fields: magic,major,minor,header_size,header_checksum[32],tables_size,tables_checksum[32]
    desc_off = 4 + 2 + 2 + 4 + 32 + 4 + 32
    descs = [struct.unpack_from('<III', meta, desc_off + 12 * i) for i in range(4)]
    tables = meta[hsize:hsize + tables_size]
    (p_off, p_n, p_sz), (e_off, e_n, e_sz) = descs[0], descs[1]
    extents = [struct.unpack_from('<QIQI', tables, e_off + e_sz * i) for i in range(e_n)]
    names = []
    for i in range(p_n):
        name, attrs, first, count, group = struct.unpack_from('<36sIIII', tables, p_off + p_sz * i)
        name = name.rstrip(b'\0').decode()
        names.append(name)
        if name != wanted:
            continue
        total = 0
        with out.open('wb') as w:
            for num_sectors, target_type, target_data, source in extents[first:first + count]:
                assert target_type == 0 and source == 0, 'only linear extents on block device 0 are supported'
                left, src = num_sectors * 512, target_data * 512
                while left:
                    n = min(left, 64 << 20)
                    w.write(img.read(src, n))
                    src += n
                    left -= n
                    total += n
        print('%s: %d bytes in %d extents -> %s' % (wanted, total, count, out))
        return
    raise SystemExit('partition %s not found; have: %s' % (wanted, ', '.join(names)))


if __name__ == '__main__':
    main()
