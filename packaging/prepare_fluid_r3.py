"""Fetch the pinned Debian FluidR3 GM bank and redistribution notice."""
import hashlib
import io
import os
import tempfile
import tarfile
import urllib.request
from pathlib import Path

URL = "https://deb.debian.org/debian/pool/main/f/fluid-soundfont/fluid-soundfont-gm_3.1-6_all.deb"
SHA256 = "9965fbcc6acee17d6f72b685d28c7f968ec64eba14645445b476d5c6cc2eee4c"
OUTPUT = Path(__file__).resolve().parents[1] / "build-tools" / "fluid-r3"


# Digests of the two distribution files inside the SHA256-pinned Debian archive.
PAYLOAD = {
    "FluidR3_GM.sf2": "74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0",
    "FluidR3-License.txt": "da49177f21a8a760192909bc81b2554a33241502f67e604c92799998ac400ae9",
}


def matches(path, expected):
    if not path.is_file():
        return False
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest() == expected


def write_verified(path, data, digest):
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"SHA-256 mismatch: {path.name}")
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temp:
        pending = Path(temp.name)
    try:
        pending.write_bytes(data)
        pending.chmod(0o644)
        os.replace(pending, path)
    finally:
        pending.unlink(missing_ok=True)


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
                        content = source.read()
                        if not matches(OUTPUT / output, PAYLOAD[output]):
                            write_verified(OUTPUT / output, content, PAYLOAD[output])
            return
        offset += 60 + size + size % 2
    raise ValueError("Missing package data")


if __name__ == "__main__":
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if all(matches(OUTPUT / name, digest) for name, digest in PAYLOAD.items()):
        raise SystemExit(0)
    cached = OUTPUT.parent / "fluid-soundfont-gm_3.1-6_all.deb"
    data = cached.read_bytes() if cached.is_file() else b""
    if hashlib.sha256(data).hexdigest() != SHA256:
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read()
        write_verified(cached, data, SHA256)
    extract(data)
