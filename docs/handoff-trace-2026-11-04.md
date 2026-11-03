# K70 loader handoff record, 2026-11-04

This record covers the newly connected Redmi K70 session. The two photographs
provided at the beginning of the request are references and are excluded from
this device's evidence. The photographed green-only result below belongs to
the first tested complete image. The subsequent trace experiments and the
unsuccessful Linux reader USB attempt are recorded separately below. The user
has now directed further USB work to remain in UniXNU/UEFI; the Linux reader
experiment is discontinued.

## Experiment sequence

| Image | Isolated diagnostic change | Evidence source | Result |
| --- | --- | --- | --- |
| Baseline | Initial complete source build | Session photograph and user menu confirmation | Green handoff bar only |
| trace01 | Loader cache/MMU-off bands and address display | New session photograph | Green, yellow and cyan; no blue/magenta; six addresses recorded below |
| trace02 | Fixed-framebuffer stage-1 entry probe | User's text report; no new photograph supplied | Still green, yellow and cyan; no blue reported; address values not established for this image |
| trace03 | Owned-memory request reduced from 512 to 256 MiB | Independently checked local image; fastboot write/reboot `OKAY` | Awaiting actual colour bands, PA and SIZE |
| reader01 | Stock-GKI Linux reader boot/vendor_boot pair | Fastboot writes `OKAY`; 45-second host observation and user text | No USB transport or snapshot; user reports stop at Redmi/Xiaomi logo |
| Return to UEFI | Restore original vendor_boot_b and previous trace02 boot_b | Restoration completed successfully | Restored UEFI inputs; subsequent USB transport still unverified |

No experiment in this record has confirmed XNU entry. Only trace01 has the
photographed addresses below; they must not be copied into later results.

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

## Diagnostic image trace01: photographed loader stages

`trace01` has completed its host build and independent packaging checks. The
single phone's `vermeer` identity, hardware revision `20000`, slot `b`, unlocked
state and boot partition capacity were checked again before the write.
Fastboot reported `OKAY` for sending the image, writing `boot_b` and rebooting.
The new trace01 photograph shows a green top bar and yellow plus cyan bottom
bands, without blue or magenta XNU entry-probe bars.

| Trace01 input | Recorded value |
| --- | --- |
| Complete Android boot image | v2 wrapper with diagnostic DarwinBoot and XNU payload |
| Image bytes | 68,657,152 |
| Image SHA-256 | `a0b07325090e1438f2381f8024739e46d9db404cb00f8da55201d0e2399f3ed2` |
| Firmware FD SHA-256 | `450165988e2804e6ce6597a29dc7d9b66cadf8d6d582406502cb543b8ab0357c` |
| Device write | Sending / writing `boot_b` / reboot each returned `OKAY` |
| Screen observation | Green top bar; yellow and cyan bottom bands; no blue or magenta |

The independently compiled Entry code matched the 260-byte code in the
DarwinBoot FFS extracted from the new FD's LZMA-compressed contents. Unpacking
the complete boot image also reproduced the expected FD and FAT payload.
These checks establish the packaged code and bytes. The photograph supplies
separate runtime evidence for the loader stages below.

| Observed trace output | Confirmed loader stage |
| --- | --- |
| Bottom band 13, yellow | Cache-maintenance stage completed |
| Bottom band 14, cyan | MMU-off stage completed |
| Centre address display | Values transcribed below |

These bands describe loader progress. They are separate from the blue and
magenta XNU entry-probe bars. Neither XNU entry nor any userland execution is
confirmed by this photograph.

The hexadecimal values displayed in the new photograph are:

| Field | Displayed value |
| --- | --- |
| PA | `0x88007C000` |
| SIZE | `0x20000000` |
| ENTRY | `0x88321E488` |
| ARGS | `0x8840CC000` |
| LOADER | `0xC65CF380` |
| FB | `0xB8000000` |

This observation motivated a probe that paints without first reading the boot
arguments. The later experiments and their separate evidence follow.

## Diagnostic image trace02: fixed-framebuffer entry probe

[patch-entry-fixed-video.py](../scripts/patch-entry-fixed-video.py) guards the
exact trace01 input kernel hash and changes only its stage-1 probe cave. The
new probe is 44 bytes and uses the fixed framebuffer destination directly,
without reading `x0`, the stack or a literal pool. The destination is
`0xb8119400` and the band length is `0x119400`. The existing stage-2 probe is
retained. This tests the dependency on the first boot-arguments read; it does
not provide a driver or root filesystem.

