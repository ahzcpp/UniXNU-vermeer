<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="assets/logo-light.svg">
    <img alt="UniXNU" src="assets/logo-light.svg" width="200">
  </picture>
</p>

<p align="center">
  <a href="https://unixnu.com"><img alt="Documentation" src="https://img.shields.io/badge/docs-unixnu.com-2A5BD7?logo=readthedocs&logoColor=white"></a>
  <a href="https://t.me/unixnu"><img alt="Telegram" src="https://img.shields.io/badge/Telegram-@unixnu-26A5E4?logo=telegram&logoColor=white"></a>
  <a href="https://x.com/unixnu"><img alt="X" src="https://img.shields.io/badge/X-@unixnu-000000?logo=x&logoColor=white"></a>
</p>

# UniXNU-vermeer: clean research branch

Fresh source bring-up for the Redmi K70 (`vermeer`, SM8550), started on
2026-11-04. The first OS target is iPadOS 17.7.11 (21H461), iPad7,1.

Start with these records for the current phone and build:

| Record | Contents |
| --- | --- |
| [Clean source build](docs/clean-start-2026-11-04.md) | Fixed source versions, build commands, verified inputs and remaining work |
| [Current loader handoff](docs/handoff-trace-2026-11-04.md) | Verified images, actual screen evidence and restoration state |
| [Heap-only entry experiment](docs/boot04-2026-11-04.md) | boot04 reached XNU/BSD initialization; root device mount failed |
| [Measured RAM experiment](docs/boot05-memory-2026-11-04.md) | boot05r1 parsed 8 RAM banks but found no eligible 514 MiB contiguous GCD region |
| [Actual RAM ranges](docs/boot05r2-memory-2026-11-04.md) | boot05r2 exposed dynamic allocation constraints incorrectly treated as occupied RAM |
| [Dynamic request correction](docs/boot05r3-memory-2026-11-04.md) | boot05r3 reached XNU/BSD through its mandatory 512 MiB window; root device remains absent in this baseline |
| [Handoff code writeback](docs/boot05r4-handoff-2026-11-04.md) | r4 reaches root/SystemOS/Data and launchd/SpringBoard exec; final watchdogd SIGTRAP, desktop unverified |
| [iPadOS disk candidate](docs/system-candidate-2026-11-04.md) | Root/storage writes all OKAY; r4 now reaches user-space services, with a watchdogd SIGTRAP under investigation |
| [Verified Data seed](system-next/data-seed-status-public-2026-11-04.txt) | Successful macOS build and independent read-only APFS ownership checks |
| [UEFI USB collector](scripts/capture-k70-uefi.ps1) | Bounded read-only PC collector; real USB transfer is not yet verified |
| [UEFI USB diagnostics](docs/uefi-usb-diagnostics-2026-11-04.md) | Source design, verified OEM inputs, build checks and transport limits |

Other documents are retained as background. Their inherited observations,
including blue/magenta probe bars and timed resets after `_arm_vm_init`, have
not been reproduced in this session and are not evidence for the current phone.
The two photographs originally supplied with the request are references; the
latest connected-session photograph is the current screen evidence.

## Current result

Latest r4 hardware photographs confirm root mount/authentication decision,
SystemOS/Data preparation, and launchd followed by SpringBoard exec signature
acceptance. The run ends at watchdogd PID 134 with OS_REASON_SIGNAL / SIGTRAP,
with about 2.37 MiB free memory. The graphical desktop is not yet observed.
See [the r4 evidence](docs/boot05r4-handoff-2026-11-04.md); historical stages below
remain recorded for reproducibility.

The phone is `vermeer`, hardware revision `20000`, slot `b`, unlocked. The
supplied original `vendor_boot_b` remains restored. Boot04 in `boot_b` reached
XNU/BSD initialization; sending, writing and reboot reported `OKAY`. The user's
two boot04 photographs show
`159 bsd_init: calling setconf?`, followed by
`failed to mount root device @IOKitBSDInit.cpp:984`. This is the first confirmed
XNU execution on this phone in this session. Root mount and userland remain
unverified; that diagnostic kernel disables native UFS/root publication.

