"""Generate the original OSTF Studio demo songs with Python 3 (stdlib only)."""

import hashlib
import json
from pathlib import Path
from zipfile import ZipFile, ZipInfo


PPQ = 960
DESTINATION = Path(__file__).resolve().parent
PITCH_CLASSES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def pitch_class(pitch):
    name = PITCH_CLASSES[pitch % 12]
    return {"step": name[0], "alter": int(name.endswith("#"))}


def key(pitch, mode):
    return {"state": "tonal", "tonic": pitch_class(pitch), "mode": mode,
            "label": f"{PITCH_CLASSES[pitch % 12]} {mode}"}


def note(identifier, start, duration, pitch, velocity, **options):
    return {"id": identifier, "start": start, "duration": duration,
            "pitch": pitch, "velocity": velocity, **options}


def track(identifier, name, role, duration, notes, **options):
    return {"id": identifier, "name": name, "role": role,
            "trackType": options.pop("trackType", "melodic"),
            "clips": [{"id": f"{identifier}-clip", "start": 0, "duration": duration, "notes": notes}],
            **options}


def voiced_progression(progression):
    """Choose compact inversions with minimum movement, keeping bass space clear."""
    previous = [58, 62, 65, 69]
    result = []
    for _, root, intervals in progression:
        ordered = sorted({(root + interval) % 12 for interval in intervals})
        candidates = []
        for start in range(54, 68):
            if start % 12 not in ordered:
                continue
            pitches = [pitch for pitch in range(start, start + 12) if pitch % 12 in ordered]
            cost = sum(abs(a - b) for a, b in zip(previous, pitches))
            cost += abs(pitches[-1] - previous[-1]) + 0.5 * abs(sum(pitches) / len(pitches) - 64)
            candidates.append((cost, pitches))
        previous = min(candidates)[1]
        result.append(previous)
    return result


def phrase(identifier, bars, measures, release=48):
    """Compose from (beat offset, MIDI pitch, beats held, velocity) tuples."""
    return [note(f"{identifier}-{bar + 1}-{i + 1}", bars[bar] + round(beat * PPQ),
                 round(held * PPQ), pitch, velocity, releaseVelocity=release)
            for bar, events in enumerate(measures)
            for i, (beat, pitch, held, velocity) in enumerate(events)]


def section_clips(song, *track_ids):
    """Expose arrangement form with named, bar-aligned clips on the timeline."""
    for lane in song["tracks"]:
        if lane["id"] not in track_ids:
            continue
        notes = lane["clips"][0]["notes"]
        lane["clips"] = []
        for section in song["timeline"]["sections"]:
            start, length = section["start"], section["duration"]
            events = [{**event, "start": event["start"] - start}
                      for event in notes if start <= event["start"] < start + length]
            if events:
                lane["clips"].append({"id": f"{lane['id']}-{section['id']}", "name": section["name"],
                                      "start": start, "duration": length, "notes": events})


def score(title, genre, labels, lengths, progression, tonic, mode, tempo, meter):
    positions = [0]
    for length in lengths:
        positions.append(positions[-1] + length)
    sections = [{"id": f"part-{index + 1}", "name": label, "start": positions[start],
                 "duration": positions[end] - positions[start]}
                for index, (label, start, end) in enumerate(labels)]
    chords = [
        {"id": f"chord-{i + 1}", "label": label, "start": positions[i], "duration": lengths[i],
         "root": pitch_class(root), "intervals": intervals,
         "voicing": {"pitches": pitches,
                     "velocities": [62 + 4 * index for index in range(len(pitches))]}}
        for i, ((label, root, intervals), pitches) in enumerate(zip(progression, voiced_progression(progression)))
    ]
    return {"format": "OSTF", "version": "5.0.0", "ppq": PPQ,
            "song": {"title": title, "genres": [{"id": genre.lower().replace(" ", "-"), "name": genre}],
                     "key": key(tonic, mode)},
            "timeline": {"sections": sections, "duration": positions[-1],
                         "tempoChanges": tempo, "meterChanges": meter},
            "harmony": {"chords": chords, "scales": []}, "tracks": [], "soundSlots": [],
            "lyrics": {"documents": [], "phrases": [], "units": [], "alignments": []}, "resources": []}, positions