| Trace02 input | Recorded value |
| --- | --- |
| Guarded trace01 kernel SHA-256 | `e86629989123dcedffd2dc764470fd5ddb9c36a36e46ffdde744405688c89530` |
| Fixed-probe kernel SHA-256 | `4559b64843cab506d9d2781172143e13ddb4c546f7b73e3298d5e23ee62906bd` |
| Firmware FD SHA-256 | `450165988e2804e6ce6597a29dc7d9b66cadf8d6d582406502cb543b8ab0357c` |
| Complete v2 boot image bytes | 68,657,152 |
| Boot image SHA-256 | `dea0d7bdcf99f037b8b5407364fc82b7c3bd0c8e83f6cbb93b7aa6771951be2a` |
| Packaged menu title | `K70 trace02 fixed-video entry - NO OS ROOT` |
| Device operation | Identity, revision, slot, unlocked state, capacity and image hash rechecked; sending / writing `boot_b` / reboot each `OKAY` |
| Screen evidence | User text report: still green, yellow and cyan, no blue |

The FD, ADT, boot arguments and 512 MiB owned-memory request were unchanged
from trace01. The central `K70 TRACE01` label is therefore expected in this
image: it belongs to the unchanged FD, while the menu title identifies the
trace02 payload.

There is no new trace02 photograph. Its PA, SIZE and other displayed addresses
must not be assumed to match trace01. The text report does not confirm XNU
entry, and the missing blue band does not establish a specific memory or
protection fault.

## Diagnostic image trace03: smaller allocation request, firmware abort

Trace03 retains the trace02 fixed-probe kernel, trace01 FD and existing boot
arguments. Its memory experiment changes `/chosen/darwinboot-memory-size`
from `0x20000000` (512 MiB) to `0x10000000` (256 MiB). The ADT field begins
at byte offset 9196; its only changed byte is offset 9199, from `0x20` to
`0x10`. The menu identifies this payload as trace03.

| Trace03 input | Recorded value |
| --- | --- |
| Kernel SHA-256, unchanged from trace02 | `4559b64843cab506d9d2781172143e13ddb4c546f7b73e3298d5e23ee62906bd` |
| Firmware FD SHA-256, unchanged | `450165988e2804e6ce6597a29dc7d9b66cadf8d6d582406502cb543b8ab0357c` |
| ADT SHA-256 | `5f0b3c2f268f96a1cb80b75d2e5883737e7732576c0ffc2156fac746305bc566` |
| FAT payload SHA-256 | `a274bad993151ddb1b369b782d337aa2f4f0e29e038477bb94ad5630f2b48a1f` |
| Complete v2 boot image bytes | 68,657,152 |
| Boot image SHA-256 | `7613e03dece54212607b2381161f043f080bd2305d47853dae4b889e0b033466` |
| Device operation | Unique vermeer / revision 20000 / slot b / unlocked state / capacity rechecked; sending / writing `boot_b` / reboot each `OKAY` |
| Screen evidence | Actual handset photograph `abfdc6ad...`: synchronous external read abort, ESR `0x96000010`, FAR `0xE687C000`, ELR `0xC6539224`, SP `0xA76495F0`, SPSR `0x20000205` |

The complete image was unpacked and independently verified. The extracted
kernel and FD match the previous inputs byte-for-byte. UEFI chooses the actual
allocation address; this image does not force a low physical address. A smaller
request is not proof of an allocation below 4 GiB. No allocated PA/SIZE was
observed. The FAR equals the allocation candidate
`align_up(0xE671D000, 2 MiB) + 0x7C000`, where `0x7C000` is the kernel
virtual-base offset within a 2 MiB block. This links the failure to that
candidate range; the photograph does not establish the faulting image or
instruction, successful allocation, or XNU entry.

The stop remains unresolved. A high-RAM, XPU, capsule or lock issue is not
established as the cause. No root/userdata image or system execution is claimed.

## Reader01: no USB snapshot, return to UniXNU/UEFI

