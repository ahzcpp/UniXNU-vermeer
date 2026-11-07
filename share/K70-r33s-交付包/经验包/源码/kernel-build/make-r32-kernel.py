#!/usr/bin/env python3
"""Derive build-r32-kernel.py from build-r31-kernel.py: host touch processing for vermeer's GT9916.

r32s proved avdd on (RPMh reads 3200 mV) and the analog front end scanning (raw frames ~10000 counts), yet the
firmware never posts a coordinate event: vermeer's firmware leaves coordinates to Android's THP stack. touch/k70_touch.c
now reads firmware-baselined difference frames (0x90/0x82) and computes contacts itself (peaks, 3x3 centroid,
slot tracking), as sheng does for its Novatek panel. No script change besides the names.
"""
from pathlib import Path
HERE=Path(__file__).resolve().parent
s=(HERE/'build-r31-kernel.py').read_text()
def sub(old,new,count=1):
    global s
    assert s.count(old)==count,(old[:90],s.count(old));s=s.replace(old,new)
sub('"""r31 kernel: r30 + GT9916 raw-frame self test, avdd readback (derived by make-r31-kernel.py).',
    '"""r32 kernel: r31 + host touch processing from GT9916 difference frames (derived by make-r32-kernel.py).')
sub("OUT=WORK/'r31-selftest-20261107';COPY=OUT/'xnu-work-r31'","OUT=WORK/'r32-thp-20261107';COPY=OUT/'xnu-work-r32'")
sub("'schema':'k70-r31-selftest-kernel/1'","'schema':'k70-r32-thp-kernel/1'")
sub("(HERE/'r31-kernel-result.json')","(HERE/'r32-kernel-result.json')")
sub("HERE/'r31-final-storage-verification.json'","HERE/'r32-final-storage-verification.json'",2)
sub("native=OUT/'mach.r31-selftest.unstripped.noctf'","native=OUT/'mach.r32-thp.unstripped.noctf'")
for a,b in (("'compile-observer-r31'","'compile-observer-r32'"),("'archive-observer-r31'","'archive-observer-r32'"),
            ("'link-r31-native'","'link-r32-native'"),("'prelink-r31'","'prelink-r32'"),("libK70GopDisplay.r31.a","libK70GopDisplay.r32.a")):
    sub(a,b)
(HERE/'build-r32-kernel.py').write_text(s)
print(HERE/'build-r32-kernel.py')
