#!/usr/bin/env python3
"""Minimal read-only EROFS extractor (pure Python, no external code).

Supports what the K70 stock vendor image uses: 4 KiB blocks, flat plain/inline files,
LZ4 compression with 0padding, full (legacy) and compact 4B/2B indexes, 1-block pclusters.
Refuses anything else (big pcluster, ztailpacking, fragments, chunks, other algorithms).

Usage: erofs-extract.py IMAGE OUT_DIR /firmware [/etc/...]
"""
import os, stat, struct, sys
from pathlib import Path

EROFS_MAGIC = 0xE0F5E1E2
PLAIN, COMPRESSED_FULL, INLINE, COMPRESSED_COMPACT, CHUNK = 0, 1, 2, 3, 4
L_PLAIN, L_HEAD1, L_NONHEAD, L_HEAD2 = 0, 1, 2, 3
ADV_COMPACT_2B, ADV_BIG1, ADV_BIG2, ADV_INLINE, ADV_INTERLACED, ADV_FRAGMENT = 1, 2, 4, 8, 0x10, 0x20


def lz4_block(src, limit):
    out, i, n = bytearray(), 0, len(src)
    while i < n and len(out) < limit:
        token = src[i]; i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255:
                    break
        out += src[i:i + lit]; i += lit
        if i >= n or len(out) >= limit:
            break
        off = src[i] | (src[i + 1] << 8); i += 2
        if not off or off > len(out):
            raise ValueError('bad LZ4 offset')
        ml = token & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255:
                    break
        ml += 4
        start = len(out) - off
        if off >= ml:
            out += out[start:start + ml]
        else:
            for k in range(ml):
                out.append(out[start + k])
    return bytes(out)


