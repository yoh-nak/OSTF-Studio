"""Validate the authored demo mixes against a checkout of the pinned OSTF 5 specification."""
import argparse
import hashlib
import json
import io
import wave
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile

from jsonschema import Draft202012Validator


def verify(path, spec, schemas, catalog):
    subprocess.run([sys.executable, str(spec / "tools/ostfp.py"), "validate", str(path)], check=True)
    with ZipFile(path) as package:
        manifest = json.loads(package.read("manifest.json"))
        schemas["manifest"].validate(manifest)
        score_bytes = package.read(manifest["score"]["path"])
        assert score_bytes == path.with_suffix(".ostf").read_bytes(), "standalone score differs"
        score = json.loads(score_bytes)
        schemas["score"].validate(score)
        document, = manifest["documents"]
        session_bytes = package.read(document["path"])
        session = json.loads(session_bytes)
        schemas["production"].validate(session)
        assert hashlib.sha256(session_bytes).hexdigest() == document["sha256"]
        assert hashlib.sha256(score_bytes).hexdigest() == session["score"]["sha256"] == manifest["score"]["sha256"]
        assert score["extensions"]["org.ostf.studio"]["data"] == {k: v for k, v in session.items()
            if k not in ("profile", "profileVersion", "score")}
        if path.stem in ("paper-lantern-replies", "first-light-vector"):
            source, = session["assets"]
            region, = session["audioRegions"]
            asset, = manifest["assets"]
            binding, = manifest["bindings"]
            assert binding == {"resourceKind": "productionAsset", "documentPath": document["path"],
                               "resourceId": source["id"], "assetId": asset["id"]}
            wav = package.read(asset["path"])
            assert wav == (path.parent / source["uri"]).read_bytes()
            assert hashlib.sha256(wav).hexdigest() == source["sha256"] == asset["sha256"]
            assert region["startTick"] > 0 and region["sourceAssetId"] == source["id"]
            with wave.open(io.BytesIO(wav)) as audio:
                assert (audio.getframerate(), audio.getnchannels(), audio.getnframes()) == (
                    source["sampleRateHz"], source["audioChannels"], region["sourceFrameCount"])
                assert any(audio.readframes(audio.getnframes()))
            strip, = [c for c in session["channels"] if c["id"] == region["channelId"]]
            assert strip["outputChannelId"] == "bus-2" and strip["sends"][0]["targetChannelId"] == "return-1"
            assert len(strip["deviceIds"]) == 2
    channels = {c["id"]: c for c in session["channels"]}
    devices = {d["id"]: d for d in session["devices"]}
    assert len(channels) == len(session["channels"]) and len(devices) == len(session["devices"])
    assigned = [d for c in channels.values() for d in c["deviceIds"]]
    assert len(set(assigned)) == len(assigned) and set(assigned) == devices.keys()
    tracks = [c for c in channels.values() if c["kind"] == "track"]
    assert sorted(c["trackId"] for c in tracks) == sorted(t["id"] for t in score["tracks"])
    used = set()
    for c in channels.values():
        chain = [devices[k] for k in c["deviceIds"]]
        if c["kind"] == "track" and c["trackId"] != "foley":
            gm, *chain = chain
            assert gm["role"] == "instrument" and gm["plugin"]["name"] == "OSTF GM Sound Module"
        for d in chain:
            ref = d["plugin"]
            assert d["role"] == "effect" and d["enabled"] and not d["bypass"]
            assert ref["format"] == "vst3" and ref["vendor"] == "OSTF Studio" and catalog[ref["name"]] == ref["pluginId"]
            used.add(ref["name"])
        visited = set()
        current = c
        while current["id"] != "master":
            assert current["id"] not in visited, "audio routing cycle"
            visited.add(current["id"])
            current = channels[current["outputChannelId"]]
    assert any(len(c["deviceIds"]) >= 3 for c in tracks), "expected a serial insert chain"
    example_track, ordered_fx = {
        "paper-lantern-replies": ("voice-track", ["OSTF EQ", "OSTF Compressor"]),
        "blue-hour-waltz": ("horn", ["OSTF EQ", "OSTF Compressor"]),
        "neon-platforms": ("hook", ["OSTF Guitar Amp", "OSTF EQ"]),
        "relay-circuit": ("sub", ["OSTF Distortion", "OSTF EQ"]),
        "offset-bridges": ("lead", ["OSTF Distortion", "OSTF Guitar Amp"]),
        "copper-mile": ("lead", ["OSTF Distortion", "OSTF Guitar Amp"]),
        "first-light-vector": ("sub", ["OSTF Distortion", "OSTF EQ"]),
    }[path.stem]
    example = next(c for c in tracks if c["trackId"] == example_track)
    assert [devices[k]["plugin"]["name"] for k in example["deviceIds"][1:]] == ordered_fx
    for bus in ("bus-1", "bus-2"):
        assert sum(c["outputChannelId"] == bus for c in tracks) >= 2 and channels[bus]["outputChannelId"] == "master"
    room = channels["return-1"]
    assert devices[room["deviceIds"][0]]["plugin"]["name"] == "OSTF Reverb" and room["outputChannelId"] == "master"
    sends = [s for c in tracks for s in c.get("sends", [])]
    assert len(sends) >= 2 and all(s["enabled"] and not s["preFader"] and s["targetChannelId"] == room["id"]
                                   and s["gainDb"] < 0 for s in sends)
    print(f"{path.stem}: {len(tracks)} tracks, {len(assigned) - len(tracks)} inserts, {len(sends)} sends")
    return used


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path)
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    catalog = {x["name"]: "ABCDEF019182FAEB4F737466" + x["code"].encode("ascii").hex().upper()
               for x in json.loads((directory.parents[1] / "src/plugins/effects/catalog.json").read_text())}
    schemas = {key: Draft202012Validator(json.loads((args.spec / "schema" / file).read_text(encoding="utf-8")))
               for key, file in {"score": "ostf-5.0.0.schema.json", "manifest": "ostf-package-manifest-1.0.schema.json",
                                 "production": "ostf-production-session-0.3.schema.json"}.items()}
    blank = json.loads((directory.parents[1] / "assets/NewProject.ostf").read_text(encoding="utf-8"))
    schemas["score"].validate(blank)
    assert not (blank["tracks"] or blank["soundSlots"] or blank["harmony"]["chords"] or blank["timeline"]["sections"])
    covered = set()
    for path in sorted(directory.glob("*.ostfp")):
        covered.update(verify(path, args.spec, schemas, catalog))
    assert covered == catalog.keys(), "missing effect coverage"
