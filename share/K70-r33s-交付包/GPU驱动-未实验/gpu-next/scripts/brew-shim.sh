#!/bin/sh
# Linux stand-in for the one Homebrew query the UniXNU archive builders make: `brew --prefix llvm`
# (they then run <prefix>/bin/llvm-objcopy). Anything else is refused.
if [ "$1" = "--prefix" ] && [ "$2" = "llvm" ]; then
    # <work>/llvm/bin holds llvm-objcopy-shim.py as llvm-objcopy (late-initializer staging), see setup-gpu-tree.sh.
    echo "$(cd "$(dirname "$0")/.." && pwd)/llvm"
    exit 0
fi
echo "brew shim: unsupported: $*" >&2
exit 1