class Erofs:
    def __init__(self, path):
        self.f = open(path, 'rb')
        sb = self.pread(1024, 128)
        magic, = struct.unpack_from('<I', sb, 0)
        if magic != EROFS_MAGIC:
            raise SystemExit('not EROFS')
        self.compat, = struct.unpack_from('<I', sb, 8)
        self.blkbits = sb[12]
        self.root, = struct.unpack_from('<H', sb, 14)
        self.meta, = struct.unpack_from('<I', sb, 40)
        self.incompat, = struct.unpack_from('<I', sb, 80)
        self.bs = 1 << self.blkbits
        # Only 0padding (bit 0) is supported.
        if self.incompat & ~0x1:
            raise SystemExit('unsupported incompat features 0x%x' % self.incompat)

    def pread(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def inode(self, nid):
        iloc = self.meta * self.bs + nid * 32
        raw = self.pread(iloc, 64)
        fmt, xcnt, mode = struct.unpack_from('<HHH', raw, 0)
        ext = fmt & 1
        layout = (fmt >> 1) & 7
        if ext:
            size, = struct.unpack_from('<Q', raw, 8)
            isize = 64
        else:
            size, = struct.unpack_from('<I', raw, 8)
            isize = 32
        u, = struct.unpack_from('<I', raw, 16)
        xsize = 12 + 4 * (xcnt - 1) if xcnt else 0
        return dict(nid=nid, iloc=iloc, mode=mode, layout=layout, size=size, u=u, isize=isize, xsize=xsize)

    def data(self, ino):
        size, layout = ino['size'], ino['layout']
        if layout == PLAIN:
            return self.pread(ino['u'] * self.bs, size)
        if layout == INLINE:
            full = size - size % self.bs if size % self.bs else size
            head = self.pread(ino['u'] * self.bs, full) if full else b''
            tail = self.pread(ino['iloc'] + ino['isize'] + ino['xsize'], size - full)
            return head + tail
        if layout in (COMPRESSED_FULL, COMPRESSED_COMPACT):
            return self.compressed(ino)
        raise SystemExit('unsupported data layout %d (nid %d)' % (layout, ino['nid']))

    def compressed(self, ino):
        end = ino['iloc'] + ino['isize'] + ino['xsize']
        hdr_pos = (end + 7) & ~7
        hdr = self.pread(hdr_pos, 8)
        advise, = struct.unpack_from('<H', hdr, 4)
        algo, cbits = hdr[6], hdr[7]
        if advise & (ADV_BIG1 | ADV_BIG2 | ADV_INLINE | ADV_INTERLACED | ADV_FRAGMENT) or (algo & 0xf) != 0:
            raise SystemExit('unsupported compressed inode nid %d advise 0x%x algo 0x%x' % (ino['nid'], advise, algo))
        lbits = self.blkbits + (cbits & 7)
        if lbits != 12:
            raise SystemExit('only 4 KiB logical clusters are supported')
        size = ino['size']
        total = (size + (1 << lbits) - 1) >> lbits
        heads = []                                   # (logical start, pblk, type)
        if ino['layout'] == COMPRESSED_FULL:
            base = hdr_pos + 8 + 8
            raw = self.pread(base, total * 8)
            for lcn in range(total):
                adv, cofs, u = struct.unpack_from('<HHI', raw, lcn * 8)
                t = adv & 3
                if t != L_NONHEAD:
                    heads.append(((lcn << lbits) + cofs, u, t))
        else:
            ebase = hdr_pos + 8
            init4 = ((32 - ebase % 32) // 4) & 7
            c2b = 0
            if advise & ADV_COMPACT_2B and init4 < total:
                c2b = (total - init4) // 16 * 16
            for lcn in range(total):
                pos, shift, l = ebase, 2, lcn
                if l >= init4:
                    pos += init4 * 4; l -= init4
                    if l < c2b:
                        shift = 1
                    else:
                        pos += c2b * 2; l -= c2b
                pos += l << shift
                vcnt = 2 if shift == 2 else 16
                pack = vcnt << shift
                ebits = (pack - 4) * 8 // vcnt
                base = pos - (pos % pack)          # packs are aligned to their size
                data = self.pread(base, pack)
                i = (pos - base) >> shift

                def dec(j):
                    bit = ebits * j
                    v = int.from_bytes(data[bit // 8:bit // 8 + 4].ljust(4, b'\0'), 'little') >> (bit & 7)
                    return v & 0xfff, (v >> 12) & 3
                lo, t = dec(i)
                if t == L_NONHEAD:
                    continue
                nblk, j = 1, i
                while j > 0:
                    j -= 1
                    l2, t2 = dec(j)
                    if t2 == L_NONHEAD:
                        j -= l2
                    if j >= 0:
                        nblk += 1
                pblk = struct.unpack_from('<I', data, pack - 4)[0] + nblk
                heads.append(((lcn << lbits) + lo, pblk, t))
        out = bytearray()
        for k, (la, pblk, t) in enumerate(heads):
            stop = heads[k + 1][0] if k + 1 < len(heads) else size
            if la != len(out):
                raise SystemExit('extent gap at %d (nid %d)' % (la, ino['nid']))
            n = stop - la
            blk = self.pread(pblk * self.bs, self.bs)
            if t == L_PLAIN:
                piece = blk[:n]
            elif t == L_HEAD1:
                z = 0
                while z < len(blk) and blk[z] == 0:
                    z += 1
                piece = lz4_block(blk[z:], n)
            else:
                raise SystemExit('HEAD2 lcluster not supported')
            if len(piece) < n:
                raise SystemExit('short extent at %d (nid %d)' % (la, ino['nid']))
            out += piece[:n]
        if len(out) != size:
            raise SystemExit('size mismatch nid %d' % ino['nid'])
        return bytes(out)

    def listdir(self, ino):
        d = self.data(ino)
        entries = []
        for b in range(0, len(d), self.bs):
            blk = d[b:b + self.bs]
            first = struct.unpack_from('<H', blk, 8)[0]
            n = first // 12
            ents = [struct.unpack_from('<QHB', blk, 12 * k) for k in range(n)]
            for k, (nid, nameoff, ftype) in enumerate(ents):
                stop = ents[k + 1][1] if k + 1 < n else len(blk)
                name = blk[nameoff:stop].split(b'\0', 1)[0].decode()
                if name not in ('.', '..'):
                    entries.append((name, nid))
        return entries

    def lookup(self, path):
        ino = self.inode(self.root)
        for part in [p for p in path.split('/') if p]:
            match = [nid for name, nid in self.listdir(ino) if name == part]
            if not match:
                raise SystemExit('not found: ' + path)
            ino = self.inode(match[0])
        return ino


def extract(fs, ino, dest, rel, manifest):
    mode = ino['mode']
    if stat.S_ISDIR(mode):
        dest.mkdir(parents=True, exist_ok=True)
        for name, nid in fs.listdir(ino):
            extract(fs, fs.inode(nid), dest / name, rel + '/' + name, manifest)
    elif stat.S_ISREG(mode):
        blob = fs.data(ino)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)
        manifest.append((rel, len(blob)))
    elif stat.S_ISLNK(mode):
        manifest.append((rel, '-> ' + fs.data(ino).decode(errors='replace')))


def main():
    fs = Erofs(sys.argv[1])
    out = Path(sys.argv[2])
    manifest = []
    for path in sys.argv[3:]:
        extract(fs, fs.lookup(path), out / path.strip('/'), path.rstrip('/'), manifest)
    for rel, size in manifest:
        print(rel, size)


if __name__ == '__main__':
    main()