The subsequent boot05 measured-RAM experiment was also written to `boot_b`
successfully. Its actual handset photograph shows the entry returning
`Compromised Data` to the UniXNU menu. The menu hides the detailed rejection
reason. The display-only boot05r1 follow-up has now been flashed successfully;
its held failure screen reports `M05 no measured 514MiB contiguous GCD gap`,
with `META 0 BANKS 8 RESV 52 DYN 0`. The real runtime FDT parses successfully;
the planner eligibility conditions stopped that image before new RAM publication
or XNU entry. Boot05r2 then displayed 55 GCD descriptors, 21 unowned gaps and zero
qualified pieces: the parser incorrectly accumulated `alloc-ranges` placement
constraints as occupied reservations, excluding the entire candidate address
space. This is a loader parser error, not evidence of insufficient physical RAM.
Boot05r3 corrects the known, uninstantiated Linux dynamic requests while preserving
fixed reservations, existing firmware ownership and all four placeholder HOBs.
Its compile, serial FDS, full FD audit and image unpacking passed. The r3 handset
photographs now show XNU/BSD through `setconf`, then the expected missing-root
device panic. The screen reports `pages=0x1b83c`, `free=0x1aec1` (about 430.75 MiB
free). Combined with the frozen loader's mandatory 512 MiB allocation and no
fallback, this qualifies the larger window for entry; it does not establish a
full RAM stress test or show the arena physical address. Boot04 remains an older
XNU/BSD rollback image. The subsequent full-system test has now replaced userdata
with the verified OS/SystemOS image and boot_b with the matching storage-enabled
candidate: all 17 sparse chunks, boot_b and reboot reported OKAY. The first
full-payload photo retains the green top and yellow cache-maintenance band,
without a visible cyan/blue band or XNU log. The handoff is under investigation;
this was the r3 full-payload result, subsequently superseded by r4 below.
The loader's old `NO OS ROOT` text is a stale diagnostic label, not proof that
the newly written root/payload is absent. The r4 boot-only follow-up adds explicit
handoff-code cache writeback, identity checks and finer transition markers; it has
been flashed and rebooted with OKAY, preserving userdata. Its subsequent screen result reaches root/SystemOS/Data preparation, launchd and SpringBoard exec; watchdogd then exits with SIGTRAP. Desktop display remains unverified.

The trace01 photograph shows the green handoff bar and yellow/cyan checkpoints:
the loader completed cache maintenance and its MMU-off sequence. Trace02's
fixed-address entry probe still produced no blue bar. Trace03's 256 MiB
allocation control instead reached a UEFI synchronous external read abort
(`ESR=0x96000010`, `FAR=0xE687C000`). None of these establishes XNU entry,
an Apple logo, userland, or a working operating system on this phone. The later
boot04 result above supersedes the earlier lack of evidence for XNU execution.

A separate minimal Linux reader was tested, did not enumerate USB, and stayed
at the Redmi/Xiaomi logo. It was then removed by the restoration above. At the
user's request, the next logging experiment targeted USB inside UniXNU/UEFI. The PC
collector passes offline mocks. The final03 complete firmware and Android
boot image have passed build, module/source audits and independent unpacking;
the image is 1,699,840 bytes. After flashing, a 61.9-second watch with 15
successful bounded host inventory probes found no diagnostic identity, and
no HELLO/log transfer occurred. The user then paused USB work and requested
continued XNU/system bring-up. Boot-services logging does not imply a live
USB channel after XNU takes control.

The non-USB entry experiment boot04 passed a complete EDK build,
FD/source/data audits and Android boot unpacking. It restricts allocations to
the stock DXE Heap, uses an explicit 128 MiB entry window and rejects unsafe
FDT/initrd overlap. Its trace02 kernel bytes are unchanged. The complete image
is 68,657,152 bytes, SHA-256
`9912e2e7980d64bf32137ee00682b6463036ef0dc95c473081ced135a9da2302`.
It has now reached XNU/BSD initialization on the handset. This small window is
an entry diagnostic, not a full-system memory configuration. Its observed free
`0x3736` pages are about 55.2 MiB; the current storage implementation alone
needs about 69.9 MiB for the two authenticated block-hash tables.

