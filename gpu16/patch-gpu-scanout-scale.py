#!/usr/bin/env python3
"""Apply bounded nearest-neighbour GPU scanout scaling to the pinned UniXNU tree.

The AIR and the frontend change together. No firmware, addresses, credentials,
original OS code or built binaries are contained in this script.
"""
from pathlib import Path
import hashlib,json,sys

root=Path(sys.argv[1]).resolve()
base=root/'drivers/AdrenoMetal'
def replace(s,old,new,count=1):
    assert s.count(old)==count,(old[:100],s.count(old),count)
    return s.replace(old,new)

ir=base/'shaders/scanout_convert.ll';old_ir=ir.read_text();s=old_ir
s=replace(s,'dereferenceable(16)','dereferenceable(32)',2)
mapping='''  %psw = getelementptr inbounds i32, ptr addrspace(2) %p, i64 4
  %sw = load i32, ptr addrspace(2) %psw, align 16
  %psh = getelementptr inbounds i32, ptr addrspace(2) %p, i64 5
  %sh = load i32, ptr addrspace(2) %psh, align 4
  %pdw = getelementptr inbounds i32, ptr addrspace(2) %p, i64 6
  %dw = load i32, ptr addrspace(2) %pdw, align 8
  %pdh = getelementptr inbounds i32, ptr addrspace(2) %p, i64 7
  %dh = load i32, ptr addrspace(2) %pdh, align 4
  %sxnum = mul i32 %x, %sw
  %synum = mul i32 %y, %sh
  %sx = udiv i32 %sxnum, %dw
  %sy = udiv i32 %synum, %dh
  %sx64 = zext i32 %sx to i64
  %sy64 = zext i32 %sy to i64
'''
s=replace(s,'body:\n  %x64', 'body:\n'+mapping+'  %x64',2)
s=replace(s,'%srow = mul i64 %y64, %ss64','%srow = mul i64 %sy64, %ss64',2)
s=replace(s,'%x2 = shl i64 %x64, 1','%x2 = shl i64 %sx64, 1')
s=replace(s,'%si = add i64 %srow, %x64','%si = add i64 %srow, %sx64')
for n in (15,25):
    line=next(x for x in s.splitlines() if x.startswith('!%u = '%n))
    new=line.replace('!"air.buffer_size", i32 16','!"air.buffer_size", i32 32').replace('!"air.arg_type_size", i32 16','!"air.arg_type_size", i32 32').replace('!"uint4"','!"ScanoutParams"')
    assert new!=line;s=s.replace(line,new)
s=s.replace('; p: width, height, source row pitch in words, destination row pitch in words.','; p[8]: destination width, rows, source pitch words, destination pitch words,\n; source width, source height, full destination width, full destination height.\n; Host validation bounds all dimensions to 16384, so coordinate products fit uint32.')
ir.write_text(s)

p=base/'GpuScanoutPresenter.h';old_h=p.read_text();s=old_h
s=replace(s,'frame.width != scanout_.width || frame.height != scanout_.height || frame.stride < row ||','!validExtent(frame) || frame.stride < row ||',2)
s=replace(s,'gHalfRows.load(std::memory_order_relaxed) ? frame.height / 2 : frame.height','gHalfRows.load(std::memory_order_relaxed) ? scanout_.height / 2 : scanout_.height')
s=replace(s,'const uint32_t params[4] = {frame.width, rows, (uint32_t)(frame.stride / 4), (uint32_t)(scanout_.stride / 4)};',
    'const uint32_t params[8] = {scanout_.width, rows, (uint32_t)(frame.stride / 4), (uint32_t)(scanout_.stride / 4), frame.width, frame.height, scanout_.width, scanout_.height};')
s=replace(s,'MTLSizeMake((frame.width + 15) / 16, (rows + 7) / 8, 1)','MTLSizeMake((scanout_.width + 15) / 16, (rows + 7) / 8, 1)')
# A GPU without our compute kernel can only use the existing blit for equal sizes.
s=replace(s,'        id<MTLBlitCommandEncoder> blit = commands ?',
    '        if (frame.width != scanout_.width || frame.height != scanout_.height) return kNoResource;\n        id<MTLBlitCommandEncoder> blit = commands ?')
s=replace(s,'const uint32_t params[4] = {frame.width, frame.height, (uint32_t)(frame.stride / 4), (uint32_t)(scanout_.stride / 4)};',
    'const uint32_t rows = gHalfRows.load(std::memory_order_relaxed) ? scanout_.height / 2 : scanout_.height;\n        const uint32_t params[8] = {scanout_.width, rows, (uint32_t)(frame.stride / 4), (uint32_t)(scanout_.stride / 4), frame.width, frame.height, scanout_.width, scanout_.height};')
s=replace(s,'MTLSizeMake((frame.width + 15) / 16, (frame.height + 7) / 8, 1)','MTLSizeMake((scanout_.width + 15) / 16, (rows + 7) / 8, 1)')
s=replace(s,'    bool validScanout() const {','''    bool validExtent(const Frame &f) const {
        // Nonzero bounded dimensions prevent division by zero and coordinate overflow in AIR.
        return f.width && f.height && f.width <= 16384 && f.height <= 16384 &&
               f.stride <= UINT32_MAX && f.bytes && f.height <= f.bytes / f.stride;
    }
    bool validScanout() const {''')
s=replace(s,'scanout_.width && scanout_.height && scanout_.stride >=',
    'scanout_.width && scanout_.height && scanout_.width <= 16384 && scanout_.height <= 16384 && scanout_.stride <= UINT32_MAX && scanout_.stride >=')
# validExtent is called before the original stride check; it must guard its own division.
s=replace(s,'f.stride <= UINT32_MAX && f.bytes','f.stride && f.stride <= UINT32_MAX && f.bytes')
p.write_text(s)
report={str(f.relative_to(root)):dict(before=hashlib.sha256(old.encode()).hexdigest(),after=hashlib.sha256(f.read_bytes()).hexdigest()) for f,old in [(ir,old_ir),(p,old_h)]}
(root/'build').mkdir(exist_ok=True)
(root/'build/ios16-scanout-scale.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
