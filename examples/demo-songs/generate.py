"""Generate the original OSTF Studio demo songs with Python 3 (stdlib only)."""

import hashlib
import json
from copy import deepcopy
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


def harmony_track(progression, positions, name, offbeat=0):
    notes = []
    for i, pitches in enumerate(voiced_progression(progression)):
        width = positions[i + 1] - positions[i]
        hits = ((offbeat, width // 2 - 120, 0), (width // 2 + offbeat, width // 2 - offbeat - 120, -8))
        for hit, (offset, length, accent) in enumerate(hits):
            for voice, pitch in enumerate(pitches):
                spread = 22 * voice
                notes.append(note(f"v{i + 1}-{hit + 1}-{voice + 1}", positions[i] + offset + spread,
                                  length - spread, pitch, 63 + accent + voice * 3, releaseVelocity=42))
    return track("keys", name, "accompaniment", positions[-1], notes)


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
                      harmony_track(progression, bars, "Electric Piano Comp", 240), bass_track(progression, bars), drum_track(song, bars)]
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
                      harmony_track(progression, bars, "Acoustic Piano Comp", 320), bass_track(progression, bars, style="walking")]
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
    answers = [
        [], [], [], [],
        [(0, 72, .65, 84), (1, 76, 1.22, 91), (2.5, 77, .7, 93)],
        [(.5, 77, .7, 89), (1.5, 81, 1.1, 94), (3, 77, .55, 85)],
        [(0, 76, .7, 84), (1, 74, .5, 82), (2, 71, 1.4, 86)],
        [(0, 72, .7, 87), (1, 69, 2.6, 88)],
    ]
    song["tracks"] = [track("voice-track", "Lead Vocal Guide", "vocal", bars[-1], []),
        harmony_track(progression, bars, "Warm Electric Piano", 320), bass_track(progression, bars, 160),
        track("answer", "Electric Piano Reply", "countermelody", bars[-1], phrase("answer", bars, answers)),
        drum_track(song, bars)]
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


def arrangement(song, form, layers):
    """Expand the composed motifs into named sections with distinct orchestration."""
    source = deepcopy(song)
    song["performancePlans"] = []
    song["timeline"] = {"sections": [], "meterChanges": [], "tempoChanges": []}
    song["harmony"] = {"chords": [], "scales": []}
    tracks = {lane["id"]: {**lane, "clips": [], "directions": []} for lane in source["tracks"]}
    for lane_id, title, role, style in layers:
        tracks[lane_id] = {"id": lane_id, "name": title, "role": role, "trackType": "melodic", "clips": []}
    song["tracks"] = list(tracks.values())
    original_chords = source["harmony"]["chords"]
    source_events = {
        lane["id"]: [(clip["start"] + event["start"], event)
                     for clip in lane["clips"] for event in clip["notes"]]
        for lane in source["tracks"]
    }
    tick = 0
    for number, (name, bars, enabled, intensity) in enumerate(form, start=1):
        section = {"id": f"part-{number}", "name": name, "start": tick, "duration": 0}
        events = {lane_id: [] for lane_id in enabled.split()}
        for position, source_bar in enumerate(bars):
            chord = original_chords[source_bar]
            start, length = chord["start"], chord["duration"]
            for field in ("meterChanges", "tempoChanges"):
                current = next(event for event in reversed(source["timeline"][field]) if event["at"] <= start)
                previous = song["timeline"][field]
                if not previous or {**previous[-1], "at": 0} != {**current, "at": 0}:
                    previous.append({**current, "at": tick})
            chord_id = f"chord-{len(song['harmony']['chords']) + 1}"
            song["harmony"]["chords"].append({**chord, "id": chord_id, "start": tick})
            for lane_id, output in events.items():
                for at, original in source_events.get(lane_id, ()):
                    if start <= at < start + length:
                        velocity = original["velocity"] + intensity
                        deviation = original.get("velocityDeviation", 0)
                        velocity = max(1, 1 - deviation, min(110, 127 - deviation, velocity))
                        output.append({**original, "id": f"{section['id']}-{original['id']}-{position + 1}",
                                       "start": tick - section["start"] + at - start,
                                       "duration": min(original["duration"], start + length - at),
                                       "velocity": velocity})
            for lane_id, _, _, style in layers:
                if lane_id not in events:
                    continue
                pitches = chord["voicing"]["pitches"]
                slots = layer_notes(style, pitches, length, position, len(bars))
                for offset, held, pitch, velocity in slots:
                    output = events[lane_id]
                    output.append(note(f"{section['id']}-{lane_id}-{len(output) + 1}",
                                       tick - section["start"] + offset, min(held, length - offset),
                                       pitch, velocity + intensity))
            tick += length
        section["duration"] = tick - section["start"]
        song["timeline"]["sections"].append(section)
        for lane_id, output in events.items():
            if output:
                tracks[lane_id]["clips"].append({"id": f"{lane_id}-{section['id']}", "name": name,
                    "start": section["start"], "duration": section["duration"],
                    "notes": sorted(output, key=lambda event: event["start"])})
    song["timeline"]["duration"] = tick
    song["harmony"]["scales"] = [{**scale, "duration": tick} for scale in source["harmony"]["scales"]]
    return song


