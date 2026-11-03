# K70 clean source build, 2026-11-04

The user's K70 now boots the newly built UniXNU menu and shows the new XNU
entry. The latest photograph from this connected session shows the green
DarwinEnter handoff bar, with no blue or magenta XNU entry-probe bars. This
establishes the loader handoff stage, not XNU execution or an OS boot.

The two photographs supplied at the start remain references, not observations
of this phone. Earlier workspace notes and binary images are not evidence for
this installation. See [the current handoff record](handoff-trace-2026-11-04.md)
for the new device evidence and the next diagnostic build.

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
- Userdata GPT geometry, the active DTBO selection and the runtime memory map
  have not been established in this run. Stock dtbo overlay 54 identifies an
  SM8550 vermeer board and has been parsed and combined offline only. That is
  not proof of the overlay selected by the running phone. The old userdata
  LBAs must not be used to generate a flash image.
- The UEFI source includes a high DRAM bank borrowed from sheng and explicitly
  calls it unverified for K70. Physical testing must validate the memory map.
- Original Apple OS and SystemOS inputs have been downloaded and their sizes
  and SHA-256 values match the board pins. No root/userdata image has been
  created or written. Writable Data state, matching storage ABI and verified
  root policies still need integration before launchd and SpringBoard can work.
- Touch/display/GPU support on sheng does not prove support for K70 hardware.

The source defaults no longer request `rd=md0` without an input ramdisk or a
six-second diagnostic reset. This removes misleading diagnostic behavior;
it does not supply a root filesystem or prove that XNU executes on this phone.

## Device session

The connected session identified one fastboot device with `product: vermeer`,
hardware revision `20000`, current slot `b`, and `unlocked: yes`. Serial numbers
and the private platform seed are deliberately omitted from this record.

The complete v2 boot image was independently checked before it was flashed to
`boot_b`, and the device was rebooted. Its size is 68,657,152 bytes and its
SHA-256 is
`ad2660d1c0c766146f55b1a68bf705050fa1c6af48e37c4f6aecec72dbc44b58`.
The user confirmed the new UniXNU menu and XNU item. The subsequent green
DarwinEnter bar is the last confirmed stage. No userdata write was performed.
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

Those earlier UEFI-only wrappers have no XNU payload and no OS. The connected
test instead used the complete v2 image identified above. The stock dtbo
overlay 54 has subsequently been inspected offline; the synthesized tree has
not been flashed. A runtime memory map is still needed.

The workflow builds the diagnostic XNU baseline and firmware independently,
then packs an Android boot image only if both jobs succeed. All three jobs
succeeded in [run 41680341472](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41680341472)
and [run 41681910792](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41681910792).
These are host build results. The phone test separately confirms the menu and
green loader handoff bar; neither result establishes XNU or userland execution.

The next `trace01` diagnostic image is being built. It adds loader-stage bands
and address output described in the handoff record. It has not been recorded
as flashed or successful.
