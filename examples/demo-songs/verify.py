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
from referencing import Registry, Resource


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
        regions = session.get("audioRegions", [])
        count = {"relay-circuit": 4, "first-light-vector": 5, "paper-lantern-replies": 1}.get(path.stem, 0)
        assert len(regions) == len(manifest["assets"]) == len(manifest["bindings"]) == len(session.get("assets", [])) == count
        audio_strips = {r["channelId"] for r in regions}
        assert len(audio_strips) == count
        for region in regions:
            source, = [a for a in session["assets"] if a["id"] == region["sourceAssetId"]]
            binding, = [b for b in manifest["bindings"] if b["resourceId"] == source["id"]]
            asset, = [a for a in manifest["assets"] if a["id"] == binding["assetId"]]
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
            assert strip["outputChannelId"] in ("bus-1", "bus-2") and strip["sends"][0]["targetChannelId"] == "return-1"
            assert len(strip["deviceIds"]) == 2
        if count >= 4:
            bar = score["ppq"] * 4
            long_stems = [r for r in regions if r["durationTicks"] >= 16 * bar]
            assert len(long_stems) == 4
            assert any(min(a["startTick"] + a["durationTicks"], b["startTick"] + b["durationTicks"])
                       - max(a["startTick"], b["startTick"]) >= 8 * bar
                       for i, a in enumerate(long_stems) for b in long_stems[i+1:])
            assert {c["outputChannelId"] for c in session["channels"] if c["id"] in audio_strips} == {"bus-1", "bus-2"}
    channels = {c["id"]: c for c in session["channels"]}
    devices = {d["id"]: d for d in session["devices"]}
    assert len(channels) == len(session["channels"]) and len(devices) == len(session["devices"])
    assigned = [d for c in channels.values() for d in c["deviceIds"]]
    assert len(set(assigned)) == len(assigned) and set(assigned) == devices.keys()
    tracks = [c for c in channels.values() if c["kind"] == "track"]
    assert sorted(c["trackId"] for c in tracks) == sorted(t["id"] for t in score["tracks"])
    lanes = [a for a in session["automation"] if a["target"]["kind"] == "device"]
    effect_ids = {d["id"] for d in devices.values() if d["role"]=="effect" and d["plugin"]["name"]!="OSTF Wave Analyzer"}
    assert {a["target"]["deviceId"] for a in lanes} == effect_ids
    assert all(len(a["points"]) == 1 and a["points"][0]["atTick"] == 0 and 0<=a["points"][0]["value"]<=1 for a in lanes)
    if path.stem == "blue-hour-waltz-shared-gm":
        instrument, = [d for d in devices.values() if d["role"] == "instrument"]
        owner, = [c for c in tracks if instrument["id"] in c["deviceIds"]]
        assert instrument["plugin"]["name"] == "OSTF GM Sound Module" and owner["trackId"] == "keys"
        routes = {r["trackId"]: r for r in session["hostExtensions"][0]["data"]["tracks"]}
        assert len({r["midiChannel"] for r in routes.values()}) == len(routes) == 7
        for t in score["tracks"]:
            r = routes[t["id"]]
            assert r["midiChannel"] == t["midi"]["channel"] and r.get("destinationTrackId", t["id"]) == "keys"
            assert any(e["type"] == "programChange" and e["channel"] == r["midiChannel"]
                       for c in t["clips"] for e in c.get("midiEvents", []))
        return {d["plugin"]["name"] for d in devices.values() if d["role"] == "effect"}
    neon = path.stem == "neon-platforms"
    if neon:
        parts = {"hook": ("hook", 1), "brass": ("hook", 7),
                 "strings": ("hook", 8), "bass": ("bass", 3), "comp": ("bass", 6)}
        routes = session["hostExtensions"][0]["data"]["tracks"]
        assert len(routes) == len(parts)
        assert {r["trackId"]: (r.get("destinationTrackId", r["trackId"]), r["midiChannel"]) for r in routes} == parts
        gm_ids = [d["plugin"]["pluginId"] for d in devices.values() if d["plugin"]["name"] == "OSTF GM Sound Module"]
        assert len(gm_ids) == 2 and len(set(gm_ids)) == 1
        for t in score["tracks"]:
            if t["id"] in parts: assert t["midi"]["channel"] == parts[t["id"]][1]
    used = set()
    for c in channels.values():
        chain = [devices[k] for k in c["deviceIds"]]
        if c["kind"] == "track" and c["id"] not in audio_strips:
            if neon and c["trackId"] in parts and parts[c["trackId"]][0] != c["trackId"]:
                assert not chain
                continue
            gm, *chain = chain
            assigned_synths = {"arp": "OSTF FM Synth", "sub": "OSTF Analog Synth", "drums": "OSTF Drum Machine", "pad": "OSTF Wavetable Synth"}
            palette = {"blue-hour-waltz":{"vibes":"FM Synth"}, "relay-circuit":{"arp":"FM Synth","sub":"Analog Synth","drums":"Drum Machine","chord":"Wavetable Synth","atmos":"Wavetable Synth"},
                       "paper-lantern-replies":{"keys":"FM Synth","answer":"FM Synth","strings":"Wavetable Synth"},
                       "copper-mile":{"organ":"FM Synth"}, "offset-bridges":{"strings":"Wavetable Synth","pulse":"FM Synth"},
                       "neon-platforms":{"keys":"FM Synth","pad":"Wavetable Synth","drums":"Drum Machine"}}
            name = palette.get(path.stem,{}).get(c["trackId"])
            expected = assigned_synths.get(c["trackId"]) if path.stem == "first-light-vector" else ("OSTF "+name if name else None)
            assert gm["role"] == "instrument" and gm["plugin"]["name"] == (expected or ("OSTF Formant Singer" if path.stem == "paper-lantern-replies" and c["trackId"] in ("voice-track", "backing") else "OSTF GM Sound Module"))
            if expected and path.stem == "first-light-vector":
                lane = next(t for t in score["tracks"] if t["id"] == c["trackId"])
                changes = [(clip["start"] + e["start"], e["program"]) for clip in lane["clips"]
                           for e in clip.get("midiEvents", []) if e["type"] == "programChange"]
                assert len(changes) == 2 and changes[1] == (48 * 4 * score["ppq"], 2)
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
        "paper-lantern-replies": ("voice-track", ["OSTF De-Esser", "OSTF Compressor"]),
        "blue-hour-waltz": ("horn", ["OSTF EQ", "OSTF Compressor"]),
        "neon-platforms": ("hook", ["OSTF EQ", "OSTF Bus Compressor"]),
        "relay-circuit": ("sub", ["OSTF Bass Rig", "OSTF Parametric EQ"]),
        "offset-bridges": ("lead", ["OSTF Electric Guitar Rig", "OSTF Delay"]),
        "copper-mile": ("lead", ["OSTF Electric Guitar Rig", "OSTF Delay"]),
        "first-light-vector": ("sub", ["OSTF Bass Rig", "OSTF Parametric EQ"]),
    }[path.stem]
    example = next(c for c in tracks if c["trackId"] == example_track)
    assert [devices[k]["plugin"]["name"] for k in example["deviceIds"][1:]] == ordered_fx
    for bus in ("bus-1", "bus-2"):
        bus_tracks = [c["trackId"] for c in tracks if c["outputChannelId"] == bus]
        assert channels[bus]["outputChannelId"] == "master"
        if neon:
            assert ("bass" if bus == "bus-1" else "hook") in bus_tracks
        else:
            assert len(bus_tracks) >= 2
    room = channels["return-1"]
    assert devices[room["deviceIds"][0]]["plugin"]["name"] == "OSTF Reverb" and room["outputChannelId"] == "master"
    sends = [s for c in tracks for s in c.get("sends", [])]
    assert len(sends) >= 2 and all(s["enabled"] and not s["preFader"] and s["targetChannelId"] == room["id"]
                                   and s["gainDb"] < 0 for s in sends)
    print(f"{path.stem}: {len(tracks)} tracks ({count} audio), {sum(d['role'] == 'effect' for d in devices.values())} inserts, {len(sends)} sends")
    return used


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path)
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    catalog = {x["name"]: "ABCDEF019182FAEB4F737466" + x["code"].encode("ascii").hex().upper()
               for x in json.loads((directory.parents[1] / "src/plugins/effects/catalog.json").read_text())}
    schemas = {key: Draft202012Validator(json.loads((args.spec / "schema" / file).read_text(encoding="utf-8")))
               for key, file in {"score": "ostf-5.1.0.schema.json", "manifest": "ostf-package-manifest-1.1.schema.json",
                                 "production": "ostf-production-session-0.4.schema.json"}.items()}
    blank = json.loads((directory.parents[1] / "assets/NewProject.ostf").read_text(encoding="utf-8"))
    # The application creates 5.2 scores. The published demo packages still target 5.1.
    bundled = directory.parents[1] / "third_party/ostf-schemas"
    documents = [json.loads(path.read_text(encoding="utf-8")) for path in bundled.glob("*.schema.json")]
    registry = Registry().with_resources((doc["$id"], Resource.from_contents(doc)) for doc in documents if "$id" in doc)
    startup_schema = json.loads((bundled / "ostf-5.2.0.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator(startup_schema, registry=registry).validate(blank)
    assert not (blank["tracks"] or blank["soundSlots"] or blank["harmony"]["chords"] or blank["timeline"]["sections"])
    covered = set()
    for path in sorted(directory.glob("*.ostfp")):
        covered.update(verify(path, args.spec, schemas, catalog))
    assert {"OSTF De-Esser","OSTF Electric Guitar Rig","OSTF Bass Rig","OSTF Acoustic Guitar Rig","OSTF Delay"} <= covered <= catalog.keys()