def layer_notes(style, tones, length, bar, section_bars):
    if style == "pad":
        return [(0, length - 160, pitch + 12, 56 + i * 3) for i, pitch in enumerate(tones[-3:])]
    if style == "stabs":
        return [(offset, 280, pitch, 76 - i * 3) for i, pitch in enumerate(tones[-3:])
                for offset in (PPQ // 2, length - PPQ // 2)]
    if style == "counter":
        return [(length // 2, 410, tones[-1] + 12, 82), (length - PPQ // 2, 320, tones[-2] + 12, 70)] if bar % 2 else []
    if style == "pulse":
        return [(offset, 166, tones[(i + bar) % len(tones)] + 12, 52 + (i % 3) * 5)
                for i, offset in enumerate(range(PPQ // 4, length, PPQ // 2))]
    if style == "bass":
        return [(0, length - 250, tones[0] - 12, 76)] if bar % 2 == 0 else [(PPQ, length - PPQ - 180, tones[0] - 12, 67)]
    if style == "climax":
        return [(0, length - 230, tones[-1] + 12, 88)] if bar % 4 < 3 else [
            (0, length // 2 - 120, tones[-2] + 12, 94), (length // 2, length // 2 - 160, tones[-1] + 12, 93)]
    if style == "fill":
        return [(length - PPQ + i * PPQ // 4, 135, tones[-1 - i % len(tones)] + 12, 68 + i * 4)
                for i in range(4)] if bar % 4 == 3 and bar + 1 < section_bars else []
    raise ValueError(f"Unknown accompaniment style: {style}")


def full_city_pop():
    ostinato = [0, 1, 2, 3]
    chorus = [4, 5, 6, 7]
    return arrangement(city_pop(), [
        ("Intro", ostinato, "keys bass pad", -11),
        ("Verse 1", ostinato * 2, "hook keys bass drums comp", -7),
        ("Pre-Chorus", chorus, "keys bass drums comp strings", -2),
        ("Chorus", (ostinato + chorus) * 2, "hook keys bass drums pad comp brass strings", 3),
        ("Verse 2", ostinato * 2, "hook keys bass drums comp brass", -4),
        ("Bridge", [2, 3, 0, 1, 4, 5, 6, 3], "keys bass pad strings", -6),
        ("Guitar Break", chorus * 2, "hook keys bass drums comp brass", 0),
        ("Final Chorus", (ostinato + chorus) * 2, "hook keys bass drums pad comp brass strings", 6),
        ("Outro", chorus, "keys bass pad strings", -10),
    ], [("pad", "Analog Pad", "texture", "pad"), ("comp", "Muted Rhythm Guitar", "rhythm", "stabs"),
        ("brass", "Brass Answers", "countermelody", "counter"), ("strings", "String Lift", "texture", "climax")])


def full_waltz():
    head = list(range(8))
    turnaround = [4, 5, 2, 3, 0, 1, 6, 7]
    song = jazz_waltz()
    bars = [i * 3 * PPQ for i in range(9)]
    song["soundSlots"] = [{"id": "ride", "name": "Ride Cymbal", "midiMapping": {"standard": "gm1", "note": 51}},
                          {"id": "brush", "name": "Brush Accent", "midiMapping": {"standard": "gm1", "note": 38}}]
    drums = []
    for bar, start in enumerate(bars[:-1]):
        for i, beat in enumerate((0, 1, 1.667, 2)):
            drums.append({"id": f"ride-{bar + 1}-{i + 1}", "start": start + round(beat * PPQ),
                          "duration": 104, "soundSlotId": "ride", "velocity": [68, 53, 48, 62][i]})
        drums.append({"id": f"brush-{bar + 1}", "start": start + PPQ, "duration": 130,
                      "soundSlotId": "brush", "velocity": 59 if bar % 2 else 43})
    song["tracks"].append(track("ride-kit", "Ride and Brushes", "drums", bars[-1], drums, trackType="drum"))
    arrangement(song, [
        ("Piano Introduction", head[:4] * 2, "keys bass vibes", -10),
        ("Head", head * 2, "horn keys bass ride-kit vibes", -4),
        ("Piano Chorus", turnaround * 3, "keys bass ride-kit vibes organ", 0),
        ("Horn Chorus", head * 3, "horn keys bass ride-kit guitar organ", 2),
        ("Trading Fours", (turnaround + head) * 2, "horn bass ride-kit guitar vibes", 0),
        ("Head Out", head * 2, "horn keys bass ride-kit vibes organ", 3),
        ("Tag", [4, 5, 6, 7, 4, 5, 6, 7], "horn keys bass guitar organ", -9),
    ], [("vibes", "Vibraphone Replies", "countermelody", "counter"),
        ("organ", "Organ Guide Tones", "texture", "pad"), ("guitar", "Jazz Guitar Comp", "rhythm", "stabs")])
    sections = song["timeline"]["sections"]
    song["tracks"][0]["directions"] = [phrasing(song["timeline"]["duration"], "contrast", 0.25, 2)]
    song["performancePlans"] = [{"id": "jazz-choruses", "name": "Fixed head, open horn chorus", "mode": "orderedFinite",
        "defaultRealization": "fixed", "visits": [
            {"id": f"visit-{i + 1}", "ordinal": i, "sourceSectionIds": [part["id"]],
             "parts": [{"trackId": "horn", "realization": "open", "role": "solo"}] if i == 3 else []}
            for i, part in enumerate(sections)]}]
    return song


def full_techno():
    pulse = [0, 1, 2, 3]
    build = [4, 5, 6, 7]
    drop = [8, 9, 10, 11]
    song = arrangement(techno(), [
        ("DJ Intro", pulse * 2, "drums sub noise arp", -9),
        ("Groove", pulse * 4, "drums bass sub arp chord", -4),
        ("Breakdown", build * 3, "arp atmos chord noise", -10),
        ("Build", build * 2, "drums arp noise atmos perc", 0),
        ("First Drop", drop * 4, "drums bass sub arp chord perc", 3),
        ("Second Break", build * 2, "arp chord atmos noise", -6),
        ("Final Drop", drop * 5, "drums bass sub arp chord atmos perc noise", 7),
        ("DJ Outro", pulse * 2, "drums sub noise", -9),
    ], [("sub", "Sub Bass Sustain", "bassline", "bass"), ("chord", "Chord Stabs", "rhythm", "stabs"),
        ("atmos", "Atmosphere Synth", "texture", "pad"), ("perc", "Metallic Sequencer", "rhythm", "pulse"),
        ("noise", "Transition Synth", "texture", "fill")])
    song["tracks"][0]["directions"] = [phrasing(song["timeline"]["duration"], "repetition", 1, 4)]
    return song


def full_progressive():
    odd = [0, 1, 2, 3]
    straight = [4, 5, 6, 7]
    song = progressive()
    song["soundSlots"] = [{"id": slot, "name": name, "midiMapping": {"standard": "gm1", "note": pitch}}
                          for slot, name, pitch in (("kick", "Kick", 36), ("snare", "Snare", 38),
                                                    ("hat", "Closed Hat", 42), ("crash", "Crash", 49))]
    drums = []
    for bar, chord in enumerate(song["harmony"]["chords"]):
        odd_meter = bar < 4
        accents = (0, 2, 4) if odd_meter else (0, 4)
        backbeats = (2, 6)
        for step in range(7 if odd_meter else 8):
            for slot, velocity in ([("hat", 75 if step in accents else 55)]
                                   + ([("kick", 104)] if step in accents else [])
                                   + ([("snare", 102)] if step in backbeats else [])
                                   + ([("crash", 98)] if step == 0 and bar in (0, 4) else [])):
                drums.append({"id": f"rock-{bar + 1}-{step + 1}-{slot}", "start": chord["start"] + step * PPQ // 2,
                              "duration": 170, "soundSlotId": slot, "velocity": velocity})
    song["tracks"].append(track("kit", "Rock Drum Kit", "drums", song["timeline"]["duration"], drums, trackType="drum"))
    return arrangement(song, [
        ("Seven Steps", odd * 3, "guitar bass organ pulse kit", -4),
        ("Wide Theme", straight * 4, "guitar bass rhythm organ strings kit", 1),
        ("Odd-Meter Interlude", odd * 3, "bass organ pulse lead", -7),
        ("Development", (odd + straight) * 3, "guitar bass rhythm organ strings lead pulse kit", 3),
        ("Recapitulation", straight * 3, "guitar bass rhythm organ strings lead kit", 7),
        ("Coda", straight, "guitar bass organ strings", -3),
    ], [("rhythm", "Rhythm Guitar", "rhythm", "stabs"), ("organ", "Organ Chords", "accompaniment", "pad"),
        ("strings", "Mellotron Strings", "texture", "climax"), ("lead", "Lead Guitar Replies", "countermelody", "counter"),
        ("pulse", "Clockwork Synth", "rhythm", "pulse")])


PAPER_LANTERN_LYRICS = {
    "Verse 1": (
        "Rain counts the change in my coat",
        "Trains pull the night down the line",
        "I cup the last of the glow",
        "And keep your side of the light",
    ),
    "Pre-Chorus": (
        "If the street lights all go out",
        "Look for the flame in my hands",
    ),
    "Chorus": (
        "Fold a lit-tle dawn in-to the blue",
        "Leave a lit-tle light a-cross the room",
        "When the ci-ty falls a-sleep a-gain",
        "My pa-per lan-tern shines for you",
    ),
    "Verse 2": (
        "Steam on the glass hides the road",
        "I trace the shape of your name",
        "Dawn puts a hand on the roofs",
        "But I still shel-ter the flame",
    ),
    "Bridge": (
        "One breath can bend the light",
        "One word can lift the shade",
        "Let all the doors swing wide",
        "I will not turn a-way",
    ),
}
PAPER_LANTERN_LYRICS["Final Chorus"] = PAPER_LANTERN_LYRICS["Chorus"] + (
    "Let the morn-ing find us by the door",
    "My pa-per lan-tern shines for you",
)


def sing_lantern(song):
    """Write section-local lyric phrasing, harmony and continuous expression."""
    lyrics = {"documents": [{"id": "lantern-text", "title": "Paper Lantern Replies"},
                            {"id": "lantern-backing-text", "title": "Wordless backing vocals"}],
              "phrases": [], "units": [], "alignments": [],
              "voices": [{"id": "voice", "trackId": "voice-track", "documentId": "lantern-text"},
                         {"id": "backing-voice", "trackId": "backing", "documentId": "lantern-backing-text"}]}
    song["lyrics"] = lyrics
    lead = song["tracks"][0]
    backing = next(lane for lane in song["tracks"] if lane["id"] == "backing")
    chords = song["harmony"]["chords"]
    for section in song["timeline"]["sections"]:
        name = section["name"]
        if name not in PAPER_LANTERN_LYRICS:
            continue
        refrain = "Chorus" in name and name != "Pre-Chorus"
        prefix = f"vocal-{section['id']}"
        lead_notes, pitch_curves, techniques, breaths, controllers, backing_notes = [], [], [], [], [], []
        parameters = {name: [] for name in ("dynamics", "breathiness", "tension", "brightness", "voicing")}
        for line, text in enumerate(PAPER_LANTERN_LYRICS[name]):
            line_id = f"{prefix}-line-{line + 1}"
            words = text.split()
            syllables = [syllable for word in words for syllable in word.split("-")]
            lyrics["phrases"].append({"id": line_id, "documentId": "lantern-text", "text": text.replace("-", "")})
            offsets = ([.5, 1, 1.5, 2.25, 3, 3.5, 4.25, 5, 5.5, 6.25, 6.75, 7.25] if refrain else
                       [.5, 1.25, 2, 2.5, 3, 3.5, 4.5, 5.25, 5.75, 6.25, 6.75, 7.25])
            shape = ([1, 2, 3, 2, 1, 2, 4, 3, 2, 1, 1, 0] if refrain else
                     [1, 2, 1, 0, 2, 1, 2, 3, 2, 1, 1, 0])
            if line % 2:
                offsets = [beat + .25 if i % 3 == 0 and beat <= 6.75 else beat
                           for i, beat in enumerate(offsets)]
                shape = [max(0, degree - (i % 2)) for i, degree in enumerate(shape)]
            if name == "Bridge":
                shape = [min(5, degree + 1) for degree in shape]
            start = line * 8 * PPQ
            breaths.append({"id": f"{line_id}-inhale", "start": start + 80, "duration": 245,
                            "kind": "inhale", "intensity": .42 if refrain else .26})
            for i, syllable in enumerate(syllables):
                offset = round(offsets[i] * PPQ)
                following = round((offsets[i + 1] if i + 1 < len(syllables) else 7.8) * PPQ)
                held = following - offset - (60 if i + 1 < len(syllables) else 180)
                at = section["start"] + start + offset
                chord = chords[at // (4 * PPQ)]
                pool = sorted({pitch for tone in chord["voicing"]["pitches"] for pitch in range(62, 84)
                               if pitch % 12 == tone % 12})
                tone = min(len(pool) - 1, shape[i] + (2 if refrain else 1))
                if i + 1 == len(syllables):
                    tone = min(len(pool) - 1, 2 if refrain else 1)
                note_id = f"{line_id}-note-{i + 1}"
                unit_id = f"{line_id}-unit-{i + 1}"
                vel = (89 if refrain else 76) + [3, -2, 6, 0][i % 4]
                lead_notes.append(note(note_id, start + offset, held, pool[tone], vel, releaseVelocity=48 if refrain else 33))
                lyrics["units"].append({"id": unit_id, "phraseId": line_id, "text": syllable, "start": at, "duration": held})
                lyrics["alignments"].append({"id": f"{unit_id}-alignment", "voiceId": "voice",
                                             "unitIds": [unit_id], "noteIds": [note_id]})
                if i == 0 or i + 1 == len(syllables):
                    attack = min(200, held // 3)
                    pitch_curves.append({"id": f"{note_id}-pitch", "targetNoteId": note_id, "unit": "cents",
                        "interpolation": "linear", "points": [{"at": start + offset, "value": -24 if i == 0 else -12},
                                                             {"at": start + offset + attack, "value": 0}]})
                    techniques.append({"id": f"{note_id}-technique", "start": start + offset, "duration": held,
                        "kind": "vibrato" if i > 0 and held > PPQ else "scoop", "targetNoteId": note_id,
                        "intensity": .44 if refrain else .29})
                    controllers.extend([
                        {"id": f"{note_id}-cc", "start": start + offset, "type": "controlChange",
                         "controller": 11, "value": 104 if refrain else 78},
                        {"id": f"{note_id}-pressure", "start": start + offset + attack, "type": "polyPressure",
                         "targetNoteId": note_id, "pitch": pool[tone], "value": 45 if refrain else 29},
                        {"id": f"{note_id}-aftertouch", "start": start + offset + attack, "type": "channelPressure",
                         "value": 42 if refrain else 23},
                        {"id": f"{note_id}-bend", "start": start + offset + 1, "type": "pitchBend", "value14": 8050},
                        {"id": f"{note_id}-bend-reset", "start": start + offset + attack, "type": "pitchBend", "value14": 8192},
                    ])
                if refrain and i + 1 == len(syllables):
                    harmony = note(f"{line_id}-backing", start + offset, held, pool[max(0, tone - 2)], vel - 21, releaseVelocity=32)
                    backing_notes.append(harmony)
                    lyric = f"{line_id}-ah"
                    lyrics["phrases"].append({"id": lyric, "documentId": "lantern-backing-text", "text": "Ah"})
                    lyrics["units"].append({"id": f"{lyric}-unit", "phraseId": lyric, "text": "Ah", "start": at, "duration": held})
                    lyrics["alignments"].append({"id": f"{lyric}-alignment", "voiceId": "backing-voice",
                                                "unitIds": [f"{lyric}-unit"], "noteIds": [harmony["id"]]})
            for parameter, values in {
                "dynamics": (.35, .88, .52) if refrain else (.30, .65, .38),
                "breathiness": (.65, .27, .56) if refrain else (.58, .38, .62),
                "tension": (.26, .71, .35) if refrain else (.20, .42, .27),
                "brightness": (.42, .77, .55) if refrain else (.28, .53, .38),
                "voicing": (.72, .97, .64) if refrain else (.58, .89, .57),
            }.items():
                for beat, value in zip((.5, 3.5, 7.5), values):
                    parameters[parameter].append({"at": start + round(beat * PPQ), "value": value})
        lead["clips"].append({"id": prefix, "name": name, "start": section["start"], "duration": section["duration"],
            "notes": lead_notes, "midiEvents": sorted(controllers, key=lambda event: event["start"]), "vocalPerformances": [{
                "id": f"{prefix}-performance", "voiceId": "voice", "noteIds": [event["id"] for event in lead_notes],
                "pitchCurves": pitch_curves, "techniques": techniques, "breathEvents": breaths,
                "expressionCurves": [{"id": f"{prefix}-{parameter}", "parameter": parameter,
                     "interpolation": "linear", "points": points} for parameter, points in parameters.items()],
            }]})
        if backing_notes:
            backing["clips"].append({"id": f"{prefix}-backing", "name": name, "start": section["start"],
                                     "duration": section["duration"], "notes": backing_notes})


def full_lantern():
    verse = [0, 1, 2, 3, 0, 1, 2, 3]
    chorus = [4, 5, 6, 7, 4, 5, 6, 7]
    song = arrangement(rhythm_and_blues(), [
        ("Intro", [0, 1, 2, 3], "keys bass strings", -10),
        ("Verse 1", verse, "keys bass drums nylon", -6),
        ("Pre-Chorus", [4, 5, 2, 3], "keys bass drums nylon strings", -2),
        ("Chorus", chorus, "keys bass drums answer nylon strings", 1),
        ("Verse 2", verse, "keys bass drums nylon answer", -3),
        ("Bridge", [2, 3, 4, 5, 0, 1, 2, 3], "keys strings nylon answer", -9),
        ("Final Chorus", chorus + [4, 5, 6, 7], "keys bass drums answer nylon strings", 4),
        ("Outro", [4, 5, 6, 7], "keys answer strings", -10),
    ], [("nylon", "Nylon Guitar", "rhythm", "stabs"), ("strings", "Chamber Strings", "texture", "pad"),
        ("backing", "Backing Vocal Harmony", "vocal", "pad")])
    sing_lantern(song)
    return song


def vary_harmonic_rhythm(song):
    """Hold harmony in introductions and anticipate cadences in instrumental passages."""
    harmony = song["harmony"]
    original = harmony["chords"]
    revised = []
    altered = []
    for section_index, section in enumerate(song["timeline"]["sections"]):
        start = section["start"]
        end = start + section["duration"]
        chords = [chord for chord in original if start <= chord["start"] < end]
        instrumental = not any(track["role"] == "vocal" and any(clip["start"] == start for clip in track["clips"])
                               for track in song["tracks"])
        if instrumental and len(chords) >= 4:
            if section_index == 0 or "Break" in section["name"] or "Intro" in section["name"]:
                first, second = chords[:2]
                chords = [{**first, "duration": first["duration"] + second["duration"]}, *chords[2:]]
                altered.append((second["start"], second["start"] + second["duration"], first))
            # Lead into the last chord early, especially at turnarounds and section ends.
            approach, resolution = chords[-2:]
            anticipation = {3 * PPQ: PPQ, 7 * PPQ // 2: 3 * PPQ // 2}.get(
                approach["duration"], approach["duration"] // 2)
            change = resolution["start"] - anticipation
            chords[-2:] = [{**approach, "duration": approach["duration"] - anticipation},
                           {**resolution, "start": change, "duration": resolution["duration"] + anticipation}]
            altered.append((change, resolution["start"], resolution))
        revised.extend(chords)
    harmony["chords"] = revised
    for lane in song["tracks"]:
        if lane["role"] == "vocal":
            continue
        for clip in lane["clips"]:
            events = []
            for event in clip["notes"]:
                at = clip["start"] + event["start"]
                end = at + event["duration"]
                for start, limit, chord in altered:
                    if "pitch" not in event or end <= start or at >= limit:
                        continue
                    if at < start:
                        events.append({**event, "duration": start - at})
                        event = {**event, "id": f"{event['id']}-change", "start": start - clip["start"],
                                 "duration": end - start}
                        at = start
                    allowed = {pitch % 12 for pitch in chord["voicing"]["pitches"]}
                    pitch = event["pitch"]
                    event = {**event, "pitch": min((p for p in range(max(0, pitch-6), min(127, pitch+6)+1)
                                                   if p % 12 in allowed), key=lambda p: (abs(p-pitch), p))}
                events.append(event)
            clip["notes"] = sorted(events, key=lambda item: item["start"])


def copper_mile():
    # A palm-muted verse in E minor opens out into a G-major guitar refrain.
    harmony = [("Em", 52, [0, 3, 7]), ("G", 55, [0, 4, 7]), ("D", 50, [0, 4, 7]), ("A", 57, [0, 4, 7]),
               ("G", 55, [0, 4, 7]), ("D/F#", 50, [0, 4, 7]), ("Cadd9", 48, [0, 2, 4, 7]), ("Em", 52, [0, 3, 7])]
    song, bars = score("Copper Mile", "Rock", [], [4 * PPQ] * 8, harmony, 52, "minor",
                       [{"at": 0, "bpm": 116}], [{"at": 0, "numerator": 4, "denominator": 4}])
    melody = [
        [(0, 64, .65, 94), (.75, 67, .2, 80), (1, 69, .8, 100), (2.5, 67, .4, 87), (3, 64, .8, 91)],
        [(.5, 67, .4, 86), (1, 71, 1.35, 96), (2.5, 69, .45, 84), (3, 67, .7, 93)],
        [(0, 66, 1.4, 92), (1.5, 69, .4, 87), (2, 74, 1.4, 102)],
        [(0, 73, 1.3, 96), (1.5, 69, .4, 85), (2, 64, .8, 88), (3.5, 66, .35, 83)],
        [(0, 71, 1.45, 104), (1.5, 74, .4, 96), (2, 79, 1.8, 105)],
        [(0, 78, 1.45, 99), (1.5, 74, .4, 88), (2, 69, .9, 95), (3, 71, .7, 89)],
        [(0, 76, 1.8, 103), (2, 74, .6, 90), (2.75, 72, .2, 82), (3, 67, .85, 94)],
        [(0, 71, .85, 96), (1, 67, .85, 89), (2, 64, 1.8, 99)]]
    guitar, bass = [], []
    for bar, (_, root, _) in enumerate(harmony):
        low = 36 + root % 12
        pattern = [(0, .7), (1, .22), (1.5, .22), (2.5, .4), (3, .65)] if bar < 4 else [(0, 1.4), (1.5, .4), (2, 1.75)]
        for hit, (beat, held) in enumerate(pattern):
            for voice, offset in enumerate((0, 7, 12)):
                guitar.append(note(f"g{bar}-{hit}-{voice}", bars[bar] + int(beat * PPQ) + voice * 10,
                                   int(held * PPQ) - voice * 10, low + 12 + offset, 88 if beat == 0 else 74))
        bass_root = 42 if bar == 5 else low
        bass.append([(0, bass_root, .85, 94), (1, bass_root, .4, 80), (1.5, bass_root + 7, .4, 86),
                     (2, bass_root, .85, 91), (3, bass_root + 12, .45, 83), (3.5, bass_root + 7, .4, 76)])
    song["tracks"] = [track("lead", "Overdrive Lead", "lead", bars[-1], phrase("hook", bars, melody)),
                      track("rhythm", "Power Chords", "rhythm", bars[-1], guitar),
                      track("bass", "Pick Bass", "bassline", bars[-1], phrase("pick", bars, bass)), drum_track(song, bars)]
    verse, refrain = list(range(4)), list(range(4, 8))
    full = "lead rhythm bass drums organ guitar"
    return arrangement(song, [
        ("Riff Intro", verse, "lead rhythm drums", -6), ("Verse", verse * 2, "rhythm bass drums guitar", -7),
        ("Lift", [6, 6, 2, 3], "lead bass drums organ", -3), ("Refrain", refrain * 2, full, 3),
        ("Verse 2", verse * 2, "lead rhythm bass drums", -5), ("Guitar Solo", verse + refrain, full, 5),
        ("Half-time Break", [6, 6, 0, 3], "bass organ guitar", -9), ("Final Refrain", refrain * 4, full, 6),
        ("Tag", [4, 5, 6, 7], "lead rhythm bass drums", -3)],
        [("organ", "Hammond Bed", "texture", "pad"), ("guitar", "Clean Guitar Answer", "countermelody", "counter")])


def first_light_vector():
    harmony = [("F#m", 54, [0, 3, 7]), ("Dmaj7", 50, [0, 4, 7, 11]), ("A", 57, [0, 4, 7]), ("E", 52, [0, 4, 7]),
               ("D", 50, [0, 4, 7]), ("A", 57, [0, 4, 7]), ("E", 52, [0, 4, 7]), ("F#m", 54, [0, 3, 7])]
    song, bars = score("First Light Vector", "Trance", [], [4 * PPQ] * 8, harmony, 54, "minor",
                       [{"at": 0, "bpm": 138}], [{"at": 0, "numerator": 4, "denominator": 4}])
    lead, arp, sub = [], [], []
    contours = [(73, 76, 78), (73, 69, 74), (76, 73, 81), (80, 76, 71),
                (78, 81, 86), (85, 81, 76), (83, 80, 76), (81, 80, 78)]
    for bar, (_, root, intervals) in enumerate(harmony):
        a, b, c = contours[bar]
        lead.append([(.5, a, .45, 89), (1, b, .95, 96), (2.5, c, 1.4, 102)] if bar % 2 == 0
                    else [(0, a, 1.45, 100), (1.5, b, .4, 86), (2, c, 1.85, 96)])
        for sixteenth in range(16):
            step = (0, 1, 2, 1, 0, 2, 1, 2)[sixteenth % 8]
            arp.append(note(f"arp-{bar}-{sixteenth}", bars[bar] + sixteenth * PPQ // 4, 165,
                            root + 12 + intervals[step] + (12 if sixteenth % 8 == 7 else 0),
                            (78, 56, 69, 60)[sixteenth % 4]))
        for beat in range(4):
            sub.append(note(f"sub-{bar}-{beat}", bars[bar] + beat * PPQ + PPQ // 2, 380,
                            36 + root % 12, 87 if beat % 2 == 0 else 80))
    kit = drum_track(song, bars, techno=True)
    # A steady four-on-the-floor kick wherever the kit is active; leave the breakdown to the pad.
    hits = kit["clips"][0]["notes"]
    hits[:] = [e for e in hits if e["soundSlotId"] != "kick"]
    for beat in range(32):
        hits.append({"id": f"floor-{beat}", "start": beat * PPQ, "duration": 144, "soundSlotId": "kick", "velocity": 104})
    song["tracks"] = [track("pulse", "Horizon Saw Lead", "lead", bars[-1], phrase("sky", bars, lead)),
                      track("arp", "Prism Arpeggio", "accompaniment", bars[-1], arp),
                      track("sub", "Offbeat Sub", "bassline", bars[-1], sub), kit]
    intro, lift = list(range(4)), list(range(4, 8))
    full = "pulse arp sub drums pad chord"
    return arrangement(song, [
        ("Night Run", intro * 2, "drums sub arp", -9), ("Open Filter", intro * 4, "drums sub arp chord", -4),
        ("Suspension", (intro + lift) * 2, "pulse pad", -12), ("Signal Build", lift * 2, "arp drums chord pad", -3),
        ("Daybreak", (lift + intro) * 2, full, 4), ("Afterglow", lift * 2, "pulse arp pad", -8),
        ("Final Lift", (intro + lift) * 2, full, 6), ("Runout", intro + lift, "drums sub chord", -8)],
        [("pad", "Wide Halo Pad", "texture", "pad"), ("chord", "Gated Chord Stabs", "rhythm", "stabs")])


def production_session(song):
    programs = {
        "hook": 27, "horn": 65, "arp": 81, "voice-track": 53, "backing": 52, "keys": 4, "bass": 33,
        "answer": 5, "nylon": 24, "strings": 48, "comp": 28, "brass": 61, "pad": 89, "vibes": 11,
        "organ": 16, "guitar": 27, "rhythm": 29, "lead": 30, "pulse": 81, "sub": 38, "chord": 62,
        "atmos": 90, "perc": 12, "noise": 99, "drums": 0, "kit": 0, "ride-kit": 0,
    }
    if song["song"]["title"] == "Blue Hour Waltz":
        programs.update(keys=0, guitar=26, bass=32)
    def channel(identifier, kind, **fields):
        return {"id": identifier, "kind": kind, "audioChannels": 2, "gainDb": 0, "pan": 0,
                "mute": False, "solo": False, "soloSafe": False, "deviceIds": [], **fields}

    catalog = json.loads((DESTINATION.parents[1] / "src/plugins/effects/catalog.json").read_text(encoding="utf-8"))
    effects = {item["type"]: item for item in catalog}
    devices = []
    def insert(strip, *types):
        for kind in types:
            item = effects[kind]
            identifier = f"{strip['id']}-{kind.lower()}"
            strip["deviceIds"].append(identifier)
            devices.append({"id": identifier, "role": "effect", "enabled": True, "bypass": False,
                            "onUnavailable": "reportAndPreserve", "plugin": {
                                "format": "vst3", "pluginId": "ABCDEF019182FAEB4F737466" + item["code"].encode("ascii").hex().upper(),
                                "vendor": "OSTF Studio", "name": item["name"]}})

    # Post-fader sends in dB. Keep bass and kick near centre; give harmony and replies their own space.
    # Values are (fader dB, stereo pan, output channel, room send dB or None, inserts in signal order).
    parts = {
        "hook": (-5, -.08, "master", -16, ["Amp", "EQ"]),
        "horn": (-6, -.12, "master", -14, ["EQ", "Compressor"]),
        "arp": (-8, -.30, "bus-2", -19, ["Phaser"]),
        "voice-track": (-4, 0, "master", -13, ["EQ", "Compressor"]),
        "backing": (-12, .24, "bus-2", -11, ["Chorus"]),
        "keys": (-9, -.26, "bus-2", -16, ["Chorus"]),
        "bass": (-6, 0, "bus-1", None, ["Compressor"]),
        "answer": (-11, .32, "bus-2", -15, ["Phaser"]),
        "nylon": (-12, -.38, "bus-2", -16, ["EQ"]),
        "strings": (-13, .24, "bus-2", -12, []),
        "comp": (-11, -.38, "bus-2", -17, ["Chorus"]),
        "brass": (-10, .22, "bus-2", -15, ["EQ"]),
        "pad": (-14, .32, "bus-2", -12, ["Chorus"]),
        "vibes": (-9, .34, "bus-2", -12, []),
        "organ": (-12, -.32, "bus-2", -17, ["Phaser"]),
        "guitar": (-11, .29, "bus-2", -17, ["Amp"]),
        "rhythm": (-10, -.38, "bus-2", -20, ["Amp"]),
        "lead": (-6, .08, "master", -16, ["Distortion", "Amp"]),
        "pulse": (-9, -.22, "bus-2", -19, ["Flanger"]),
        "sub": (-7, 0, "bus-1", None, ["Distortion", "EQ"]),
        "chord": (-11, .25, "bus-2", -18, ["Chorus"]),
        "atmos": (-14, .36, "bus-2", -11, ["Phaser"]),
        "perc": (-12, .30, "bus-1", -19, ["EQ"]),
        "noise": (-15, -.32, "bus-2", -14, ["Flanger"]),
        "drums": (-7, 0, "bus-1", -22, []),
        "kit": (-7, 0, "bus-1", -23, []),
        "ride-kit": (-9, .06, "bus-1", -17, []),
    }
    if song["song"]["title"] == "Blue Hour Waltz":
        parts["keys"] = (-6, -.18, "bus-2", -14, ["EQ"])
    channels = [channel("master", "master", gainDb=-1), channel("bus-1", "bus", name="Rhythm", gainDb=-2, outputChannelId="master"),
                channel("bus-2", "bus", name="Harmony", gainDb=-1.5, outputChannelId="master"),
                channel("return-1", "bus", name="Room", gainDb=-3, outputChannelId="master")]
    for strip, kind in zip(channels, ("Limiter", "Compressor", "EQ", "Reverb")):
        insert(strip, kind)
    for index, lane in enumerate(song["tracks"], start=1):
        midi_channel = 10 if lane["trackType"] == "drum" else index
        lane["midi"] = {"mode": "singleChannel", "channel": midi_channel}
        clip = lane["clips"][0]
        clip.setdefault("midiEvents", []).insert(0, {"id": f"{lane['id']}-program", "type": "programChange",
            "start": 0, "channel": midi_channel, "program": programs[lane["id"]]})
        device = f"instrument-{index}"
        devices.append({"id": device, "role": "instrument", "enabled": True, "bypass": False,
            "onUnavailable": "reportAndPreserve", "plugin": {
                "format": "vst3", "pluginId": "ABCDEF019182FAEB4F7374664F676D31",
                "name": "OSTF GM Sound Module", "vendor": "OSTF Studio",
            }})
        level, pan, output, send, inserts = parts[lane["id"]]
        strip = channel(f"track-{index}", "track", trackId=lane["id"], outputChannelId=output,
                        deviceIds=[device], gainDb=level, pan=pan)
        if send is not None:
            strip["sends"] = [{"id": f"send-{index}-room", "targetChannelId": "return-1", "gainDb": send,
                               "preFader": False, "enabled": True}]
        insert(strip, *inserts)
        channels.append(strip)
    return {"channels": channels, "devices": devices}


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def write_song(stem, document):
    vary_harmonic_rhythm(document)
    production = production_session(document)
    document.setdefault("extensions", {})["org.ostf.studio"] = {
        "version": "1.0", "status": "current", "dependencies": {"scope": "document"}, "data": production,
    }
    data = json_bytes(document)
    digest = hashlib.sha256(data).hexdigest()
    session = json_bytes({**production, "profile": "OSTF-Production-Session", "profileVersion": "0.3.0",
                          "score": {"formatVersion": "5.0.0", "sha256": digest}})
    session_path = "session/studio.production.json"
    (DESTINATION / f"{stem}.ostf").write_bytes(data)
    manifest = {"packageFormat": "OSTFP", "packageVersion": "1.0.0", "projectId": stem,
                "title": document["song"]["title"],
                "score": {"path": "score/song.ostf", "format": "OSTF", "version": "5.0.0",
                          "sha256": digest}, "assets": [], "bindings": [],
                "documents": [{"id": "studio-production", "role": "productionSession", "path": session_path,
                               "family": "OSTF-Production-Session", "version": "0.3.0",
                               "sha256": hashlib.sha256(session).hexdigest()}]}
    with ZipFile(DESTINATION / f"{stem}.ostfp", "w") as archive:
        for path, payload in (("mimetype", b"application/vnd.ostf-project+zip"),
                              ("manifest.json", json_bytes(manifest)), ("score/song.ostf", data), (session_path, session)):
            archive.writestr(ZipInfo(path), payload)


if __name__ == "__main__":
    for name, author in (("neon-platforms", full_city_pop), ("blue-hour-waltz", full_waltz),
                         ("relay-circuit", full_techno), ("paper-lantern-replies", full_lantern),
                         ("offset-bridges", full_progressive), ("copper-mile", copper_mile),
                         ("first-light-vector", first_light_vector)):
        write_song(name, author())