The reader01 stock-GKI boot and vendor_boot pair was written, with fastboot
reporting `OKAY` for the writes. During the following 45-second observation,
no expected reader USB connection appeared and no snapshot was received. The
user reported that the phone remained at the Redmi/Xiaomi logo. This is an
unsuccessful reader transport attempt; it is not proof that its Linux init,
CDC gadget or collection protocol ran on the phone. Host-only parser tests
do not establish a successful device connection.

The user then explicitly requested USB interaction in UniXNU/UEFI and no
further Linux reader testing. Restoration to the original vendor_boot_b and
the previously tested trace02 UEFI boot image has completed successfully.
This restores the recorded UEFI inputs; it does not establish a UEFI USB
connection or recover a snapshot from the failed reader attempt.

| Restoration input | SHA-256 |
| --- | --- |
| Original vendor_boot.img | `8bede21000f37f1f1416a983b5a5e79c8984a69d518c85ce9da107a65a6a940f` |
| Trace02 complete UEFI boot image | `dea0d7bdcf99f037b8b5407364fc82b7c3bd0c8e83f6cbb93b7aa6771951be2a` |

No userdata, init_boot, dtbo or vbmeta partition was written in these reader
and restoration operations. No reader snapshot or recovered earlier UEFI/XNU
log is available. Further USB work must establish a UEFI device-controller
and transport path on this board, with its own observed results.

## UEFI USB direction: source review, not a working connection

The [UniXNU firmware guide](https://unixnu.com/zh/porting/uefi-firmware) and
[UEFI driver integration guide](https://unixnu.com/zh/drivers/uefi-drivers)
require a driver to publish its protocol before a consumer uses it. UniXNU's
[logging implementation](https://github.com/UniXNU/UniXNU/blob/main/package/UniXNUPkg/Library/UniXNULib/Main.c)
finds an existing `EFI_SERIAL_IO_PROTOCOL` and writes to it. This does not
create a USB peripheral controller, CDC descriptors or a host COM port.

The current vermeer firmware's
[DSC](https://github.com/yyx2025/uefi-vermeer/blob/vermeer/Platforms/Xiaomi/vermeerPkg/vermeer.dsc)
defaults `VERMEER_VENDOR_HW` to zero. Its
[DXE include](https://github.com/yyx2025/uefi-vermeer/blob/vermeer/Platforms/Xiaomi/vermeerPkg/Include/DXE.inc)
places UsbfnDwc3Dxe, UsbDeviceDxe, UsbConfigDxe, UsbInitDxe and related vendor
drivers behind that gate. The DSC also records display loss when the entire
vendor hardware set is initialized. A USB experiment must isolate its actual
dependencies rather than treat the complete gate as a verified USB fix.

HakiOS supplies a relevant UEFI transport example, distinct from its XNU
drivers: [loader.c](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xnuloader/loader.c)
implements a polled fastboot bulk server through the Qualcomm USB device
protocol declared in [efi.h](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xnuloader/efi.h).
It signals the platform USB-init event, locates the protocol and starts the
descriptor set. Its [host script](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xl)
contains memory-map and log retrieval commands. This is a consumer-side
reference, not a replacement for a missing K70 controller driver, and the
[HakiOS phone guide](https://hackios-web.vercel.app/en/guide/phone) describes
redwood rather than vermeer. Its storage-log behavior is not adopted here.

The [standard USB Function I/O declaration](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/UsbFunctionIo.h)
and Qualcomm's USB device API are different interfaces; a probe must identify
which provider is actually installed. Only after that can a bounded UEFI
memory-map/FDT/log exporter establish host enumeration and transfer results.
Boot-services USB output must not be assumed to survive ExitBootServices.

The [UniXNU USB debug-terminal documentation](https://unixnu.com/guide/debugging#device-compatibility)
requires the giulia device-side protocol. Its host terminal is not evidence
of a UEFI or XNU USB implementation running on K70. The
[AdvancedLogDxe source](https://github.com/UniXNU/UniXNU/tree/main/package/UniXNUPkg/Driver/AdvancedLogDxe)
exports an existing Advanced Logger buffer as QR frames while boot services
are available; it does not itself add USB or log collection. No such protocol
or USB transport has been observed on this K70 in this record.

References: [official bring-up order](https://unixnu.com/zh/porting/bring-up),
[loader entry diagnostics](https://unixnu.com/zh/guide/debugging),
[clean source build record](clean-start-2026-11-04.md).
