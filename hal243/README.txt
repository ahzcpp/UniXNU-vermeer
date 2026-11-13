K70 iOS16 audio HAL247 — offline candidate only

Public source adapted from the pinned upstream provenance.json. Builds ordinary
arm64/iOS16.0 and the native ABI1 stereo48k/mono input rings.

247 validates all mapping lengths/alignment/overflow/nonoverlap, fixed ABI/ring
layout and mach_timebase_info agreement before publishing any shared pointer;
partial failures unmap every successful mapping. Unqualified VirtualAudio import
rebinding and the donor host-clock constant rewrite are both compiled OFF.

Synthetic host tests exercise actual helper and actual connectService body with
mocked IOKit, including guard pages, failure cleanup and timebase mismatch.
Cloud build/signature checks are not iOS16-server or real-time hardware tests.
No K70 DSP/TDM/Awinic kernel service is activated by this bundle. No audible
playback, microphone, mixer volume, wake/sleep or server restart acceptance.
Do not install until the kernel service and real hardware path are qualified.
No Apple/OEM binaries, credentials, keys or phone/network identifiers included.
