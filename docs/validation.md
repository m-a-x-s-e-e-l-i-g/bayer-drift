# Release validation

## Camera confirmation

On 2026-10-31, Max Seelig confirmed that the current Bayer Drift module was working on the EOS M and requested its first GitHub release. This is an owner-reported physical-camera result, separate from the automated fixtures. An installed-module hash and result CR2 files were not supplied with that initial confirmation.

Max subsequently supplied original CR2 photographs for the [public gallery](https://bayer-drift.netlify.app/) and [README examples](../README.md#from-the-eos-m). Five photographs are shown with lossless, coordinate-matched detail patches. Their source-file hashes, RAW-development settings and crop bounds are recorded in [photos.json](../site/assets/photos/photos.json). Capture recipes and an exact installed-module hash remain unrecorded. The original release's archived report is retained unchanged; the later gallery is additional photographic evidence, not a new automated hardware test.

Release 0.1.0 promotes the capture-only development implementation. Its camera-processing source is unchanged; the release updates version metadata, documentation and build/package tooling. The release evidence compares every module build input against the archived development source, normalizing line endings, with only the version header and module README allowed to change. The versioned release binary was rebuilt and passed the full automated suite; it has a new binary hash and is not claimed to have been independently retested on the camera.

The supported target is the original EOS M, firmware 2.0.2, using the Magic Lantern 2025-06-20 EOSM.202 API-8 build. This confirmation does not establish compatibility with other cameras or ML builds.

## Automated checks

`python scripts/test_bayer.py --arm --full` checks:

- The 14-bit RAW sample permutation against an independent scalar oracle and the immutable first-development kernel, across all 288 recipes and full EOS M geometry.
- Physical shutter ownership, held recipes, mode/review refusal, timeout, pre-write and post-write cancellation, scratch leases and shutdown.
- Menu controls, saved-settings recovery and a constrained 1.75 MiB DMA pool with a 64 KiB largest allocation region.
- Execution of the actual built relocatable module against a synthetic SDK on `arm946-eos` and `arm946-eos5`, including a negative control that must reject deliberate corruption.
- No display-filter callback, preview renderer, display processing APIs or preview allocation in the release module.

The build audits imports against the hash-verified official EOSM.202 distribution, rejects camera platform-header dependencies and checks property-handler registration. Packaging rechecks source, module, evidence and ZIP-member hashes.

These are host/emulator checks, not camera frame-rate measurements or tests of real sensor DMA timing. The release makes no numerical on-camera processing-speed claim.
