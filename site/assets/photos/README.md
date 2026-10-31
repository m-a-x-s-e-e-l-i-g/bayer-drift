# Real camera examples

Photographs © Max Seelig (MAXmade), supplied as original Canon EOS M CR2 files
for this Bayer Drift showcase. Photography is not licensed under the project's
software GPL licence.

The images are developed from the camera-processed RAW data with LibRaw/rawpy,
AHD demosaicing, camera white balance, standard automatic brightness and sRGB
output. No additional Bayer Drift simulation, sharpening or noise reduction is
applied by the asset script. Web overviews are resized WebP derivatives.

Each `*-pixels.png` is a lossless 512×512-pixel crop of the full-resolution
development. Each `*-detail.png` shows a 160×160-pixel crop enlarged six times
with nearest-neighbour scaling. The interactive viewer reads the lossless
full-frame lossless WebP tiles and reports magnification in CSS display pixels
per developed-image pixel. Click or tap anywhere in the overview, or pan the
close-up, to inspect any part of the photograph. Only visible 512-pixel tiles
are requested, with a bounded cache; the full-resolution frame is never decoded
all at once in the browser. A loading label distinguishes the immediate overview
preview from native pixels while a new area loads. These are developed RGB pixels,
not a diagram of individual sensor sites.

`photos.json` records crop coordinates, source-file SHA-256 hashes and processing
settings, full-frame tile geometry and individual tile hashes. Capture recipes were not supplied, so no preset, strength or OFF/ON
comparison is attributed to a photograph. Original CR2 files are not published.

Regenerate with:

```text
python scripts/create_gallery_assets.py --source <folder-containing-original-CR2-files>
```
