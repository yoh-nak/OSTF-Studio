"""Fetch the pinned Debian FluidR3 GM bank and redistribution notice."""
import hashlib
import io
import tarfile
import urllib.request
from pathlib import Path

URL = "https://deb.debian.org/debian/pool/main/f/fluid-soundfont/fluid-soundfont-gm_3.1-6_all.deb"
SHA256 = "9965fbcc6acee17d6f72b685d28c7f968ec64eba14645445b476d5c6cc2eee4c"
OUTPUT = Path(__file__).resolve().parents[1] / "build-tools" / "fluid-r3"


def extract(deb):
    if not deb.startswith(b"!<arch>\n"):
        raise ValueError("Invalid Debian archive")
    offset = 8
    while offset < len(deb):
        name = deb[offset:offset + 16].strip()
        size = int(deb[offset + 48:offset + 58])
        if name.startswith(b"data.tar."):
            with tarfile.open(fileobj=io.BytesIO(deb[offset + 60:offset + 60 + size]), mode="r:*") as archive:
                for member_name, output in (("./usr/share/sounds/sf2/FluidR3_GM.sf2", "FluidR3_GM.sf2"),
                                            ("./usr/share/doc/fluid-soundfont-gm/copyright", "FluidR3-License.txt")):
                    with archive.extractfile(member_name) as source:
                        (OUTPUT / output).write_bytes(source.read())
            return
        offset += 60 + size + size % 2
    raise ValueError("Missing package data")


if __name__ == "__main__":
    OUTPUT.mkdir(parents=True, exist_ok=True)
    cached = OUTPUT.parent / "fluid-soundfont-gm_3.1-6_all.deb"
    if not cached.exists():
        with urllib.request.urlopen(URL, timeout=60) as response:
            cached.write_bytes(response.read())
    data = cached.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("FluidR3 Debian package SHA-256 mismatch")
    extract(data)
