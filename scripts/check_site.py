"""Check the static showcase's local links, image records and crop bounds."""
from html.parser import HTMLParser
import hashlib
import json
from pathlib import Path
import re
import struct
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"


def lossless_webp_size(data):
    assert data[:4] == b"RIFF" and data[8:12] == b"WEBP"
    at = 12
    while at+8 <= len(data):
        kind, size = data[at:at+4], struct.unpack_from("<I", data, at+4)[0]
        if kind == b"VP8L":
            assert data[at+8] == 0x2f
            bits = struct.unpack_from("<I", data, at+9)[0]
            return (bits & 0x3fff)+1, ((bits >> 14) & 0x3fff)+1
        at += 8+size+(size & 1)
    raise AssertionError("Expected a lossless WebP tile")


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.references = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            assert a["id"] not in self.ids, "Duplicate HTML id: " + a["id"]
            self.ids.add(a["id"])
        for key in ("src", "href"):
            if key in a:
                self.references.append(a[key])
        if "srcset" in a:
            self.references.extend(item.strip().split()[0] for item in a["srcset"].split(","))


def main():
    page = Page()
    page.feed((SITE / "index.html").read_text(encoding="utf-8"))
    for ref in page.references:
        parts = urlsplit(ref)
        if parts.scheme or parts.netloc:
            continue
        if parts.path:
            target = SITE / unquote(parts.path).lstrip("/")
            assert target.is_file(), "Missing local file: " + ref
        elif parts.fragment:
            assert parts.fragment in page.ids, "Missing section: " + ref
    for path in re.findall(r"url\(['\"]?([^)'\"]+)", (SITE / "styles.css").read_text()):
        assert (SITE / path).is_file(), "Missing CSS asset: " + path
    data = json.loads((SITE / "assets/photos/photos.json").read_text())
    assert len(data["photos"]) == 5
    tile_count = 0
    for photo in data["photos"]:
        crop = photo["patch"]
        assert 0 <= crop["x"] <= photo["width"] - crop["size"]
        assert 0 <= crop["y"] <= photo["height"] - crop["size"]
        assert re.fullmatch(r"[0-9a-f]{64}", photo["source_sha256"])
        for key in ("wide", "small", "pixels", "detail"):
            assert (SITE / photo[key]).is_file(), photo[key]
        for key, expected in (("pixels", 512), ("detail", 960)):
            png = (SITE / photo[key]).read_bytes()
            assert png[:8] == b"\x89PNG\r\n\x1a\n"
            assert struct.unpack_from(">II", png, 16) == (expected, expected)
        tiles = photo["tiles"]
        size = tiles["size"]
        assert tiles["lossless"] and size == 512
        assert tiles["columns"] == (photo["width"]+size-1)//size
        assert tiles["rows"] == (photo["height"]+size-1)//size
        assert set(tiles["sha256"]) == {f"{x}-{y}.webp" for y in range(tiles["rows"]) for x in range(tiles["columns"])}
        for y in range(tiles["rows"]):
            for x in range(tiles["columns"]):
                path = tiles["template"].replace("{x}", str(x)).replace("{y}", str(y))
                tile = (SITE / path).read_bytes()
                assert hashlib.sha256(tile).hexdigest() == tiles["sha256"][f"{x}-{y}.webp"], path
                assert lossless_webp_size(tile) == (min(size, photo["width"]-x*size), min(size, photo["height"]-y*size)), path
                tile_count += 1
    assert not list(SITE.rglob("*.CR2")), "Source RAW files must not be published"
    print(f"Static site: local links, five source records, ten lossless crops and {tile_count} full-frame lossless tiles PASS")


if __name__ == "__main__":
    main()
