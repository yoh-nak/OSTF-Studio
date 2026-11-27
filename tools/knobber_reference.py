"""Generate a standalone Max reference patch from the supplied LEAD v1.2 device.

Run the patch in an isolated Max instance. It records the original Gen~ processors
into a RAM buffer, then writes a stereo WAV adjacent to the patch. Its temporary
audio-driver settings use MSP's override mode. No Live set or export-code licence
is needed. The VST3 test uses the same notes and factory bank.
"""
import argparse
import json
import re
from pathlib import Path


def make_patch(device, directory, bright_saw):
    source = json.loads(device.read_bytes()[32:])["patcher"]
    original = {entry["box"]["id"]: entry["box"] for entry in source["boxes"]}
    unpack = {line["patchline"]["destination"][0]: line["patchline"]["source"][1]
              for line in source["lines"] if line["patchline"]["source"][0] == "v17-preset-unpack"}
    bank = original["v180-preset-coll"]["coll_data"]["data"]
    preset = next(row["value"] for row in bank if int(row["key"]) == 234)
    controls = {}
    for name, dial in original.items():
        if not name.startswith("v6-dial-"):
            continue
        number = int(name.removeprefix("v6-dial-"))
        route = "v33-porta-pre" if number == 215 else f"v6-pre-{number}"
        parameter = original[route]["text"].split()[-1]
        value = dial["saved_attribute_attributes"]["valueof"]["parameter_initial"][0] if bright_saw else preset[unpack[name]]
        if number in (1, 5, 9):
            value -= 2
        if number in (4, 8, 12):
            value *= .01
        controls[parameter] = value
    controls.update(mono_mode=1 if bright_saw else preset[unpack["v33-mono-toggle"]], sustain=0, bend_range=2)
    if bright_saw:
        for name in ("o2_lvl", "o3_lvl", "sub_lvl", "saw_mix", "drift", "osc_slop", "envamt", "kbamt",
                     "reso", "glide", "va", "fa", "vel_amp", "nz_lvl"):
            controls[name] = 0
        controls.update(vs=100, cutoff=16000, o1_wave=.5, drive=6, sat=6)

    patch = dict(fileversion=1, appversion=source["appversion"], rect=[100, 100, 720, 530], boxes=[], lines=[])

    def box(name, text, kind="newobj", **attributes):
        i = len(patch["boxes"])
        entry = dict(id=name, maxclass=kind, patching_rect=[20+(i%5)*136, 20+(i//5)*36, 128, 22], **attributes)
        if text:
            entry["text"] = text
        patch["boxes"].append({"box": entry})

    def connect(start, end, outlet=0, inlet=0):
        patch["lines"].append({"patchline": {"source": [start, outlet], "destination": [end, inlet]}})

    def at(time, name, message, destination, inlet=0):
        timer = name + "-timer"
        box(timer, f"delay {time}")
        box(name, message, "message")
        connect("load", timer)
        connect(timer, name)
        connect(name, destination, inlet=inlet)

    box("load", "loadbang")
    box("driver", "adstatus driver")
    box("drivers", "", "umenu")
    connect("driver", "drivers")
    connect("drivers", "driver")
    at(100, "temporary-driver", "override 1", "driver")
    at(200, "driver-list", "bang", "driver")
    at(450, "offline", "symbol NonRealTime", "drivers")
    box("rate", "adstatus sr")
    at(650, "temporary-rate", "override 1", "rate")
    at(850, "48k", "48000", "rate", 1)

    # Drive only the four MIDI parameters with a sample clock. Preserve the
    # oscillator, envelope, filter, dynamics and effects expressions verbatim.
    schedule = """
History referenceFrame(0); Param fixture(0);
t = referenceFrame / samplerate;
referenceFrame = fixture > 0 ? referenceFrame + 1 : 0;
indexNote = clamp(floor((t - 0.125) / 0.375), 0, 7);
withinNote = t - 0.125 - indexNote * 0.375;
knote = t < 0.125 ? 36 : (indexNote < 1 ? 60 : (indexNote < 2 ? 67 : (indexNote < 3 ? 72 : (indexNote < 4 ? 74 : (indexNote < 5 ? 79 : (indexNote < 6 ? 84 : (indexNote < 7 ? 81 : 76)))))));
kgate = (withinNote >= 0) && (withinNote < 0.270);
ktrig = t < 0.125 ? 0 : (1 - (indexNote % 2)); kvel = 100;
"""
    if bright_saw:
        schedule = "Param fixture(0); knote = 105; kvel = 100; kgate = fixture; ktrig = fixture;\n"
    for name, key in (("voice", "obj-8"), ("drive", "v115-global-amp-gen"), ("fx", "v113-postfx-gen")):
        node = original[key]["patcher"]
        codebox = next(b["box"] for b in node["boxes"] if "code" in b["box"])
        code = codebox["code"]
        params = set(re.findall(r"Param (\w+)\(", code))
        if name == "voice":
            code = re.sub(r"Param (knote|kgate|ktrig|kvel)\([^)]+\);", "", code)
            codebox["code"] = code.replace("sr = samplerate;", schedule+"sr = samplerate;", 1)
        box(name, original[key]["text"], patcher=node)
        at(1300, name+"-settings", ", ".join(f"{p} {v:.8f}" for p, v in controls.items() if p in params), name)

    duration = 2000 if bright_saw else 4150
    stem = "Max-Gen-A7" if bright_saw else "Max-Gen-GM-082"
    box("ram", f"buffer~ {stem} {duration} 2")
    box("rec", f"record~ {stem} 2 @loop 0")
    box("audio", "dac~")
    for ch in (0, 1):
        connect("voice", "drive", ch, ch)
        connect("drive", "fx", ch, ch)
        connect("voice", "rec", ch, ch)
        connect("fx", "rec", ch, ch)
    at(1900, "clear", "clear", "ram")
    at(2700, "start", "1", "audio")
    at(4150, "capture", "1", "rec")
    box("begin-notes", "fixture 1", "message")
    connect("capture-timer", "begin-notes")
    connect("begin-notes", "voice")
    at(24000, "end-capture", "0", "rec")
    at(25000, "end-audio", "stop", "audio")
    at(27000, "write-wave", f'writewave "{(directory/(stem+".wav")).as_posix()}"', "ram")
    return stem, {"patcher": patch}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("amxd", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bright-saw", action="store_true")
    args = parser.parse_args()
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    stem, patch = make_patch(args.amxd, directory, args.bright_saw)
    (directory/(stem+".maxpat")).write_text(json.dumps(patch), encoding="utf-8")
