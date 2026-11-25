#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <string_view>
#include <optional>
#include <vector>

namespace ostfstudio::singing {
// Values 0–3 are used by the original Studio articulation messages.
enum Onset { vowelOnly, sibilant, velar, moraN, g, z, t, d, n, h, b, pStop, m, r, y, w, sh, j, ch, ts, f, v, closure, lengthMark, onsetCount };
enum Delivery { plain, vibrato, scoop };
struct Phone {
    int vowel = 0, onset = vowelOnly, glide = 0, delivery = plain, intensity = 0;
    int english = 0; // ARPAbet index, with bit 6 for primary stress.
    int durationMs = 0; // Optional playback timing, outside the score format.
    int morae = 1; // Length marks within one lyric unit extend its preceding vowel.
    int sampleKey() const { return (onset * 3 + glide) * 5 + vowel; }
};

inline int kanaVowel(juce::juce_wchar c) {
    constexpr std::u32string_view small = U"ぁぃぅぇぉ";
    const auto index = small.find(c);
    return index == small.npos ? -1 : int(index);
}

inline std::optional<Phone> kanaPhone(juce::juce_wchar c) {
    struct Row { std::u32string_view kana; int onset; };
    constexpr Row rows[] = {{U"あいうえお", vowelOnly}, {U"かきくけこ", velar}, {U"がぎぐげご", g},
        {U"さしすせそ", sibilant}, {U"ざじずぜぞ", z}, {U"たちつてと", t}, {U"だぢづでど", d},
        {U"なにぬねの", n}, {U"はひふへほ", h}, {U"ばびぶべぼ", b}, {U"ぱぴぷぺぽ", pStop},
        {U"まみむめも", m}, {U"らりるれろ", r}, {U"や　ゆ　よ", y}, {U"わゐ　ゑを", w}};
    if (c == U'し') return Phone{1, sh};
    if (c == U'じ' || c == U'ぢ') return Phone{1, j};
    if (c == U'ち') return Phone{1, ch};
    if (c == U'つ') return Phone{2, ts};
    if (c == U'づ') return Phone{2, z};
    if (c == U'ふ') return Phone{2, f};
    if (c == U'ゔ') return Phone{2, v};
    if (c == U'ん') return Phone{2, moraN};
    if (c == U'っ') return Phone{0, closure};
    if (c == U'　') return {};
    for (auto row : rows) {
        const auto index = row.kana.find(c);
        if (index != row.kana.npos) return Phone{int(index), row.onset};
    }
    return {};
}

inline std::optional<Phone> romanMora(const juce::String& token) {
    if (token == "n" || token == "nn") return Phone{2, moraN};
    if (token == "cl" || token == "xtsu") return Phone{0, closure};
    if (token.isEmpty()) return {};
    const auto nucleus = juce::String("aiueo").indexOfChar(token.getLastCharacter());
    if (nucleus < 0) return {};
    const auto prefix = token.dropLastCharacters(1);
    struct Entry { const char* text; int onset; int glide = 0; };
    constexpr Entry entries[] = {{"",vowelOnly},{"k",velar},{"g",g},{"s",sibilant},{"z",z},{"t",t},{"d",d},
        {"n",n},{"h",h},{"b",b},{"p",pStop},{"m",m},{"r",r},{"y",y},{"w",w},{"sh",sh},{"j",j},{"ch",ch},
        {"ts",ts},{"f",f},{"v",v},{"ky",velar,1},{"gy",g,1},{"ny",n,1},{"hy",h,1},{"by",b,1},
        {"py",pStop,1},{"my",m,1},{"ry",r,1},{"sy",sh},{"zy",j},{"ty",ch},{"kw",velar,2},{"gw",g,2}};
    for (auto e : entries) if (prefix == e.text) return Phone{nucleus,e.onset,e.glide};
    return {};
}

inline std::vector<Phone> pronounce(const juce::String& text, bool strict = false) {
    const auto lower = text.trim().toLowerCase();
    if (auto mora = romanMora(lower)) return {*mora};
    std::vector<Phone> phones;
    int latinOnset = vowelOnly;
    bool adjacentKana = false;
    for (auto c : lower) {
        if (c >= 0x30a1 && c <= 0x30f6) c -= 0x60;
        if (c == U'ー') {
            phones.push_back(phones.empty() ? Phone{0, lengthMark} : Phone{phones.back().vowel, vowelOnly});
        } else if (c == U'ゃ' || c == U'ゅ' || c == U'ょ' || kanaVowel(c) >= 0) {
            const bool palatal = c == U'ゃ' || c == U'ゅ' || c == U'ょ';
            const int nucleus = palatal ? (c == U'ゃ' ? 0 : c == U'ゅ' ? 2 : 4) : kanaVowel(c);
            if (adjacentKana && !phones.empty() && phones.back().onset != closure && phones.back().onset != moraN) {
                auto& last = phones.back();
                if (palatal && last.onset != sh && last.onset != ch && last.onset != j) last.glide = 1;
                if (!palatal && last.vowel == 2 && (last.onset == velar || last.onset == g)) last.glide = 2;
                last.vowel = nucleus;
            } else phones.push_back({nucleus, palatal ? y : vowelOnly});
        } else if (auto k = kanaPhone(c)) {
            phones.push_back(*k);
        } else {
            adjacentKana = false;
            if (strict) return {};
            const int nucleus = juce::String("aiueo").indexOfChar(c);
            if (nucleus >= 0) { phones.push_back({nucleus, latinOnset}); latinOnset = vowelOnly; }
            else if (juce::String("sfhzvx").containsChar(c)) latinOnset = sibilant;
            else if (juce::String("ktpgdb").containsChar(c)) latinOnset = velar;
            continue;
        }
        adjacentKana = true;
    }
    if (phones.empty() && !strict) phones.push_back({0, latinOnset});
    return phones;
}
} // namespace ostfstudio::singing
