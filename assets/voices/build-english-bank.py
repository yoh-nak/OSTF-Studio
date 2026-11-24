"""Build the English lyric resources from CMUdict and Flite 2.2's kal16 phone renderer."""
import argparse
import gzip
import pathlib
import struct
import subprocess
import tempfile
import wave


PHONES = "AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW B CH D DH F G HH JH K L M N NG P R S SH T TH V W Y Z ZH".split()


def dictionary(source, destination):
    words = {}
    for line in source.read_text(encoding="utf-8").splitlines():
        fields = line.partition("#")[0].split()
        if len(fields) < 2 or "(" in fields[0] or not fields[0].isascii():
            continue
        phones = [PHONES.index(p.rstrip("012")) + 1 + (64 if p.endswith("1") else 0)
                  for p in fields[1:]]
        words.setdefault(fields[0].lower(), bytes(phones))
    records = bytearray()
    offsets = []
    for word, phones in sorted(words.items()):
        offsets.append(len(records))
        records.extend(word.encode("ascii") + b"\0" + phones + b"\0")
    index = struct.pack("<I", len(offsets)) + struct.pack(f"<{len(offsets)}I", *offsets)
    destination.write_bytes(gzip.compress(index + records, mtime=0))


def recordings(renderer, destination):
    with tempfile.TemporaryDirectory() as directory:
        result = bytearray()
        for phone in PHONES:
            name = phone.lower()
            path = pathlib.Path(directory) / "phone.wav"
            # Surround consonants with vowels to capture their release and avoid silent stops.
            output = subprocess.check_output([str(renderer.resolve()), f"pau aa {name} aa pau", str(path)], text=True)
            segments = [line.split() for line in output.splitlines() if len(line.split()) == 2]
            with wave.open(str(path)) as wav:
                rate = wav.getframerate()
                assert wav.getsampwidth() == 2 and wav.getnchannels() == 1
                start, end = (round(float(segments[i][1]) * rate) for i in (1, 2))
                wav.setpos(start)
                pcm = wav.readframes(end - start)
            assert len(pcm) > 128, name
            result.extend(struct.pack("<II", rate, len(pcm)//2) + pcm)
        destination.write_bytes(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("cmudict", type=pathlib.Path)
    parser.add_argument("renderer", type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).parent
    dictionary(args.cmudict, root / "english-lexicon.gz")
    recordings(args.renderer, root / "english-phones.bin")
