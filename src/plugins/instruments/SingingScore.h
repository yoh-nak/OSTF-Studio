#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <string_view>
#include <vector>

namespace ostfstudio::singing {
struct Phone {
    int vowel = 0, onset = 0;
};
using Lyrics = std::map<juce::String, std::vector<Phone>>;
inline const juce::Array<juce::var>& items(const juce::var& v) {
    static const juce::Array<juce::var> empty;
    return v.isArray() ? *v.getArray() : empty;
}
// Five vowel nuclei with a voiced, fricative or stop onset. Kana and romanised lyrics
// are interpreted locally; this small voice does not depend on a pronunciation service.
inline std::vector<Phone> pronounce(const juce::String& text) {
    static constexpr std::u32string_view kana[]{U"あぁかがさざただなはばぱまゃやらわ",
                                     U"いぃきぎしじちぢにひびぴみりゐ",
                                     U"うぅくぐすずつづぬふぶぷむゅゆるゔ",
                                     U"えぇけげせぜてでねへべぺめれゑ",
                                     U"おぉこごそぞとどのほぼぽもょよろを"};
    std::vector<Phone> phones;
    int onset = 0;
    for (auto c : text.toLowerCase()) {
        if (c >= 0x30a1 && c <= 0x30f6)
            c -= 0x60;
        const auto roman = juce::String("aiueo").indexOfChar(c);
        if (roman >= 0) {
            phones.push_back({roman, onset});
            onset = 0;
            continue;
        }
        bool found = false;
        for (int v = 0; v < 5; ++v)
            if (kana[v].find(c) != std::u32string_view::npos) {
                const int attack =
                    std::u32string_view(U"さしすせそざじずぜぞはひふへほ").find(c) != std::u32string_view::npos                       ? 1
                    : std::u32string_view(U"かきくけこたちつてとぱぴぷぺぽがぎぐげごだぢづでど").find(c) != std::u32string_view::npos ? 2
                                                                                                                   : 0;
                phones.push_back({v, attack});
                onset = 0;
                found = true;
                break;
            }
        if (!found && juce::String("sfhzvx").containsChar(c))
            onset = 1;
        else if (!found && juce::String("ktpgdb").containsChar(c))
            onset = 2;
        else if (c == 0x3093)
            phones.push_back({2, 3});
    }
    if (phones.empty())
        phones.push_back({0, onset});
    return phones;
}
inline Lyrics lyricsFor(const juce::var& score) {
    Lyrics lyrics;
    std::map<juce::String, juce::String> text;
    for (const auto& u : items(score["lyrics"]["units"]))
        text[u["id"].toString()] = u["text"].toString();
    for (const auto& a : items(score["lyrics"]["alignments"])) {
        juce::String words;
        for (const auto& u : items(a["unitIds"]))
            words += text[u.toString()] + " ";
        if (words.isEmpty() || words.trim().isEmpty())
            continue;
        for (const auto& n : items(a["noteIds"])) {
            auto& p = lyrics[n.toString()];
            const auto next = pronounce(words);
            p.insert(p.end(), next.begin(), next.end());
        }
    }
    return lyrics;
}
inline juce::MidiMessage phoneMessage(int ch, int key, Phone phone) {
    const juce::uint8 data[]{
        0x7d, 'O', 'S', 'V', juce::uint8(ch - 1), juce::uint8(key), juce::uint8(phone.vowel), juce::uint8(phone.onset)};
    return juce::MidiMessage::createSysExMessage(data, sizeof(data));
}
inline bool isPhone(const juce::MidiMessage& m) {
    if (!m.isSysEx() || m.getSysExDataSize() != 8)
        return false;
    const auto* p = m.getSysExData();
    return true && p[0] == 0x7d && p[1] == 'O' && p[2] == 'S' && p[3] == 'V' && p[4] < 16 && p[5] < 128 && p[6] < 5 &&
           p[7] < 4;
}
struct Syllable {
    double start, end;
    int key, velocity;
    Phone phone;
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
                                              first + length * (i + 1) / phones.size(), key, vel, phones[i]});
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
