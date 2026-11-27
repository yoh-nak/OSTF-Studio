"""Match the bilingual illustrated catalogue to the devices in this build."""
import re
import struct
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging"))
from plugin_choices import catalog, display_name

GALLERY = ROOT / "docs/plugins"
GROUPS = ("workstations", "eq-dynamics", "space-and-tone", "analysis")


def anchors(text):
    named = set(re.findall(r'<a (?:id|name)="([^"]+)"', text))
    for title in re.findall(r"^#{1,6} (.+)$", text, re.MULTILINE):
        named.add(re.sub(r"[^\w -]", "", title.lower()).replace(" ", "-"))
    return named


def verify():
    devices = {p["name"] if p["code"] == "Osd1" else display_name(p) for p in catalog()}
    for suffix, index in ((".en.md", "en.md"), (".md", "README.md")):
        overview = (GALLERY / index).read_text(encoding="utf-8")
        seen = set()
        for group in GROUPS:
            page = GALLERY / (group + suffix)
            sections = re.split(r"^## ([^\n]+)\n", page.read_text(encoding="utf-8"), flags=re.MULTILINE)
            for name, body in zip(sections[1::2], sections[2::2]):
                slug = name.lower().replace(" ", "-")
                assert name not in seen, f"Duplicate device in {page}: {name}"
                seen.add(name)
                assert f"({page.name}#{slug})" in overview, (page, name)
                assert f"](../images/plugins/{slug}.png)" in body, (page, name)
        assert seen == devices, (index, seen ^ devices)

    pages = [ROOT / "README.md", ROOT / "README.ja.md", *GALLERY.glob("*.md"),
             *(ROOT / "docs/manual").glob("*.md")]
    for page in pages:
        text = page.read_text(encoding="utf-8")
        for link, img in re.findall(r'\]\(([^)\s]+)\)|<img\s+src="([^"]+)"', text):
            url = urlsplit(link or img)
            if url.scheme or url.netloc:
                continue
            target = (page.parent / unquote(url.path)) if url.path else page
            assert target.exists(), (page, target)
            if url.fragment:
                assert unquote(url.fragment) in anchors(target.read_text(encoding="utf-8")), (page, url)
    for image in (ROOT / "docs/images").rglob("*.png"):
        with image.open("rb") as stream:
            header = stream.read(24)
        assert header[:8] == bytes.fromhex("89504e470d0a1a0a") and header[12:16] == b"IHDR", image
        assert all(struct.unpack(">II", header[16:24])), image
    print(f"Verified {len(devices)} device entries per language and relative links in {len(pages)} guides.")


if __name__ == "__main__":
    verify()
