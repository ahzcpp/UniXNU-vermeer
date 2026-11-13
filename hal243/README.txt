K70 iOS16 audio HAL243 - independent build candidate, not a working sound release.

Based on BSD-2-Clause-Patent sources in UniXNU/UniXNU-sheng commit
1108068537d5396edb2c63b2f8c6f5b97b0a4bbc. Original authors/license retained.
The shared ShengAudioAbi.h is byte identical to the retained K70 kernel kit:
SHA256 9ef369877cfae3d2255129f7fdf332448b71300a0fc840d6522ec83e09754b52.

This branch builds an ordinary arm64 iOS16.0 HAL plug-in with matching bundle
MinimumOSVersion16.0, a K70 factory/device identity, and its own ad-hoc CDHash.
The unqualified CoreAudio host-clock rewrite is compiled out by default.
Do not treat build success or bundle eligibility as working audio or correct IO timing.

No phone/firmware/partition access is performed. No driver is installed.
No Apple/OEM firmware, extracted Apple libraries, credentials or private keys
are included. macOS/iOS SDK headers remain only on the build runner.

Remaining prerequisites: real ADSP/GLINK/SPF, the K70 AW88261/secondary-TDM
hardware path with accepted calibration and PCM ownership, iOS16 service/HAL
enumeration and timing validation, and audible playback/capture tests.
The existing Pad CS35L43/I2S hardware initialization must not be used on K70.
