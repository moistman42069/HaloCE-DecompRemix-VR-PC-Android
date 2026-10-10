# v1.0.19 release provenance

Release: https://github.com/moistman42069/HaloCE-Quest-VR/releases/tag/v1.0.19
Upstream base: OpenCE Build 157, network 24, with selected compatible updates through Build 177
Version: 1.0.19 / Android version code 63
Packages: com.halo.decomp and com.halo.decomp.vr; ARM64; minimum API 28
Runtime source commit: pending final package
Release documentation/source commit: pending final package
Signing certificate SHA-256: 53d416f7e123cc62b749940983209bc9a400002e033fd5f50900d4ffad8e2aa4

## APK assets

Fill these from the final stable packaging manifest before publishing.

| Asset | Package | Bytes | SHA-256 |
|---|---|---:|---|
| HaloCE-Android-1.0.19.apk | com.halo.decomp | pending | pending |
| HaloCE-Quest-1.0.19.apk | com.halo.decomp.vr | pending | pending |
| compatibility.json | updater metadata | pending | pending |

## Build and validation

- Build both APKs from the same release runtime commit with stable version name 1.0.19 and code63.
- Run the complete Quest/Android check gates for both build flavors and the focused voice integration checks.
- Confirm the expected signing certificate, package IDs, ABI, version metadata, ZIP integrity, adaptive icon, release-guide placeholder warning and networking parity.
- Owner-confirmed device result: the Quest no longer closes while joining the reported server, and voice chat works. This confirms those reported paths only; it is not a universal device/server/map/revision/player-count certification.
- No exhaustive Original/Rev 1/Rev 2 image, all-network, 128-player campaign, or all-device matrix is implied.

## Release contents and history

Only the Android APK, Quest APK, and updater-required compatibility.json are
attached as custom GitHub assets, in that order. GitHub's tag source archive
provides the source; no duplicate archives, logs, game data or temporary build
artifacts are attached. The v1.0.18 release and repository About description
remain unchanged.
