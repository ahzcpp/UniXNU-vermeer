# K70 loader handoff record, 2026-11-04

This record covers the newly connected Redmi K70 session. The two photographs
provided at the beginning of the request are references and are excluded from
this device's evidence. Only the latest photograph from this session supports
the current screen observation.

## Device and tested image

One fastboot device was identified as `vermeer`, hardware revision `20000`,
current slot `b`, with the bootloader reporting `unlocked: yes`. This public
record contains no serial number or platform seed.

| Input | Recorded value |
| --- | --- |
| Complete Android boot image | v2 wrapper with UniXNU and XNU payload |
| Image bytes | 68,657,152 |
| Image SHA-256 | `ad2660d1c0c766146f55b1a68bf705050fa1c6af48e37c4f6aecec72dbc44b58` |
| Device operation | Successfully flashed `boot_b`, then rebooted |
| User confirmation | Newly built menu and new XNU entry visible |
| Latest screen | Green DarwinEnter bar; no blue or magenta entry-probe bars |

The image was checked before the write. The green bar is drawn by the loader
at the DarwinEnter handoff. It does not establish that the XNU entry point ran.
No XNU log, XNU probe bar, mounted root, launchd or SpringBoard result has been
observed in this session.

## Host and input checks

All three jobs succeeded in [CI run 41680341472](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41680341472)
and [CI run 41681910792](https://github.com/ahzcpp/UniXNU-vermeer/actions/runs/41681910792).
CI success demonstrates the recorded build and packaging checks, separately
from the phone observations above.

Original Apple restore members for iPad7,1 / iPadOS 17.7.11 (21H461) were
downloaded and verified against the board pins:

| Component | Member | Bytes | SHA-256 |
| --- | --- | --- | --- |
| OS | `044-76040-038.dmg` | 5,157,406,069 | `3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d` |
| Cryptex1,SystemOS | `044-76034-038.dmg` | 3,303,014,400 | `b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f` |

These are original system inputs. No root/userdata image was created or written.
They remain outside Git. The inherited userdata geometry is not a measurement
of the connected phone.

Stock dtbo overlay 54 identifies SM8550 vermeer. Its offline parsing and tree
composition are preparation only: that synthesized tree has not been flashed,
and the active runtime overlay has not been proven.

## Next diagnostic build: trace01

`trace01` is being built to distinguish loader stages after the existing green
bar. It is not recorded as flashed or successfully booted.

| New trace output | Meaning if observed in a future test |
| --- | --- |
| Bottom band 13, yellow | Loader cache-maintenance stage completed |
| Bottom band 14, cyan | Loader MMU-off stage completed |
| Centre address display | Physical base, entry, boot-arguments, loader and framebuffer addresses for comparison with the image and memory map |

These bands describe loader progress. They are separate from the blue and
magenta XNU entry-probe bars. A new test must record its exact image hash and
which bands appear before updating the confirmed stage.

The present evidence leaves the failure after the green handoff stage
unresolved. Confirming a capsule, lock or memory issue as the cause would
require additional observations; none is established here. The next run should
capture the complete trace and addresses, then change one input at a time.

References: [official bring-up order](https://unixnu.com/zh/porting/bring-up),
[loader entry diagnostics](https://unixnu.com/zh/guide/debugging),
[clean source build record](clean-start-2026-11-04.md).
