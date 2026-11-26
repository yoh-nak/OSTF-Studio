"""Build the original OSTF Studio preview SoundFont. No external audio assets."""
from json import loads
from math import exp, pi, sin, tanh
from pathlib import Path
from random import Random
from struct import pack

RATE = 44000
# Harmonic balance, attack, decay, sustain, release and shaped harmonics for 16 GM families.
FAMILIES = loads(Path(__file__).with_name("GMFamilies.json").read_text(encoding="utf-8"))


def chunk(name, data):
    return name + pack("<I", len(data)) + data + bytes(len(data) % 2)


def label(text):
    return text.encode("ascii")[:20].ljust(20, b"\0")


def tone(harmonics, period):
    # A seamless eight-cycle waveform, rooted at A4.
    scale = sum(harmonics)
    return [sum(gain * sin(2 * pi * i * (n + 1) / period) for n, gain in enumerate(harmonics)) / scale
            for i in range(period * 8)]


def drum(kind):
    seconds = {"kick": .21, "snare": .19, "hat": .08, "open": .32, "tom": .25, "crash": .54, "ride": .42, "clap": .18}[kind]
    rng = Random(kind)
    frames = []
    previous = 0
    for i in range(round(seconds * RATE)):
        t = i / RATE
        noise = rng.uniform(-1, 1)
        high = noise - previous
        previous = .55 * previous + .45 * noise
        if kind in ("kick", "tom"):
            hz, bend = (48, 82) if kind == "kick" else (105, 94)
            wave = .86 * sin(2*pi*(hz*t + bend*.018*(1-exp(-t/.018)))) + .15*noise*exp(-t/.009)
        elif kind in ("snare", "clap"):
            wave = (.58 * noise + .36 * sin(2*pi*179*t)*exp(-t/.048))
            if kind == "clap":
                wave *= .35 + .65 * (sin(2*pi*36*t) ** 6)
        else:
            metal = sum(sin(2*pi*f*t) for f in (5117, 6937, 8429, 9871)) / 4
            wave = .52*high + .34*metal
        frames.append(tanh(wave) * exp(-4*t/seconds) * min(1, t/.0005))
    return frames


def create_bank(path):
    pcm, shdr, phdr, pbag, pgen, inst, ibag, igen = ([] for _ in range(8))
    def sample(name, frames, root=69, sample_rate=RATE):
        start = len(pcm)
        pcm.extend(round(max(-1, min(1, value)) * 29000) for value in frames)
        end = len(pcm)
        pcm.extend([0] * 46)
        shdr.append(pack("<20sIIIIIBbHH", label(name), start, end, start, end, sample_rate, root, 0, 0, 1))
        return len(shdr)-1

    def zone(generators):
        ibag.append(pack("<HH", len(igen), 0))
        igen.extend(pack("<Hh", op, value) for op, value in generators)

    def preset(name, program, bank, instrument):
        phdr.append(pack("<20sHHHIII", label(name), program, bank, len(pbag), 0, 0, 0))
        pbag.append(pack("<HH", len(pgen), 0))
        pgen.append(pack("<Hh", 41, instrument))

    for family, (name, harmonics, attack, decay, sustain, release) in enumerate(FAMILIES):
        for variation in range(8):
            # Within a family, vary brightness and the envelope continuously.
            tilt = (variation - 3.5) * .045
            shape = tuple(gain * (1 + tilt) ** n for n, gain in enumerate(harmonics))
            iid = len(inst)
            inst.append(pack("<20sH", label(name), len(ibag)))
            for lowest, highest, root in ((0, 59, 48), (60, 83, 69), (84, 107, 88), (108, 127, 108)):
                frequency = 440 * 2 ** ((root - 69) / 12)
                ceiling = 440 * 2 ** ((highest - 69) / 12)
                limit = max(1, int(17000 / ceiling))
                # Keep upper registers clean while retaining more harmonic variation in the bass.
                period = max(3, int(RATE / frequency))
                sid = sample(name, tone(shape[:limit], period), root, round(frequency * period))
                zone([(43, lowest | (highest << 8)), (34, attack + variation*65),
                      (36, decay - variation*40), (37, sustain), (38, release + variation*60),
                      (54, 1), (53, sid)])
            preset(f"{name} {variation+1}", family*8+variation, 0, iid)

    percussion = {name: sample(name, drum(name)) for name in ("kick", "snare", "hat", "open", "tom", "crash", "ride", "clap")}
    hits = {35:"kick",36:"kick",37:"clap",38:"snare",39:"clap",40:"snare",42:"hat",44:"hat",46:"open",
            49:"crash",51:"ride",52:"crash",53:"ride",55:"crash",57:"crash",59:"ride"}
    kit = len(inst)
    inst.append(pack("<20sH", label("Studio Drums"), len(ibag)))
    for key in range(35, 82):
        sid = percussion[hits.get(key, "tom" if key <= 50 else "clap")]
        zone([(43, key | (key << 8)), (38, -1800), (58, key), (53, sid)])
    preset("Studio Drums", 0, 128, kit)
    phdr.append(pack("<20sHHHIII", label("EOP"), 0, 0, len(pbag), 0, 0, 0))
    pbag.append(pack("<HH", len(pgen), 0))
    pgen.append(bytes(4))
    inst.append(pack("<20sH", label("EOI"), len(ibag)))
    ibag.append(pack("<HH", len(igen), 0))
    igen.append(bytes(4))
    shdr.append(pack("<20sIIIIIBbHH", label("EOS"), 0, 0, 0, 0, 0, 0, 0, 0, 0))
    info = b"INFO" + chunk(b"ifil", pack("<HH", 2, 1)) + chunk(b"isng", b"EMU8000\0") + chunk(b"INAM", b"OSTF Studio Preview\0")
    sample_data = b"sdta" + chunk(b"smpl", pack("<" + "h"*len(pcm), *pcm))
    hydra = b"pdta" + b"".join(chunk(name, b"".join(items)) for name, items in (
        (b"phdr",phdr),(b"pbag",pbag),(b"pmod",[bytes(10)]),(b"pgen",pgen),
        (b"inst",inst),(b"ibag",ibag),(b"imod",[bytes(10)]),(b"igen",igen),(b"shdr",shdr)))
    path.write_bytes(chunk(b"RIFF", b"sfbk" + chunk(b"LIST",info) + chunk(b"LIST",sample_data) + chunk(b"LIST",hydra)))


if __name__ == "__main__":
    create_bank(Path(__file__).resolve().parent / "StudioPreview.sf2")