def harmony_track(progression, positions, offbeat=0):
    notes = []
    for i, pitches in enumerate(voiced_progression(progression)):
        width = positions[i + 1] - positions[i]
        hits = ((offbeat, width // 2 - 120, 0), (width // 2 + offbeat, width // 2 - offbeat - 120, -8))
        for hit, (offset, length, accent) in enumerate(hits):
            for voice, pitch in enumerate(pitches):
                spread = 22 * voice
                notes.append(note(f"v{i + 1}-{hit + 1}-{voice + 1}", positions[i] + offset + spread,
                                  length - spread, pitch, 63 + accent + voice * 3, releaseVelocity=42))
    return track("keys", "Keys Voicing", "voicing", positions[-1], notes)


def bass_track(progression, positions, swing=0, style="syncopated"):
    notes = []
    for i, (_, root, intervals) in enumerate(progression):
        width = positions[i + 1] - positions[i]
        low = 36 + root % 12
        next_root = 36 + progression[(i + 1) % len(progression)][1] % 12
        approach = next_root - 1 if next_root >= low else next_root + 1
        if style == "walking":
            events = [(0, low, 780), (PPQ, low + intervals[1], 740), (2 * PPQ, approach, 670)]
        elif style == "pulse":
            events = [(beat * PPQ + 480, low + (12 if beat == 3 else 0), 330)
                      for beat in range(4) if i != 5 and (i != 4 or beat < 2)]
        else:
            events = [(0, low, 850), (width // 2 - 240, low + 7, 340),
                      (width // 2 + swing, low, 620), (width - 480, approach, 260)]
        for n, (offset, pitch, length) in enumerate(events):
            notes.append(note(f"b{i + 1}-{n + 1}", positions[i] + offset, min(length, width - offset),
                              pitch, [88, 72, 80, 68][n]))
    return track("bass", "Bass", "bassline", positions[-1], notes)


def drum_track(song, positions, techno=False):
    song["soundSlots"] = [
        {"id": "kick", "name": "Kick", "midiMapping": {"standard": "gm1", "note": 36}},
        {"id": "snare", "name": "Snare", "midiMapping": {"standard": "gm1", "note": 38}},
        {"id": "hat", "name": "Closed Hat", "midiMapping": {"standard": "gm1", "note": 42}},
        {"id": "open-hat", "name": "Open Hat", "midiMapping": {"standard": "gm1", "note": 46}},
        {"id": "clap", "name": "Clap", "midiMapping": {"standard": "gm1", "note": 39}},
        {"id": "tom", "name": "Low Tom", "midiMapping": {"standard": "gm1", "note": 45}},
    ]
    events = []
    for bar, start in enumerate(positions[:-1]):
        beats = (positions[bar + 1] - start) // PPQ
        for beat in range(beats):
            hits = [("hat", 0, 50 + 4 * (beat % 2)),
                    ("open-hat" if techno and beat % 2 else "hat", 480, 67)]
            if techno and bar not in (4, 5) or not techno and beat in (0, 2):
                hits.append(("kick", 0, 108 if techno else 98))
            if beat in (1, 3) and (not techno or bar >= 6):
                hits.append(("clap" if techno else "snare", 14, 96))
            if bar % 4 == 3 and beat == beats - 1:
                hits.append(("tom", 720, 83))
            for index, (target, offset, velocity) in enumerate(hits):
                events.append({"id": f"d{bar + 1}-{beat + 1}-{index + 1}", "start": start + beat * PPQ + offset,
                               "duration": 120, "soundSlotId": target, "velocity": velocity})
    return track("drums", "Drums", "drums", positions[-1], events, trackType="drum")


def phrasing(duration, development, minimum, maximum):
    return {"id": "phrase-direction", "type": "phrasing", "start": 0, "duration": duration,
            "parameters": {"onsetGroupsPerQuarter": {"min": minimum, "max": maximum},
                           "development": development, "adherence": "preferred"}}


def city_pop():
    progression = [("Cmaj7", 60, [0, 4, 7, 11]), ("Am7", 57, [0, 3, 7, 10]),
                   ("Dm9", 62, [0, 2, 3, 7, 10]), ("G13", 55, [0, 4, 7, 9, 10]),
                   ("Em7", 52, [0, 3, 7, 10]), ("A7", 57, [0, 4, 7, 10]),
                   ("Dm9", 62, [0, 2, 3, 7, 10]), ("Cmaj9", 60, [0, 2, 4, 7, 11])]
    song, bars = score("Neon Platforms", "City Pop", [("Station", 0, 4), ("Night Train", 4, 8)],
                      [4 * PPQ] * 8, progression, 60, "major", [{"at": 0, "bpm": 106}],
                      [{"at": 0, "numerator": 4, "denominator": 4}])
    song["harmony"]["scales"] = [{"id": "ionian", "label": "C Ionian", "start": 0, "duration": bars[-1],
                                   "tonic": pitch_class(60), "intervals": [0, 2, 4, 5, 7, 9, 11]}]
    hook = [
        [(0.5, 72, .35, 91), (1, 76, .75, 96), (2.5, 79, .4, 94), (3, 76, .7, 85)],
        [(0.5, 72, .35, 88), (1, 76, .7, 93), (2.5, 81, .4, 99), (3, 79, .65, 92)],
        [(0, 77, 1.2, 91), (1.75, 76, .4, 85), (2.5, 74, .9, 88)],
        [(0, 71, 1.25, 89), (2, 74, .45, 85), (2.5, 76, 1.1, 91)],
        [(0.5, 76, .35, 91), (1, 79, .75, 97), (2.5, 83, .4, 99), (3, 79, .65, 89)],
        [(0.5, 76, .35, 88), (1, 81, .7, 96), (2.5, 85, .4, 102), (3, 81, .65, 90)],
        [(0, 84, .85, 94), (1.5, 81, .35, 89), (2, 77, .7, 92), (3, 74, .65, 85)],
        [(0, 76, .75, 90), (1, 74, .55, 83), (2, 72, 1.75, 90)],
    ]
    song["tracks"] = [track("hook", "Electric Guitar Hook", "lead", bars[-1], phrase("hook", bars, hook)),
                      harmony_track(progression, bars, 240), bass_track(progression, bars), drum_track(song, bars)]
    section_clips(song, "hook", "keys", "bass", "drums")
    return song


def jazz_waltz():
    progression = [("Dm7", 62, [0, 3, 7, 10]), ("G7", 55, [0, 4, 7, 10]),
                   ("Cmaj7", 60, [0, 4, 7, 11]), ("A7", 57, [0, 4, 7, 10]),
                   ("Fmaj7", 53, [0, 4, 7, 11]), ("Fm6", 53, [0, 3, 7, 9]),
                   ("Cmaj7", 60, [0, 4, 7, 11]), ("C6", 60, [0, 4, 7, 9])]
    song, bars = score("Blue Hour Waltz", "Jazz Waltz", [("A1", 0, 2), ("A2", 2, 4), ("B", 4, 6), ("A3", 6, 8)],
                      [3 * PPQ] * 8, progression, 60, "major", [{"at": 0, "bpm": 132}],
                      [{"at": 0, "numerator": 3, "denominator": 4}])
    melody = phrase("horn", bars, [
        [(.667, 65, .55, 88), (1.333, 69, .52, 94), (2, 72, .85, 92)],
        [(0, 71, .7, 87), (1, 67, .43, 81), (1.667, 65, .28, 76), (2, 62, .69, 83)],
        [(.667, 64, .55, 84), (1.333, 67, .52, 90), (2, 71, .85, 91)],
        [(0, 73, .75, 95), (1, 76, .58, 97), (2, 67, .68, 85)],
        [(0, 69, .75, 90), (1, 72, .53, 96), (1.667, 76, .3, 99), (2, 77, .87, 100)],
        [(0, 77, .65, 90), (1, 75, .47, 85), (1.667, 72, .28, 82), (2, 68, .68, 80)],
        [(.667, 67, .55, 86), (1.333, 64, .52, 82), (2, 62, .76, 78)],
        [(0, 60, 2.72, 83)],
    ])
    melody[18].update(probability=0.8, velocityDeviation=-8)
    song["tracks"] = [track("horn", "Waltz Horn", "lead", bars[-1], melody,
                            directions=[phrasing(bars[-1], "contrast", 0.25, 2)]),
                      harmony_track(progression, bars, 320), bass_track(progression, bars, style="walking")]
    section_clips(song, "horn", "keys", "bass")
    visits = ["part-1", "part-2", "part-3", "part-4"]
    song["performancePlans"] = [{"id": "head-and-chorus", "name": "Head and open chorus", "mode": "orderedFinite",
        "defaultRealization": "fixed", "visits": [
            {"id": "head", "ordinal": 0, "sourceSectionIds": visits,
             "parts": [{"trackId": "horn", "realization": "fixed", "role": "lead"}]},
            {"id": "chorus", "ordinal": 1, "sourceSectionIds": visits,
             "parts": [{"trackId": "horn", "realization": "open", "role": "solo"}]},
        ]}]
    return song


def techno():
    progression = [("Em", 52, [0, 3, 7]), ("Em", 52, [0, 3, 7]),
                   ("C", 48, [0, 4, 7]), ("D", 50, [0, 4, 7])] * 3
    song, bars = score("Relay Circuit", "Techno", [("Pulse", 0, 4), ("Break", 4, 8), ("Relay", 8, 12)],
                      [4 * PPQ] * 12, progression, 52, "minor", [{"at": 0, "bpm": 128}],
                      [{"at": 0, "numerator": 4, "denominator": 4}])
    arp = [note(f"a{i + 1}-{j + 1}", bars[i] + j * 480, 230 if i >= 8 else 300,
                root + 12 + steps[[0, 2, 1, 2, 0, 1, 2, 1][j]] + (12 if i >= 8 and j in (3, 7) else 0),
                [89, 66, 78, 71, 86, 69, 80, 74][j], enabled=not (i == 5 and j == 3),
                probability=.65 if j == 7 and i in (3, 7) else 1)
           for i, (_, root, steps) in enumerate(progression) for j in range(8)
           if i != 5 or j < 4]
    song["tracks"] = [track("arp", "Relay Arpeggio", "lead", bars[-1], arp,
                            directions=[phrasing(bars[-1], "repetition", 1, 4)]),
                      bass_track(progression, bars, style="pulse"), drum_track(song, bars, techno=True)]
    section_clips(song, "arp", "bass", "drums")
    return song


def rhythm_and_blues():
    progression = [("Am9", 57, [0, 2, 3, 7, 10]), ("Fmaj9", 53, [0, 2, 4, 7, 11]),
                   ("Dm9", 62, [0, 2, 3, 7, 10]), ("E7(b9)", 52, [0, 1, 4, 7, 10]),
                   ("Fmaj9", 53, [0, 2, 4, 7, 11]), ("Dm9", 62, [0, 2, 3, 7, 10]),
                   ("E7", 52, [0, 4, 7, 10]), ("Am9", 57, [0, 2, 3, 7, 10])]
    song, bars = score("Paper Lantern Replies", "Rhythm and Blues", [("Call", 0, 4), ("Reply", 4, 8)],
                      [4 * PPQ] * 8, progression, 57, "minor", [{"at": 0, "bpm": 78}],
                      [{"at": 0, "numerator": 4, "denominator": 4}])
    song["lyrics"] = {"documents": [{"id": "lantern-text", "title": "Paper Lantern"}],
        "phrases": [{"id": "call-a", "documentId": "lantern-text", "text": "Fold a little dawn into the blue"},
                    {"id": "call-b", "documentId": "lantern-text", "text": "Leave a little light across the room"}],
        "units": [], "alignments": [], "voices": [{"id": "voice", "trackId": "voice-track", "documentId": "lantern-text"}]}
    melody = phrase("vocal", bars, [
        [(1, 69, 1.25, 88), (2.5, 72, .35, 78), (3, 76, .76, 94)],
        [(0, 77, 1.15, 96), (1.5, 76, .45, 85), (2.25, 72, 1.25, 83)],
        [(.5, 74, .65, 88), (1.5, 77, 1.15, 98), (3, 76, .65, 86)],
        [(0, 71, .85, 85), (1, 68, .5, 80), (2, 71, 1.55, 91)],
        [], [], [], [],
    ], release=35)
    words = ["Fold", "a", "lit", "tle", "dawn", "in", "to", "the", "blue"]
    for index, (word, event) in enumerate(zip(words, melody), start=1):
        unit = f"lantern-unit-{index}"
        targets = [item["id"] for item in (melody[index - 1:] if index == len(words) else [event])]
        song["lyrics"]["units"].append({"id": unit, "phraseId": "call-a", "text": word})
        song["lyrics"]["alignments"].append({"id": f"lantern-alignment-{index}", "voiceId": "voice",
                                               "unitIds": [unit], "noteIds": targets})
    answers = [
        [], [], [], [],
        [(0, 72, .65, 84), (1, 76, 1.22, 91), (2.5, 77, .7, 93)],
        [(.5, 77, .7, 89), (1.5, 81, 1.1, 94), (3, 77, .55, 85)],
        [(0, 76, .7, 84), (1, 74, .5, 82), (2, 71, 1.4, 86)],
        [(0, 72, .7, 87), (1, 69, 2.6, 88)],
    ]
    song["tracks"] = [track("voice-track", "Lead Vocal Guide", "vocal", bars[-1], melody,
        directions=[{"id": "reply-options", "type": "vocalFreedom", "start": bars[4], "duration": bars[-1] - bars[4],
                     "parameters": {"voiceId": "voice", "textPolicy": "alternatives", "melodyPolicy": "open",
                                    "candidatePhraseSets": [{"phraseIds": ["call-a"]}, {"phraseIds": ["call-b"]}],
                                    "adherence": "preferred"}}]),
        harmony_track(progression, bars, 320), bass_track(progression, bars, 160),
        track("answer", "Electric Piano Reply", "countermelody", bars[-1], phrase("answer", bars, answers)),
        drum_track(song, bars)]
    section_clips(song, "keys", "bass", "answer", "drums")
    vocal_clip = song["tracks"][0]["clips"][0]
    first_note = melody[0]["id"]
    vocal_clip["midiEvents"] = [
        {"id": "vocal-cc-start", "start": PPQ, "type": "controlChange", "controller": 11, "value": 70},
        {"id": "vocal-cc-peak", "start": PPQ + 600, "type": "controlChange", "controller": 11, "value": 105},
        {"id": "vocal-at-peak", "start": PPQ + 720, "type": "channelPressure", "value": 45},
        {"id": "vocal-poly", "start": PPQ + 480, "type": "polyPressure", "pitch": melody[0]["pitch"],
         "targetNoteId": first_note, "value": 32},
        {"id": "vocal-bend", "start": PPQ + 240, "type": "pitchBend", "value14": 7900},
        {"id": "vocal-bend-reset", "start": PPQ + 390, "type": "pitchBend", "value14": 8192},
    ]
    vocal_clip["midiEvents"].sort(key=lambda event: event["start"])
    vocal_clip["vocalPerformances"] = [{
        "id": "lantern-performance", "voiceId": "voice", "noteIds": [first_note],
        "pitchCurves": [{"id": "lantern-pitch", "targetNoteId": first_note, "unit": "cents",
                         "interpolation": "linear", "points": [{"at": PPQ, "value": -30},
                                                            {"at": PPQ + 300, "value": 0}]}],
        "expressionCurves": [{"id": "lantern-dynamics", "parameter": "dynamics", "interpolation": "linear",
                              "points": [{"at": PPQ, "value": 0.35}, {"at": PPQ + 600, "value": 0.8},
                                         {"at": PPQ + 1100, "value": 0.5}]}] + [
            {"id": f"lantern-{parameter}", "parameter": parameter, "interpolation": "linear",
             "points": [{"at": PPQ, "value": initial}, {"at": PPQ + 600, "value": peak},
                        {"at": PPQ + 1100, "value": end}]}
            for parameter, initial, peak, end in (("breathiness", .68, .28, .55), ("tension", .25, .53, .31),
                                                   ("brightness", .37, .64, .42), ("voicing", .72, .95, .60))
        ],
        "techniques": [{"id": "lantern-scoop", "targetNoteId": first_note, "kind": "scoop",
                        "start": PPQ, "duration": 300, "intensity": .35}],
        "breathEvents": [{"id": "lantern-inhale", "kind": "inhale", "start": PPQ - 260,
                          "duration": 190, "intensity": .3}],
    }]
    return song


def progressive():
    progression = [("D5", 50, [0, 7]), ("F5", 53, [0, 7]), ("G5", 55, [0, 7]), ("A5", 57, [0, 7]),
                   ("Dm", 50, [0, 3, 7]), ("Bb", 58, [0, 4, 7]), ("C", 60, [0, 4, 7]), ("D5", 50, [0, 7])]
    change = 14 * PPQ
    song, bars = score("Offset Bridges", "Progressive Rock", [("Seven Steps", 0, 4), ("Wide Bridge", 4, 8)],
                      [7 * PPQ // 2] * 4 + [4 * PPQ] * 4, progression, 50, "minor",
                      [{"at": 0, "bpm": 116}, {"at": change, "bpm": 138}],
                      [{"at": 0, "numerator": 7, "denominator": 8, "beatGroups": [2, 2, 3]},
                       {"at": change, "numerator": 4, "denominator": 4}])
    notes = []
    for i, (_, root, steps) in enumerate(progression):
        figure = [(0, 0, .4), (.5, 0, .32), (1, 7, .67), (2, 0, .37), (2.5, 12, .36), (3, 7, .44)]
        if i >= 4:
            figure = [(0, 0, 1.4), (1.5, steps[1], .4), (2, 7, .72), (3, 12, .9)]
        for j, (beat, interval, held) in enumerate(figure):
            notes.append(note(f"g{i + 1}-{j + 1}", bars[i] + round(beat * PPQ),
                              round(held * PPQ), root + interval, 99 if beat in (0, 1, 2) else 79))
    song["tracks"] = [track("guitar", "Offset Guitar", "lead", bars[-1], notes), bass_track(progression, bars)]
    section_clips(song, "guitar", "bass")
    return song


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def write_song(stem, document):
    data = json_bytes(document)
    (DESTINATION / f"{stem}.ostf").write_bytes(data)
    manifest = {"packageFormat": "OSTFP", "packageVersion": "1.0.0", "projectId": stem,
                "title": document["song"]["title"],
                "score": {"path": "score/song.ostf", "format": "OSTF", "version": "5.0.0",
                          "sha256": hashlib.sha256(data).hexdigest()}, "documents": [], "assets": [], "bindings": []}
    with ZipFile(DESTINATION / f"{stem}.ostfp", "w") as archive:
        for path, payload in (("mimetype", b"application/vnd.ostf-project+zip"),
                              ("manifest.json", json_bytes(manifest)), ("score/song.ostf", data)):
            archive.writestr(ZipInfo(path), payload)


if __name__ == "__main__":
    for name, author in (("neon-platforms", city_pop), ("blue-hour-waltz", jazz_waltz), ("relay-circuit", techno),
                         ("paper-lantern-replies", rhythm_and_blues), ("offset-bridges", progressive)):
        write_song(name, author())
