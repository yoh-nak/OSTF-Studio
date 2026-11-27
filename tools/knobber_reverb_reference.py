"""Make a standalone Max Hall impulse fixture from SUPER KNOBBER REVERB v1.0.

Open the resulting maxpat in a separate Max instance. It writes 1.1 seconds of
stereo wet output plus an input marker on channel three. No Live set is needed.
"""
import argparse
import json
from pathlib import Path


def hall_reference(device, directory):
    patch = json.loads(device.read_bytes()[32:])["patcher"]
    patch["openinpresentation"] = 0
    patch["rect"] = [100, 100, 800, 530]
    patch["boxes"] = [entry for entry in patch["boxes"]
                      if entry["box"]["id"] not in ("io-in", "io-out")]
    for connection in patch["lines"]:
        line = connection["patchline"]
        source, outlet = line["source"]
        if source == "io-in":
            line["source"] = [("ref-left", "ref-right")[outlet], 0]
        if line["destination"][0] == "io-out":
            line["destination"][0] = "ref-record"

    def box(name, text, kind="newobj"):
        entry = dict(id="ref-" + name, maxclass=kind, patching_rect=[20, 20, 160, 22])
        if text:
            entry["text"] = text
        patch["boxes"].append({"box": entry})

    def wire(source, target, outlet=0, inlet=0):
        patch["lines"].append({"patchline": {"source": [source, outlet],
                                             "destination": [target, inlet]}})

    def at(ms, name, message, target, inlet=0):
        box(name + "-timer", f"delay {ms}")
        box(name, message, "message")
        wire("ref-load", "ref-" + name + "-timer")
        wire("ref-" + name + "-timer", "ref-" + name)
        wire("ref-" + name, target, inlet=inlet)

    box("load", "loadbang")
    box("driver", "adstatus driver")
    box("drivers", "", "umenu")
    wire("ref-driver", "ref-drivers")
    wire("ref-drivers", "ref-driver")
    at(100, "override", "override 1", "ref-driver")
    at(200, "list", "bang", "ref-driver")
    at(450, "offline", "symbol NonRealTime", "ref-drivers")
    box("rate", "adstatus sr")
    at(650, "rate-override", "override 1", "ref-rate")
    at(850, "rate-set", "48000", "ref-rate", 1)
    box("dac", "dac~")
    box("buffer", "buffer~ knobReverbReference 1100 3")
    box("record", "record~ knobReverbReference 3 @loop 0")
    at(1500, "hall", "9", "type-menu")
    at(2050, "wet", "100", "dial-63")
    at(2700, "start", "1", "ref-dac")
    at(3100, "buffer-rate", "sr 48000", "ref-buffer")
    at(3250, "buffer-length", "sizeinsamps 52800", "ref-buffer")

    box("click", "click~")
    box("left", "*~ 0.8")
    box("right", "*~ 0.4")
    wire("ref-click", "ref-left")
    wire("ref-click", "ref-right")
    wire("ref-click", "ref-record", inlet=2)
    box("capture-timer", "delay 4800")
    box("capture", "t b 1")
    wire("ref-load", "ref-capture-timer")
    wire("ref-capture-timer", "ref-capture")
    wire("ref-capture", "ref-record", outlet=1)
    wire("ref-capture", "ref-click")
    at(20000, "stop-record", "0", "ref-record")
    at(21000, "stop-dsp", "stop", "ref-dac")
    at(23000, "save", f'writewave "{(directory / "Max-Hall-trace.wav").as_posix()}"', "ref-buffer")
    return {"patcher": patch}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    folder = args.output_directory.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "Max-Hall-reference.maxpat").write_text(
        json.dumps(hall_reference(args.device, folder)), encoding="utf-8")
