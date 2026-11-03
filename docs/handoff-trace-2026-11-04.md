# K70 loader handoff record, 2026-11-04

This record covers the newly connected Redmi K70 session. The two photographs
provided at the beginning of the request are references and are excluded from
this device's evidence. The photographed green-only result below belongs to
the first tested complete image. The subsequent `trace01` write is recorded
separately and is awaiting screen feedback.

## First complete image: photographed handoff

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
| Screen evidence for this image | Green DarwinEnter bar; no blue or magenta entry-probe bars |

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

## Diagnostic image trace01: flashed, awaiting screen

`trace01` has completed its host build and independent packaging checks. The
single phone's `vermeer` identity, hardware revision `20000`, slot `b`, unlocked
state and boot partition capacity were checked again before the write.
Fastboot reported `OKAY` for sending the image, writing `boot_b` and rebooting.
Screen feedback for this image is pending.

| Trace01 input | Recorded value |
| --- | --- |
| Complete Android boot image | v2 wrapper with diagnostic DarwinBoot and XNU payload |
| Image bytes | 68,657,152 |
| Image SHA-256 | `a0b07325090e1438f2381f8024739e46d9db404cb00f8da55201d0e2399f3ed2` |
| Firmware FD SHA-256 | `450165988e2804e6ce6597a29dc7d9b66cadf8d6d582406502cb543b8ab0357c` |
| Device write | Sending / writing `boot_b` / reboot each returned `OKAY` |
| Screen observation | Awaiting user feedback |

The independently compiled Entry code matched the 260-byte code in the
DarwinBoot FFS extracted from the new FD's LZMA-compressed contents. Unpacking
the complete boot image also reproduced the expected FD and FAT payload.
These checks establish the packaged code and bytes; the successful write and
reboot commands do not establish that any new trace stage has executed.

| New trace output | Meaning if observed in this image's screen feedback |
| --- | --- |
| Bottom band 13, yellow | Loader cache-maintenance stage completed |
| Bottom band 14, cyan | Loader MMU-off stage completed |
| Centre address display | PA, ENTRY, ARGS, loader and framebuffer addresses for comparison with the image and memory map |

These bands describe loader progress. They are separate from the blue and
magenta XNU entry-probe bars. No trace01 band, central address output or XNU
execution has yet been confirmed. Record which bands and PA/ENTRY/ARGS fields
appear before updating the confirmed stage.

The present evidence leaves the failure after the green handoff stage
unresolved. Confirming a capsule, lock or memory issue as the cause would
require additional observations; none is established here. The pending screen
feedback should capture the complete trace and addresses before another input
is changed.

References: [official bring-up order](https://unixnu.com/zh/porting/bring-up),
[loader entry diagnostics](https://unixnu.com/zh/guide/debugging),
[clean source build record](clean-start-2026-11-04.md).
