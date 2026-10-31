"""Check the static showcase's local links, image records and crop bounds."""
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import struct
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"


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
    assert not list(SITE.rglob("*.CR2")), "Source RAW files must not be published"
    print("Static site: local links, five source records, ten lossless patches/crops and bounds PASS")


if __name__ == "__main__":
    main()
