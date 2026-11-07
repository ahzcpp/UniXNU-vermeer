#!/usr/bin/env python3
"""Run a command in the GPU overlay tree with the same Linux toolchain environment as the K70 rNN kernel builds.

Mirrors usb-next/build-r31-kernel.py: UNIXNU_TOOLCHAIN=<work>/toolchain-view and unixnu.kernel.linux_env() of
the vermeer kernel context (xcrun shim, fakeroot SDK, arm64-apple-ios17.0). Never contacts a phone.
Usage: k70env.py COMMAND [ARGS...]   (cwd: /root/k70-gpu-work/tree)
"""
import os, subprocess, sys
from pathlib import Path

ROOT = Path('/root/k70-clean-20261104')
WORK = ROOT / 'storage-candidate-20261104'
CORE = ROOT / 'UniXNU'
TREE = Path(os.environ.get('K70_GPU_TREE', '/root/k70-gpu-work/tree'))
os.environ['UNIXNU_TOOLCHAIN'] = str(WORK / 'toolchain-view')
sys.path.insert(0, str(CORE / 'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import kernel

ctx = Context(parser().parse_args(['build', 'kernel', '-D', str(WORK / 'device'), '--out', str(WORK / 'out'),
                                   '--kernelcache', str(ROOT / 'out/ipsw/21H461/kernelcache.decompressed'), '--jobs', '4']))
env = dict(os.environ, **kernel.linux_env(ctx))
env['UNIXNU_XNU_ARCH'], env['UNIXNU_IOS_VERSION'] = 'arm64', '17.0'
# `brew --prefix llvm` stand-in (see brew-shim.sh), installed by setup-gpu-tree.sh.
env['PATH'] = str(TREE.parent / 'bin') + os.pathsep + env['PATH']
sys.exit(subprocess.call(sys.argv[1:], cwd=TREE, env=env))
