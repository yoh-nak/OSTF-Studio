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
    # Both guides also cover explicitly labelled devices from Windows development builds.
    inventories = []
    for suffix, index in ((".en.md", "en.md"), (".md", "README.md")):
        overview = (GALLERY / index).read_text(encoding="utf-8")
        seen = set()
        preview = set()
        preview_label = "**Windows development preview**" if index == "en.md" else "**Windows開発版**"
        for group in GROUPS:
            page = GALLERY / (group + suffix)
            sections = re.split(r"^## ([^\n]+)\n", page.read_text(encoding="utf-8"), flags=re.MULTILINE)
            count = re.search(rf"^## \[.*?\((\d+)\)\]\({re.escape(page.name)}\)$", overview, re.MULTILINE)
            assert count and int(count[1]) == len(sections[1::2]), (index, group, "device count")
            for name, body in zip(sections[1::2], sections[2::2]):
                slug = name.lower().replace(" ", "-")
                assert name not in seen, f"Duplicate device in {page}: {name}"
                seen.add(name)
                if body.lstrip().startswith(preview_label):
                    preview.add(name)
                assert f"({page.name}#{slug})" in overview, (page, name)
                assert f"](../images/plugins/{slug}.png)" in body, (page, name)
        assert seen == devices | preview, (index, seen ^ (devices | preview))
        inventories.append((seen, preview))
    assert inventories[0] == inventories[1], "The two guides disagree on devices or preview status"

    homepages = [ROOT / "README.md", ROOT / "README.ja.md", *sorted((ROOT / "docs/languages").glob("*.md"))]
    funding = (ROOT / ".github/FUNDING.yml").read_text(encoding="utf-8")
    account = re.search(r"^buy_me_a_coffee: ([\w-]+)$", funding, re.MULTILINE)
    assert account, "Missing Buy Me a Coffee account"
    assets = None
    for home in homepages:
        body = home.read_text(encoding="utf-8")
        assert f"](https://buymeacoffee.com/{account[1]})" in body, home
        links = {link for link in re.findall(r"\]\(([^)]+)\)", body) if "/releases/download/" in link}
        if assets is None:
            assets = links
            assert len(assets) == 4, "Expected two Windows and two macOS downloads"
        assert links == assets, (home, "download links")
        nav = next(line for line in body.splitlines() if line.startswith("🌐 "))
        targets = {(home.parent / link).resolve() for link in re.findall(r"\]\(([^)]+)\)", nav)}
        assert targets == set(homepages) - {home}, (home, "language navigation")

    pages = [*homepages, *GALLERY.glob("*.md"),
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
    print(f"Verified {len(seen)} device entries per language ({len(devices)} in this build) "
          f"and relative links in {len(pages)} guides.")


if __name__ == "__main__":
    verify()
