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

# UniXNU-vermeer

Device repository for the Redmi K70 (vermeer, SM8550). It holds the board file, kernel defconfig, device scripts, drivers and package.

- [Documentation](https://unixnu.com)
- [Device page](https://unixnu.com/devices/vermeer-ahzcpp)
- [Porting guide](https://unixnu.com/porting/)
- [Build](https://unixnu.com/building)

Firmware: [uefi-vermeer](https://github.com/yyx2025/uefi-vermeer).

Build with the `unixnu` driver of [UniXNU](https://github.com/UniXNU/UniXNU). Clone both repositories with `--recurse-submodules`; `unixnu fetch` also updates the module submodules.

## Documents in this repository

Read them in this order; each says where its claims come from, and marks what has not been checked against the sources here.

| Document | What it is for |
| --- | --- |
| [docs/build-and-flash.md](docs/build-and-flash.md) | How to do it: environment, every build command, flashing, and the full operation for the current two steps |
| [docs/playbook.md](docs/playbook.md) | What to do: stage position, screen-outcome tables, log channels by reachability, and the ruled-out list |
| [docs/bring-up.md](docs/bring-up.md) | Why: the reasoning and its evidence |
| [docs/hardware-facts.md](docs/hardware-facts.md) | Measured values from this phone's own stock firmware data, re-derivable with `scripts/read-stock-layout.py` |
| [docs/lab-crossref.md](docs/lab-crossref.md) | How to use `ios-vermeer-lab`: it is not UniXNU, so what transfers and what cannot |

## Bring-up state

Stages are the ones in the [bring-up order](https://unixnu.com/zh/porting/bring-up). Nothing below is a claim about operating-system features: no feature of the OS has been observed on this device yet.

| Stage | State | Observed |
| --- | --- | --- |
| 1 UEFI | ✅ | Firmware starts from a flashed boot image; the panel is stable |
| 2 UniXNU | ✅ | The menu appears and the configuration parses |
| 3 Payload | ✅ | `unixnu.toml` and the payload files are read |
| 4 DarwinBoot | ✅ | The green handoff bar appears |
| 5 Kernel entry | ⚠️ | The blue and magenta probe bars appear and a timed reset after `_arm_vm_init` fires, so the kernel runs past the MMU enable. The screen then stops with no text, and the panic screen cannot print at that point, so the stop is not yet attributed. See [docs/bring-up.md](docs/bring-up.md) |
| 6 Storage and root volume | ❌ | Not started. The `[userdata]` geometry is still the reference board's placeholder |
| 7 Userland | ❌ | Not started. The Apple logo belongs to this stage |

✅ reached, ⚠️ in progress, ❌ not started.

The firmware manifest, the userdata geometry and the shared SoC GMU base are still the reference board's values; each one is recorded with its correct source in [docs/hardware-facts.md](docs/hardware-facts.md) and left unchanged until its stage.

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE).
