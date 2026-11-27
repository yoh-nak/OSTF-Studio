#pragma once
#include "EnglishLyrics.h"
#include "../../LyricAlignment.h"
#include "../../core/TempoMap.h"
#include <map>
#include <string_view>
#include <vector>

namespace ostfstudio::singing {
using Lyrics = std::map<juce::String, std::vector<Phone>>;
inline const juce::Array<juce::var>& items(const juce::var& v) {
    static const juce::Array<juce::var> empty;
    return v.isArray() ? *v.getArray() : empty;
}
// Carry the last sung nucleus through a melisma. Release consonants and the
// written offglide on the final note, even when pitches change in between.
inline void appendMelisma(Lyrics& lyrics, const std::vector<Phone>& phones,
                          const juce::Array<juce::var>& ids, const std::map<juce::String,juce::int64>& starts) {
    auto nucleus = std::find_if(phones.rbegin(), phones.rend(), [](const Phone& p) {
        if (p.english) return (p.english & 63) <= 15;
        return p.onset != closure && p.onset != moraN && p.onset != lengthMark
            && p.vowelTiming != VowelTiming::support && p.vowelTiming != VowelTiming::offglide;
    });
    const auto notes = lyricIdsInTimeOrder(ids,starts);

    if (nucleus == phones.rend()) {
        // A standalone length mark is resolved against the previous sung note below.
        for (const auto& id : notes) lyrics[id].insert(lyrics[id].end(),phones.begin(),phones.end());
        return;
    }
    const auto tail = nucleus.base();
    Phone held = *nucleus;
    held.onset = vowelOnly;
    held.glide = 0;
    for (size_t i=0; i<notes.size(); ++i) {
        auto& dest = lyrics[notes[i]];
        if (i==0) dest.insert(dest.end(),phones.begin(),tail-1);
        auto body = i==0 ? *nucleus : held;
        // A dictionary diphthong carries its glide internally. Sing just its
        // opening vowel until the last note, which supplies the full release.
        if (i+1<notes.size() && body.english) {
            const int en = body.english & 63;
            if (en==5 || en==6 || en==9 || en==12 || en==13) {
                const int opening = en==9 ? 7 : en==12 || en==13 ? 4 : 1;
                body.english = (body.english & 64) | opening;
            }
        }
        dest.push_back(body);
        if (i+1==notes.size()) dest.insert(dest.end(),tail,phones.end());
    }
}
inline Lyrics lyricsFor(const juce::var& score) {
    Lyrics lyrics;
    Lyrics readings;
    LyricStarts unitStarts;
    std::map<juce::String, std::vector<juce::var>> phraseUnits;
    for (const auto& unit : items(score["lyrics"]["units"])) {
        if (unit.hasProperty("start")) unitStarts.emplace(unit["id"].toString(),juce::int64(unit["start"]));
        auto reading = unit["pronunciation"].toString().trim();
        readings[unit["id"].toString()] = sungReading(unit["text"].toString().trim(), reading);
        phraseUnits[unit["phraseId"].toString()].push_back(unit);
    }
    // A word such as "city" can be written across the note units "ci" and "ty".
    // Derive the syllables from the whole word when its spelling matches the units.
    for (const auto& phrase : items(score["lyrics"]["phrases"])) {
        const auto& units = phraseUnits[phrase["id"].toString()];
        size_t pos = 0;
        for (const auto& token : juce::StringArray::fromTokens(phrase["text"].toString(), " \t\r\n,.;:!?", {})) {
            const auto word = englishLyricWord(token);
            if (word.isEmpty()) continue;
            juce::String joined;
            const auto begin = pos;
            while (pos < units.size() && joined.length() < word.length()) {
                const auto& unit = units[pos++];
                joined += englishLyricWord(unit["text"].toString());
            }
            if (joined != word) break;
            if (pos - begin < 2 || !word.containsOnly("abcdefghijklmnopqrstuvwxyz'-")) continue;
            std::vector<Phone> pronunciation;
            if (!englishWord(word, pronunciation)) continue;
            const auto starts = englishSyllableBoundaries(pronunciation);
            if (starts.size() != pos - begin + 1) continue;
            std::vector<std::vector<Phone>> syllables(pos - begin);
            bool matched = true;
            for (size_t i = 0; i < syllables.size(); ++i) {
                const auto reading = units[begin+i]["pronunciation"].toString().trim();
                if (reading.isEmpty()) {
                    syllables[i].assign(pronunciation.begin()+starts[i], pronunciation.begin()+starts[i+1]);
                    continue;
                }
                const auto kana = lyricPhones(reading);
                size_t next = 0;
                if (!isKatakanaReading(reading)
                    || !matchSungSyllable(pronunciation, starts[i], starts[i+1], kana, next, syllables[i], false)
                    || next != kana.size()) { matched = false; break; }
            }
            if (matched)
                for (size_t i = 0; i < syllables.size(); ++i)
                    readings[units[begin+i]["id"].toString()] = std::move(syllables[i]);
        }
    }
    std::map<juce::String,juce::int64> noteStarts;
    const auto& alignments = items(score["lyrics"]["alignments"]);
    if (std::any_of(alignments.begin(),alignments.end(),[](const auto& a){return a["noteIds"].size()>1;}))
        for (const auto& track : items(score["tracks"]))
            for (const auto& clip : items(track["clips"]))
                for (const auto& note : items(clip["notes"]))
                    noteStarts.emplace(note["id"].toString(),juce::int64(clip["start"])+juce::int64(note["start"]));
    for (const auto& alignment : alignments) {
        const auto& ids = items(alignment["noteIds"]);
        const auto& units = items(alignment["unitIds"]);
        if (ids.size() > 1 && ids.size() == units.size()) {
            const auto notes = lyricIdsInTimeOrder(ids,noteStarts);
            const auto syllables = lyricIdsInTimeOrder(units,unitStarts);
            for (size_t i = 0; i < notes.size(); ++i) {
                const auto src = readings.find(syllables[i]);
                if (src == readings.end()) continue;
                auto& dest = lyrics[notes[i]];
                dest.insert(dest.end(),src->second.begin(),src->second.end());
            }
            continue;
        }
        std::vector<Phone> phones;
        for (const auto& id : items(alignment["unitIds"])) {
            const auto found = readings.find(id.toString());
            if (found == readings.end()) continue;
            const auto& next = found->second;
            phones.insert(phones.end(), next.begin(), next.end());
        }
        if (phones.empty()) continue;
        if (alignment["unitIds"].size()==1 && ids.size()>1) {
            appendMelisma(lyrics,phones,ids,noteStarts);
            continue;
        }
        for (const auto& id : ids) {
            auto& dest = lyrics[id.toString()];
            dest.insert(dest.end(), phones.begin(), phones.end());
        }
    }
    for (const auto& track : items(score["tracks"])) {
        std::multimap<double, juce::String> ordered;
        for (const auto& clip : items(track["clips"])) {
            if (!bool(clip.getProperty("enabled", true))) continue;
            for (const auto& note : items(clip["notes"]))
                if (bool(note.getProperty("enabled", true)))
                    ordered.emplace(double(clip["start"]) + double(note["start"]), note["id"].toString());
            for (const auto& perf : items(clip["vocalPerformances"]))
                for (const auto& tech : items(perf["techniques"])) {
                    const auto id = tech["targetNoteId"].toString();
                    const auto kind = tech["kind"].toString();
                    if (id.isEmpty() || (kind != "vibrato" && kind != "scoop")) continue;
                    auto& phones = lyrics[id];
                    if (phones.empty()) phones.push_back({});
                    for (auto& phone : phones) {
                        phone.delivery = kind == "vibrato" ? vibrato : scoop;
                        phone.intensity = juce::jlimit(0, 127, juce::roundToInt(double(tech.getProperty("intensity", 0.5)) * 127));
                    }
                }
        }
        int lastVowel = 0;
        Onset heldOnset = vowelOnly;
        for (const auto& [time, id] : ordered) {
            if (auto found = lyrics.find(id); found != lyrics.end()) {
                for (auto& phone : found->second) {
                    if (phone.onset == lengthMark) { phone.onset = heldOnset; phone.vowel = lastVowel; }
                    if (phone.onset != closure) {
                        heldOnset = phone.onset == moraN ? moraN : vowelOnly;
                        lastVowel = phone.vowel;
                    }
                }
            }
        }
    }
    return lyrics;
}

// Studio-private non-commercial SysEx. Accept the earlier two-field articulation as well.
inline juce::MidiMessage phoneMessage(int ch, int key, Phone phone, double duration = 0.) {
    const int ms = std::clamp(juce::roundToInt(duration * 1000), 0, 16383);
    const juce::uint8 data[]{0x7d, 'O', 'S', 'V', juce::uint8(ch - 1), juce::uint8(key),
        juce::uint8(phone.vowel), juce::uint8(phone.onset), juce::uint8(phone.glide),
        juce::uint8(phone.delivery), juce::uint8(phone.intensity), juce::uint8(phone.english),
        juce::uint8(ms & 127), juce::uint8(ms >> 7)};
    return juce::MidiMessage::createSysExMessage(data, ms ? 14 : phone.english ? 12 : 11);
}
inline bool isPhone(const juce::MidiMessage& m) {
    if (!m.isSysEx()) return false;
    const int size = m.getSysExDataSize();
    if (size != 8 && size != 11 && size != 12 && size != 14) return false;
    const auto* p = m.getSysExData();
    return p[0] == 0x7d && p[1] == 'O' && p[2] == 'S' && p[3] == 'V' && p[4] < 16 && p[5] < 128 && p[6] < 5
        && (size < 12 || (size == 14 && p[11] == 0) || ((p[11] & 63) >= 1 && (p[11] & 63) <= 39))
        && (size == 8 ? p[7] < 4 : p[7] < onsetCount && p[8] <= 2 && p[9] <= scoop && p[10] <= 127);
}
struct Syllable {
    double start, end;
    int key, velocity;
    Phone phone;
    bool continuesNote = false;
};
struct Part {
    juce::String name;
    std::vector<Syllable> syllables;
};
inline std::vector<Part> readParts(const juce::var& song) {
    const auto ppq = juce::int64(song["ppq"]);
    std::vector<Part> parts;
    if (song["format"] != "OSTF" || !(ppq > 0))
        return parts;
    const auto lyrics = lyricsFor(song);
    const auto& tempos = items(song["timeline"]["tempoChanges"]);
    TempoMap clock(ppq);
    for (const auto& tempo : tempos) {
        const auto tick = juce::int64(tempo["at"]);
        if (clock.empty() && tick > 0) clock.append(0,120.);
        clock.append(tick,double(tempo["bpm"]));
    }
    if (clock.empty()) clock.append(0,120.);
    for (const auto& track : items(song["tracks"])) {
        Part part{track["name"].toString(), {}};
        if (track["trackType"] == "drum")
            continue;
        for (const auto& clip : items(track["clips"])) {
            if (!bool(clip.getProperty("enabled", true)))
                continue;
            for (const auto& n : items(clip["notes"])) {
                if (n["pitch"].isVoid() || !bool(n.getProperty("enabled", true)))
                    continue;
                const auto clipStart = juce::int64(clip["start"]), offset = juce::int64(n["start"]),
                           duration = juce::int64(n["duration"]);
                const auto key = int(n["pitch"]), vel = std::clamp(int(n.getProperty("velocity", 92)), 0, 127);
                if (clipStart < 0 || offset < 0 || duration <= 0 || key < 0 || key > 127
                    || clipStart > std::numeric_limits<juce::int64>::max() - offset
                    || clipStart + offset > std::numeric_limits<juce::int64>::max() - duration)
                    continue;
                const auto start = clipStart + offset, end = start + duration;
                const auto first = double(start) / ppq, last = double(end) / ppq;
                const auto entry = lyrics.find(n["id"].toString());
                if (entry == lyrics.end()) {
                    part.syllables.push_back({first, last, key, vel, {}});
                    continue;
                }
                const auto& phones = entry->second;
                const auto startSeconds = clock.secondsAt(start), seconds = clock.secondsAt(end) - startSeconds;
                const auto edges = articulationPositions(phones, seconds);
                double from = first;
                for (size_t i = 0; i < phones.size(); ++i) {
                    const double to = i + 1 == phones.size() ? last : clock.tickAt(startSeconds + seconds * edges[i + 1]) / ppq;
                    part.syllables.push_back({from, to, key, vel, phones[i], i > 0});
                    from = to;
                }
            }
        }
        std::stable_sort(part.syllables.begin(), part.syllables.end(),
                         [](auto& a, auto& b) { return a.start < b.start; });
        if (!part.syllables.empty())
            parts.push_back(std::move(part));
    }
    return parts;
}
} // namespace ostfstudio::singing
