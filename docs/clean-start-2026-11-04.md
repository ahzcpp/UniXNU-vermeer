# K70 clean source build, 2026-11-04

The user's K70 has reached a bootloader menu. The two supplied photographs are
references, not observations of this phone. Earlier workspace notes and binary
images are not evidence for this installation.

The agreed first OS target is iPadOS 17.7.11 (21H461), iPad7,1. This is a new
source build, not a conversion of the stock Linux kernel into XNU.

## Reproducible inputs

| Source | Revision |
| --- | --- |
| Device starting point, ahzcpp/UniXNU-vermeer | f021f82a95e2a2f85552c7c73999c0c43f0cd78f |
| UniXNU/UniXNU | c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5 |
| yyx2025/uefi-vermeer | ae57e93f0eb6b556f1bb964ffb12f0e78aaaf33c |
| SM8550 common | 23b2049fcd9e30356161def84231334fb55ecca9 |
| Apple XNU | d8b80295118ef25ac3a784134bcf95cd8e88109f |

Dependencies are fetched at the commits recorded by these sources. Original
Apple components are fetched from the URL in board.toml; they are not committed
to this repository. The decompressed Apple kernelcache was freshly fetched and
its SHA-256 is `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`.

## Build

Use the **Clean vermeer kernel baseline** workflow on the research branch. It
uses the official Linux toolchain and verifies the patched XNU compiler. A
stock clang18 symlink or the Allenwdk portable-kalloc fallback is not accepted
as an equivalent toolchain. On Linux with the correct toolchain already
installed:

```sh
python3 scripts/build-clean-kernel.py --core /path/to/UniXNU --work /new/empty-parent/k70-build
```

The script creates its work directory and clones the committed device source
into it. It first generates the matching prelink reference without PMGR, then
builds with PMGR. Logs, exit codes, revisions and the kernel hash are retained.
GPU is excluded from this diagnostic baseline, as in the inherited CI profile.

`kernel.macho` is **not** an Android boot image and must never be passed to
`fastboot boot` or `fastboot flash boot`.

## What still prevents claiming a working system

- The inherited defconfig disables the native UFS/root chain. A successfully
  compiled baseline cannot mount an iPadOS installation by itself.
- The user's partition geometry, active DTBO, runtime memory map, panel and
  touch identity have not been read in this run. The old userdata LBAs are not
  measurements of this phone and must not be used to generate a flash image.
- The UEFI source includes a high DRAM bank borrowed from sheng and explicitly
  calls it unverified for K70. Physical testing must validate the memory map.
- Apple OS/SystemOS, writable Data state, matching storage ABI and verified
  root policies must be integrated before launchd and SpringBoard can work.
- Touch/display/GPU support on sheng does not prove support for K70 hardware.

The source defaults no longer request `rd=md0` without an input ramdisk or a
six-second diagnostic reset. This removes misleading diagnostic behavior;
it does not supply a root filesystem or prove that XNU executes on this phone.

## Device session

No USB or partition-writing command has been run. The first connected session
must identify the single fastboot device, confirm `product: vermeer`, record
partition sizes and collect the current boot evidence. Only a complete,
unpacked and hash-checked Android image may be considered for temporary boot.
No userdata image will be made from inherited geometry. Preserve the two
original stock files for recovery.

References: [porting](https://unixnu.com/zh/porting/steps),
[packaging](https://unixnu.com/zh/porting/packaging),
[bring-up](https://unixnu.com/zh/porting/bring-up),
[K70 record](https://unixnu.com/zh/devices/vermeer-ahzcpp).

## Host results in this run

UniXNU package and the pinned vermeer UEFI firmware compiled successfully from
fresh checkouts. Two UEFI-only Android v0 wrappers were generated using the
stock vendor_boot's SM8550 v1 and v2 trees. Both were independently unpacked;
the decompressed firmware matched byte-for-byte, and the old verifier offset
failure was reproduced. The patch is `android-v0-offset.patch` at repository
root and must be applied to the pinned UniXNU build checkout before packaging.

These UEFI-only wrappers have no XNU payload and no OS. They are not the desired
system image, and neither is selected for this phone before its hardware
revision and runtime memory map are established. The existing board DTB is also
not byte-identical to the provided stock firmware's v2 tree.

The new workflow builds the diagnostic XNU baseline and firmware independently,
then packs an Android boot image only if both jobs succeed. The image still has
no OS root; a green workflow is a host build result, not a successful phone boot.
