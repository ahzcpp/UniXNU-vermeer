#!/usr/bin/env python3
"""llvm-objcopy stand-in for the UniXNU "late initializer" step on Linux.

The archive builders run `llvm-objcopy --remove-section=__DATA,__mod_init_func --globalize-symbol=SYM IN OUT`,
which LLVM 18's Mach-O objcopy refuses. That exact form is done with the K70 project's own
display-next/reference/coachz_late_init.stage_initializer (already used by the r-series kernels for
NativeKeyStoreEvents): the initializer section stops being a mod-init section and SYM becomes external.
Any other invocation goes to the real llvm-objcopy-18.
"""
import os, sys
from pathlib import Path

REAL = '/usr/lib/llvm-18/bin/llvm-objcopy'
args = sys.argv[1:]
remove = [a for a in args if a.startswith('--remove-section=')]
glob = [a for a in args if a.startswith('--globalize-symbol=')]
files = [a for a in args if not a.startswith('-')]
if remove == ['--remove-section=__DATA,__mod_init_func'] and len(glob) == 1 and len(files) == 2 and len(args) == 4:
    sys.path.insert(0, os.environ.get('K70_DISPLAY_REFERENCE', '/mnt/e/K70-UniXNU-20261104/display-next/reference'))
    from coachz_late_init import stage_initializer
    name = glob[0].split('=', 1)[1]
    src, dst = Path(files[0]), Path(files[1])
    dst.write_bytes(stage_initializer(src.read_bytes(), name))
    sys.exit(0)
os.execv(REAL, [REAL] + args)
