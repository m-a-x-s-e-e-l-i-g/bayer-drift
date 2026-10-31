"""Develop supplied EOS M CR2 examples and lossless native-pixel detail patches.

Requires rawpy, Pillow and NumPy. Original files are opened read-only. No effect
is added here: Bayer Drift is already present in the camera's RAW samples.
"""
import argparse
import hashlib
import json
from pathlib import Path

import rawpy
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = [
    ("bark", "IMG_0011.CR2", "A little rough around the edges.", "Tree bark", .52, .40,
     "Tree bark broken into small repeating colour blocks and bright fringes."),
    ("city", "IMG_0025.CR2", "The city, slightly out of register.", "City blocks", .54, .53,
     "A cityscape turned into a dense patchwork of coloured interference."),
    ("harbour", "IMG_0028.CR2", "An ordinary edge. An odd result.", "By the water", .48, .42,
     "A pale waterside post with coloured registration fringes against the horizon."),
    ("wall", "IMG_0047.CR2", "Colour finds the cracks.", "Brickwork", .58, .40,
     "Pale brickwork with fine magenta, green and cyan patterns along the mortar."),
    ("bark-two", "IMG_0014.CR2", "Same tree. Another kind of strange.", "Bark, again", .60, .50,
     "A second tree-bark photograph with dense blue, gold and violet texture."),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "site/assets/photos")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    entries = []
    for slug, filename, title, label, nx, ny, alt in SAMPLES:
        original = args.source / filename
        source_hash = hashlib.sha256(original.read_bytes()).hexdigest()
        with rawpy.imread(str(original)) as raw:
            pixels = raw.postprocess(
                demosaic_algorithm=rawpy.DemosaicAlgorithm.AHD,
                use_camera_wb=True, no_auto_bright=False, output_bps=8,
                output_color=rawpy.ColorSpace.sRGB,
                user_flip=None,
            )
        photo = Image.fromarray(pixels)
        width, height = photo.size
        for suffix, limit, quality in (("wide", 1800, 90), ("small", 720, 85)):
            preview = photo.copy()
            preview.thumbnail((limit, limit), Image.Resampling.LANCZOS)
            preview.save(args.output / f"{slug}-{suffix}.webp", quality=quality, method=6)
        edge = 512
        left = max(0, min(width-edge, round(nx*width-edge/2)))
        top = max(0, min(height-edge, round(ny*height-edge/2)))
        patch = photo.crop((left, top, left+edge, top+edge))
        patch.save(args.output / f"{slug}-pixels.png", optimize=True)
        # A fixed 160x160 native-pixel crop, enlarged with nearest-neighbour.
        detail = patch.crop((176, 176, 336, 336)).resize((960, 960), Image.Resampling.NEAREST)
        detail.save(args.output / f"{slug}-detail.png", optimize=True)
        item = dict(id=slug, original=filename, title=title, label=label, alt=alt,
                    width=width, height=height, patch=dict(x=left, y=top, size=edge),
                    source_sha256=source_hash, wide=f"assets/photos/{slug}-wide.webp",
                    small=f"assets/photos/{slug}-small.webp", pixels=f"assets/photos/{slug}-pixels.png",
                    detail=f"assets/photos/{slug}-detail.png")
        entries.append(item)
        print(filename, photo.size, "patch", left, top, flush=True)
    manifest = dict(
        photographer="Max Seelig (MAXmade)", camera="Canon EOS M",
        processing=dict(rawpy=rawpy.__version__, demosaic="AHD", color="sRGB",
                        white_balance="camera", auto_bright=True, bit_depth=8,
                        added_effects=False, sharpening=False, noise_reduction=False),
        note="Web developments of camera-processed CR2 files; no synthetic effect added. "
             "Recipe values were not supplied and are not inferred. Native patches are lossless PNG; "
             "web overviews are resized WebP. Source CR2 files are not distributed.",
        photos=entries,
    )
    (args.output / "photos.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
