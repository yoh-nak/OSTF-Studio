#pragma once
#include "JapaneseMora.h"
#include <map>
#include <string_view>
#include <vector>

namespace ostfstudio::singing {
using Lyrics = std::map<juce::String, std::vector<Phone>>;
inline const juce::Array<juce::var>& items(const juce::var& v) {
    static const juce::Array<juce::var> empty;
    return v.isArray() ? *v.getArray() : empty;
}
inline Lyrics lyricsFor(const juce::var& score) {
    Lyrics lyrics;
    std::map<juce::String, juce::String> readings;
    for (const auto& unit : items(score["lyrics"]["units"])) {
        auto reading = unit["pronunciation"].toString().trim();
        readings[unit["id"].toString()] = reading.isEmpty() ? unit["text"].toString() : reading;
    }
    for (const auto& alignment : items(score["lyrics"]["alignments"])) {
        std::vector<Phone> phones;
        for (const auto& id : items(alignment["unitIds"])) {
            const auto found = readings.find(id.toString());
            if (found == readings.end() || found->second.trim().isEmpty()) continue;
            const auto next = pronounce(found->second);
            phones.insert(phones.end(), next.begin(), next.end());
        }
        if (phones.empty()) continue;
        for (const auto& id : items(alignment["noteIds"])) {
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
        for (const auto& [time, id] : ordered) {
            if (auto found = lyrics.find(id); found != lyrics.end()) {
                for (auto& phone : found->second) {
                    if (phone.onset == lengthMark) { phone.onset = vowelOnly; phone.vowel = lastVowel; }
                    if (phone.onset != closure && phone.onset != moraN) lastVowel = phone.vowel;
                }
            }
        }
    }
    return lyrics;
}

// Studio-private non-commercial SysEx. Accept the earlier two-field articulation as well.
inline juce::MidiMessage phoneMessage(int ch, int key, Phone phone) {
    const juce::uint8 data[]{0x7d, 'O', 'S', 'V', juce::uint8(ch - 1), juce::uint8(key),
        juce::uint8(phone.vowel), juce::uint8(phone.onset), juce::uint8(phone.glide),
        juce::uint8(phone.delivery), juce::uint8(phone.intensity)};
    return juce::MidiMessage::createSysExMessage(data, sizeof(data));
}
inline bool isPhone(const juce::MidiMessage& m) {
    if (!m.isSysEx()) return false;
    const int size = m.getSysExDataSize();
    if (size != 8 && size != 11) return false;
    const auto* p = m.getSysExData();
    return p[0] == 0x7d && p[1] == 'O' && p[2] == 'S' && p[3] == 'V' && p[4] < 16 && p[5] < 128 && p[6] < 5
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
    const auto ppq = double(song["ppq"]);
    std::vector<Part> parts;
    if (song["format"] != "OSTF" || !(ppq > 0))
        return parts;
    const auto lyrics = lyricsFor(song);
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
                const auto first = (double(clip["start"]) + double(n["start"])) / ppq,
                           length = double(n["duration"]) / ppq;
                const auto key = int(n["pitch"]), vel = std::clamp(int(n.getProperty("velocity", 92)), 0, 127);
                if (!(length > 0) || key < 0 || key > 127)
                    continue;
                const auto entry = lyrics.find(n["id"].toString());
                if (entry == lyrics.end()) {
                    part.syllables.push_back({first, first + length, key, vel, {}});
                    continue;
                }
                const auto& phones = entry->second;
                for (size_t i = 0; i < phones.size(); ++i)
                    part.syllables.push_back({first + length * i / phones.size(),
                                              first + length * (i + 1) / phones.size(), key, vel, phones[i], i > 0});
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
