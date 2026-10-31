# Bayer Drift 0.1.0

**Magic Lantern module for the original Canon EOS M, firmware 2.0.2.**
First public release. EOS M camera operation confirmed by Max Seelig.

Compose and focus with normal Canon LiveView. Bayer Drift is applied when
you take a RAW photograph. The saved RAW
transform and all recipes are unchanged from the capture-only development build.

## Install and shoot

1. Keep the existing EOS M Magic Lantern 2025-06-20 `ed3e7c0` installation,
   module API 8. With the camera OFF, copy **ML/MODULES/bayer.mo** from the ZIP.
2. Disable other MAXmade effects, including Mirror Worlds, and restart.
   Runtime capture hooks remain resident until a power-off/restart.
3. Use **A+ / Auto ISO / Canon RAW / single-shot drive**, native **1x LCD**.
4. Open **Shoot → Bayer Drift → Photo effect ON**. Choose a recipe, close the
   menu, wait for **ON - Ready**, compose/focus normally and use the shutter.
5. Release between photos. Canon handles saving, review and playback. Judge
   the actual captured photo; LiveView deliberately stays original.

Global Draw is no longer needed by Bayer Drift. The module does not change
that setting or disable Canon/ML focus overlays. The guarded capture mode still
requires normal 1x LCD operation. RAW converters and camera JPEG review can
render colour differently; the saved CR2 is the definitive processed image.

## Settings

| Control | Values | Applies to |
|---|---|---|
| Photo effect | OFF / ON | RAW capture |
| Preset | Subtle / Crunchy / Overdrive / Custom | Starting recipe |
| Strength | 1-8 | Local sample displacement |
| Pattern | Dots / Horizontal / Vertical | Both axes / X only / Y only |
| Texture size | Fine / Medium / Coarse | Local tile span |
| Phase | 1-4 | Affected CFA row and column parity |
| Shuffle | Press SET | A stable new Custom recipe |

Editing a preset switches to Custom. ON/OFF and recipes save automatically in
checked `bayer0.prf` / `bayer1.prf` slots. Existing settings remain compatible.
The first physical half/full shutter edge freezes the shot's recipe. Controls
changed during a focus hold apply to later photos. Half-press alone takes no
photo. Focus holds refresh the existing shutter deadline while Canon stays
ready; a release without shooting quietly releases its workspace lease.

## Processing and memory

The unchanged RAW kernel rearranges original 14-bit samples within their own
CFA planes in bounded local tiles. This deliberately misregisters the planes,
producing repeating false colour and beaded edges. There is no averaging,
clipping or random noise. The decoder-visible bounds, optical-black pixels and
packing neighbours are preserved.

The capture runtime registers **no display-filter callback**. It allocates no
preview canvas, reads no LiveView pixel buffer and performs no per-frame
rendering or copying. It only tracks camera state, desired/held controls and
one **39,600-byte RAW workspace**. Including the unchanged **1.5 MiB** reserve,
the initial pool requirement is **1,612,464 bytes**; the largest allocation is
39,600 bytes, with the existing 1 KiB contiguous-block headroom.

ROM/context/readout/overlap checks, physical-shutter ownership and cancellation
remain in the capture gate. A pre-write cancellation leaves the sensor image
unchanged. Once writing starts the complete bounded frame finishes. Stop or
shutdown retains the workspace until an active RAW callback releases it.
The existing 100 ms scheduling/source-stability check is retained.

## Validation

Build: `python tools/build_module.py --module bayer --runtime wsl`.

Host sanitizers: `python scripts/test_bayer.py --full`. Add `--arm` with the
existing EOS QEMU toolchain for both ARM CPU models and execution of the actual
built `.mo` against a synthetic SDK. These are not physical-camera tests.

The suite checks the unchanged RAW bytes against an independent scalar oracle
and the immutable dev1 kernel, all 288 recipes, full EOS M geometry, capture
without any rendered frame, half-press recipe freezing, timeout, cancellation,
review/mode refusal, lease generation, memory release and settings recovery.
It also runs with a 1.75 MiB DMA pool and a 64 KiB largest block.

The module fixture checks that the callback table contains no display filter,
no display APIs are called, only the RAW workspace is allocated, normal Canon
keys pass through, controls are available without frame delivery, OFF/ON works,
and unsupported firmware is refused. The import audit rejects linked display
APIs and the preview renderer. Older preview helpers are retained for regression/history and are not linked
into the release module.

## Camera check and limits

Compare OFF, Subtle, Crunchy and Overdrive shots of lettering, spokes or fine
fabric. Retain the original CR2 files. Confirm AF, ordinary LiveView, the normal
shutter, review/Play, recipe changes and restart persistence on the camera.

Only original EOS M 2.0.2 single-shot Canon RAW is supported by this release.
Video, bursts and other cameras are outside its workflow. If capture stops,
retain its menu reason and try OFF/ON. Power OFF before replacing the module.
