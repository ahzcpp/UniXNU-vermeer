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
| [Current loader handoff](docs/handoff-trace-2026-11-04.md) | Previous screen evidence, verified `trace01` write and pending screen capture |

Other documents are retained as background. Their inherited observations,
including blue/magenta probe bars and timed resets after `_arm_vm_init`, have
not been reproduced in this session and are not evidence for the current phone.
The two photographs originally supplied with the request are references; the
latest connected-session photograph is the current screen evidence.

## Current result

The connected phone was identified as `vermeer`, hardware revision `20000`,
slot `b`, bootloader unlocked. A checked complete v2 boot image was flashed to
`boot_b` and the phone rebooted. The user confirmed the new UniXNU menu and XNU
entry. That image's screen showed the green DarwinEnter handoff bar, without
blue or magenta XNU entry-probe bars. This remains the last confirmed screen
stage. XNU execution remains unconfirmed.

All three host jobs succeeded in [CI 41680341472](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41680341472)
and [CI 41681910792](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41681910792).
Original OS/SystemOS inputs match the board's size and SHA-256 pins. No root or
userdata image has been created or written. Storage, userland and device-driver
support remain work to verify on this phone.

The complete `trace01` v2 image has now been built, independently checked,
flashed to `boot_b` and rebooted with fastboot reporting `OKAY`. Its size is
68,657,152 bytes and SHA-256 is
`a0b07325090e1438f2381f8024739e46d9db404cb00f8da55201d0e2399f3ed2`.
The phone identity, slot, unlocked state and boot capacity were checked again
before the write. Screen feedback for this image is pending; no new colour
band, address display or XNU execution is claimed. See the handoff record for
the expected yellow/cyan bands and central PA/ENTRY/ARGS fields.

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

Official references: [documentation](https://unixnu.com),
[device page](https://unixnu.com/devices/vermeer-ahzcpp),
[porting](https://unixnu.com/porting/),
[building](https://unixnu.com/building),
[bring-up order](https://unixnu.com/zh/porting/bring-up).
Core: [UniXNU](https://github.com/UniXNU/UniXNU).
Firmware: [uefi-vermeer](https://github.com/yyx2025/uefi-vermeer).

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE).
