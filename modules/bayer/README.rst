Bayer Drift
===========

:Author: Max Seelig (MAXmade)
:License: GPL-2.0-or-later
:Summary: Intentional Bayer interference in Canon RAW photos
:Version: 0.1.0

First public release for the original EOS M, Canon firmware 2.0.2.
LiveView stays original for composing and focusing.
The effect is applied when you take a normal Canon RAW photo.
The approximate LiveView effect has been removed.

Installation
------------

With the camera OFF, copy ML/MODULES/bayer.mo to the card.
Keep the existing Magic Lantern 2025-06-20 EOS M installation (API 8).
Disable other MAXmade effects, including Mirror Worlds, and restart.
This does not replace Canon firmware or the ML core.

Shooting
--------

Use A+ / Auto ISO / Canon RAW / single-shot drive and native 1x LCD.
Open Shoot / Bayer Drift and turn Photo effect ON.
Choose a recipe, close the menu and wait for ON - Ready.
Compose and focus using normal Canon LiveView, then press the shutter.
Release between shots. Canon handles saving, review and Play.
Judge the effect from the captured photo, not the unchanged LiveView.
Global Draw is no longer required by this module.

Settings
--------

Preset: Subtle / Crunchy / Overdrive / Custom.
Strength: 1-8. Pattern: Dots / Horizontal / Vertical.
Texture size: Fine / Medium / Coarse. Phase: 1-4.
Shuffle selects a stable new Custom recipe.
All settings survive restart, including settings from dev4.
Changing controls during a focus hold affects later photos.

Capture-only operation
----------------------

There is no display-filter callback, preview canvas or per-frame processing.
One 39,600-byte workspace supports the unchanged RAW transform.
The normal 1.5 MiB memory reserve and contiguous-block check remain.
The first physical half/full shutter edge owns the recipe and workspace.
Source/readout/ROM guards and cancellation stay enabled.
No exports, routine logs, automatic shutter release or flash writes.

Limits
------

Original EOS M / Canon 2.0.2 only. Other cameras, video and bursts unsupported.
Camera operation confirmed by Max Seelig on the original EOS M.
If capture stops, the menu reports why; OFF/ON retries.
Power OFF before replacing/removing the module.
Runtime hooks remain until restart.
