#!/usr/bin/env python3
"""Audit a real signed own Mach-O; never execute it, contact devices or sign files."""
import hashlib
from pathlib import Path
import plistlib
import struct


def require(ok, text):
    if not ok:
        raise ValueError(text)


def u32(b, off, endian='<'):
    require(0 <= off <= len(b)-4, 'u32 bounds')
    return struct.unpack_from(endian+'I', b, off)[0]


def verify_binary(path, expected_entitlements):
    b = Path(path).read_bytes()
    require(len(b) >= 32 and u32(b, 0) == 0xfeedfacf, 'thin Mach-O64 required')
    require(u32(b, 4) == 0x0100000c and u32(b, 8) == 0 and u32(b, 12) == 2,
            'native arm64 executable required, no arm64e/simulator/host substitute')
    ncmds, total = u32(b, 16), u32(b, 20)
    require(ncmds <= 256 and total <= len(b)-32, 'load command bounds')
    off = 32; signature = None; deployment = None; dylibs = []
    for _ in range(ncmds):
        command, size = u32(b, off), u32(b, off+4)
        require(size >= 8 and size <= 32+total-off, 'load command size')
        if command == 0x32:  # LC_BUILD_VERSION
            require(size >= 24 and deployment is None, 'deployment command')
            deployment = (u32(b, off+8), u32(b, off+12), u32(b, off+16))
        if command == 0x1d:  # LC_CODE_SIGNATURE
            require(size == 16 and signature is None, 'signature command')
            signature = (u32(b, off+8), u32(b, off+12))
        if command in (0xc, 0x80000018, 0x8000001f):
            name_offset = u32(b, off+8)
            require(24 <= name_offset < size, 'dylib command')
            dylibs.append(b[off+name_offset:off+size].split(b'\0')[0].decode())
        off += size
    require(off == 32+total, 'load command end')
    require(deployment and deployment[:2] == (2, (16 << 16) | (7 << 8)),
            'LC_BUILD_VERSION must be iOS/minOS16.7')
    require(signature is not None, 'missing native code signature')
    sigoff, sigsize = signature
    require(sigoff <= len(b) and sigsize <= len(b)-sigoff, 'signature bounds')
    sb = b[sigoff:sigoff+sigsize]
    require(u32(sb, 0, '>') == 0xfade0cc0 and u32(sb, 4, '>') <= len(sb), 'signature superblob')
    limit = u32(sb, 4, '>'); count = u32(sb, 8, '>')
    require(count <= 64 and 12+8*count <= limit, 'signature index')
    blobs = {}
    for i in range(count):
        slot, at = u32(sb, 12+8*i, '>'), u32(sb, 16+8*i, '>')
        require(at <= limit-8, 'signature blob header')
        size = u32(sb, at+4, '>')
        require(size >= 8 and size <= limit-at and slot not in blobs, 'signature blob bounds/duplicate')
        blobs[slot] = sb[at:at+size]
    require(5 in blobs and u32(blobs[5], 0, '>') == 0xfade7171, 'embedded XML entitlements required')
    actual = plistlib.loads(blobs[5][8:])
    require(actual == expected_entitlements, 'entitlements differ from unchanged permission baseline')
    directories = []
    for slot, cd in blobs.items():
        if slot != 0 and not 0x1000 <= slot <= 0x1005:
            continue
        require(len(cd) >= 44 and u32(cd, 0, '>') == 0xfade0c02, 'CodeDirectory')
        version, flags = u32(cd, 8, '>'), u32(cd, 12, '>')
        hash_offset, identifier_offset = u32(cd, 16, '>'), u32(cd, 20, '>')
        special, nslots, code_limit = u32(cd, 24, '>'), u32(cd, 28, '>'), u32(cd, 32, '>')
        hashsize, hashtype, page_exp = cd[36], cd[37], cd[39]
        require((hashtype, hashsize) in ((2, 32), (3, 20)), 'SHA256 CodeDirectory required')
        require(page_exp == 12 and flags & 2, 'ad-hoc signature with 4K pages required')
        if version >= 0x20300:
            require(len(cd) >= 64, '64-bit CodeDirectory bounds')
            code_limit_64 = struct.unpack_from('>Q', cd, 56)[0]
            if code_limit_64:
                code_limit = code_limit_64
        require(code_limit == sigoff and nslots == (code_limit+4095)//4096, 'code slot coverage')
        require(hash_offset <= len(cd) and nslots*hashsize <= len(cd)-hash_offset,
                'code hash table bounds')
        require(identifier_offset < len(cd), 'identifier bounds')
        identifier = cd[identifier_offset:].split(b'\0')[0].decode()
        require(identifier == 'org.unixnu.wifi.supplicant', 'signature identifier')
        for i in range(nslots):
            page = b[4096*i:min(4096*(i+1), code_limit)]
            require(hashlib.sha256(page).digest()[:hashsize] ==
                    cd[hash_offset+i*hashsize:hash_offset+(i+1)*hashsize], 'actual code page digest mismatch')
        require(special >= 5 and hash_offset >= special*hashsize, 'special hash slot bounds')
        for number in (5, 7):
            if number in blobs:
                require(special >= number, 'missing entitlement special slot')
                require(hashlib.sha256(blobs[number]).digest()[:hashsize] ==
                        cd[hash_offset-number*hashsize:hash_offset-(number-1)*hashsize],
                        'entitlement special slot hash mismatch')
        directories.append(dict(slot=slot, page_bytes=4096, verified_pages=nslots,
                                cdhash=hashlib.sha256(cd).digest()[:20].hex()))
    require(directories and directories[0]['slot'] == 0, 'primary CodeDirectory absent')
    return dict(sha256=hashlib.sha256(b).hexdigest(), bytes=len(b), architecture='arm64',
                minimum_os='16.7', platform='iOS', sdk_encoded=deployment[2], dylibs=dylibs,
                entitlements=actual, code_directories=directories, hardware_verified=False)
