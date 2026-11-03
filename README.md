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
| [UEFI USB collector](scripts/capture-k70-uefi.ps1) | Bounded read-only PC collector; real USB transfer is not yet verified |
| [UEFI USB diagnostics](docs/uefi-usb-diagnostics-2026-11-04.md) | Source design, verified OEM inputs, build checks and transport limits |

Other documents are retained as background. Their inherited observations,
including blue/magenta probe bars and timed resets after `_arm_vm_init`, have
not been reproduced in this session and are not evidence for the current phone.
The two photographs originally supplied with the request are references; the
latest connected-session photograph is the current screen evidence.

## Current result

The phone is `vermeer`, hardware revision `20000`, slot `b`, unlocked. It has
been restored to the supplied original `vendor_boot_b` and the known trace02
UniXNU `boot_b`. Both writes and reboot reported `OKAY`; the user confirmed
the UniXNU menu or handoff screen returned.

The trace01 photograph shows the green handoff bar and yellow/cyan checkpoints:
the loader completed cache maintenance and its MMU-off sequence. Trace02's
fixed-address entry probe still produced no blue bar. Trace03's 256 MiB
allocation control instead reached a UEFI synchronous external read abort
(`ESR=0x96000010`, `FAR=0xE687C000`). None of these establishes XNU entry,
an Apple logo, userland, or a working operating system on this phone.

A separate minimal Linux reader was tested, did not enumerate USB, and stayed
at the Redmi/Xiaomi logo. It was then removed by the restoration above. At the
user's request, current logging work targets USB inside UniXNU/UEFI. The PC
collector passes offline mocks. The final03 complete firmware and Android
boot image have passed build, module/source audits and independent unpacking;
the image is 1,699,840 bytes. It has not yet been flashed. Real USB
enumeration/log transfer remain unverified. Boot-services logging
does not imply a live USB channel after XNU takes control.

All three host jobs succeeded in [CI 41680341472](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41680341472)
and [CI 41681910792](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41681910792).
Original OS/SystemOS inputs match the board's size and SHA-256 pins. This
session has not created or written a root/userdata image; the user's existing
modified userdata has not been inspected. Storage, userland and device drivers
remain work to verify. The handoff record retains image hashes and exact
displayed addresses. A high-RAM, XPU or capsule failure has not been proved.

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

Official references: [documentation](https://unixnu.com),
[device page](https://unixnu.com/devices/vermeer-ahzcpp),
[porting](https://unixnu.com/porting/),
[building](https://unixnu.com/building),
[bring-up order](https://unixnu.com/zh/porting/bring-up).
Core: [UniXNU](https://github.com/UniXNU/UniXNU).
Firmware: [uefi-vermeer](https://github.com/yyx2025/uefi-vermeer).

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE).