All three host jobs succeeded in [CI 41680341472](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41680341472)
and [CI 41681910792](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41681910792).
Original OS/SystemOS inputs match the board's size and SHA-256 pins. A
9,382,658,048-byte offline SHNGROOT two-window candidate has now passed full
independent readback, SHA-256, GPT CRC and APFS container metadata checks.
That two-window image remains unchanged. A separate first-install Data seed
has now been created and corrected in [CI 41751537169](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41751537169),
preserving its original volume identity. The 1,405,072-byte packed seed has
3 GiB logical capacity. All 221 active inode owners/modes were independently
read back, including 206 source-template entries; file contents and symlink
targets were checked after a read-only remount. This does not establish a
Data mount or userland boot on the phone.

Both OS/SystemOS authentication policies have been built and checked against
all 2,290,185 source blocks. A separate storage-enabled native kernel has
linked, and its final original-storage code, 16 bindings and 190 method slots
passed the final binary checks. The five-file FAT payload has been integrated
and read back, including both complete authentication tables and the corrected
packed Data seed. A full storage boot containing the r3 firmware is also packaged
and independently unpacked; the matching firmware's RAM512 entry passed with the
baseline kernel, while this full kernel, UFS I/O and root/Data mounts still require
their own hardware test. The verified root candidate has now replaced the prior
modified userdata; that earlier content was not inspected. Fastboot acknowledged
all writes, but this is not a device readback or a successful root mount claim.
Storage, userland and device drivers remain work to verify. The handoff record
retains image hashes and exact displayed addresses. A high-RAM, XPU or capsule
failure has not been proved.

## Source baseline and build

| Source | Baseline revision |
| --- | --- |
| ahzcpp/UniXNU-vermeer | `f021f82a95e2a2f85552c7c73999c0c43f0cd78f` |
| UniXNU/UniXNU | `c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5` |
| yyx2025/uefi-vermeer | `ae57e93f0eb6b556f1bb964ffb12f0e78aaaf33c` |

Use this branch's **Clean vermeer kernel baseline** workflow or the commands in
the clean source build record. Linux requires the official patched XNU
toolchain. A Mach-O `kernel.macho` alone is not an Android boot image. Original
Apple inputs and private device identifiers stay outside Git.

The experimental UEFI diagnostics are supplied as a source-only
[patch](uefi-usb-diagnostics.patch) with a [SHA manifest](uefi-usb-diagnostics.manifest.json).
From this repository's root, check and apply it with `git apply --check
uefi-usb-diagnostics.patch` followed by `git apply uefi-usb-diagnostics.patch`.
It creates `package/uefi/K70DiagnosticsPkg` and the PC test source. After
applying, read that package's `Tools/README.md` and
`Tools/README-build-firmware.md` to prepare the exact OEM inputs and build the
separate diagnostic profile. The patch includes no OEM or Apple binaries.
The baseline workflow does not automatically apply or build this experiment.

The separate non-USB entry and system-disk tools are supplied in
[boot04-system-source.patch](boot04-system-source.patch) with its
[manifest](boot04-system-source.manifest.json). Check/apply this patch from
the repository root; it creates 46 text files under `research/boot-chain-04`
and `research/system-build`. Read their README files for pinned workspace
dependencies and offline checks. It includes source, tests and audit helpers,
without Apple/OEM binaries. It does not change the baseline CI configuration.

Official references: [documentation](https://unixnu.com),
[device page](https://unixnu.com/devices/vermeer-ahzcpp),
[porting](https://unixnu.com/porting/),
[building](https://unixnu.com/building),
[bring-up order](https://unixnu.com/zh/porting/bring-up).
Core: [UniXNU](https://github.com/UniXNU/UniXNU).
Firmware: [uefi-vermeer](https://github.com/yyx2025/uefi-vermeer).

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE).
