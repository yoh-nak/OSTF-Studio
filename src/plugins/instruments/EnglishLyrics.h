#pragma once
#include "JapaneseMora.h"
#include "EnglishLexiconData.h"
#include <algorithm>
#include <cstring>

namespace ostfstudio::singing {
// The bundled dictionary stores a sorted word index followed by word/ARPAbet pairs.
// Lookup and decompression run while preparing the score, outside the audio callback.
inline int japaneseVowelForEnglish(int phone) {
    constexpr int vowels[] = {0,0,0,0,4,0,0,3,0,3,1,1,4,4,2,2};
    return vowels[phone & 63];
}
inline bool englishWord(const juce::String& word, std::vector<Phone>& result) {
    static const juce::MemoryBlock dictionary = [] {
        juce::MemoryInputStream bytes(EnglishLexiconData::englishlexicon_gz, EnglishLexiconData::englishlexicon_gzSize, false);
        juce::GZIPDecompressorInputStream stream(&bytes, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryBlock data;
        stream.readIntoMemoryBlock(data);
        return data;
    }();
    const auto* data = static_cast<const char*>(dictionary.getData());
    if (dictionary.getSize() < 4) return false;
    const size_t count = juce::ByteOrder::littleEndianInt(data);
    const auto* records = data + 4 * (count + 1);
    const auto* text = word.toRawUTF8();
    size_t first = 0, last = count;
    while (first < last) {
        const size_t mid = first + (last - first) / 2;
        const auto* entry = records + juce::ByteOrder::littleEndianInt(data + 4 * (mid + 1));
        const int order = std::strcmp(text, entry);
        if (order < 0) last = mid;
        else if (order > 0) first = mid + 1;
        else {
            const auto begin = result.size();
            for (const auto* code = entry + std::strlen(entry) + 1; *code != 0; ++code) {
                Phone phone;
                phone.english = static_cast<unsigned char>(*code);
                result.push_back(phone);
            }
            int nextVowel = 2;
            for (auto i = result.size(); i-- > begin;) {
                auto& phone = result[i];
                const int id = phone.english & 63;
                if (id <= 15) nextVowel = japaneseVowelForEnglish(id);
                phone.vowel = id == 33 || id == 18 ? 4 : nextVowel;
            }
            return true;
        }
    }
    return false;
}

inline int japaneseOnsetForEnglish(int consonant) {
    // B CH D DH F G HH JH K L M N NG P R S SH T TH V W Y Z ZH
    constexpr int onsets[] = {b,ch,d,z,f,g,h,j,velar,r,m,n,moraN,pStop,r,sibilant,sh,t,sibilant,v,w,y,z,j};
    return onsets[consonant - 16];
}

inline std::vector<Phone> lyricPhones(const juce::String& reading) {
    std::vector<Phone> result;
    for (const auto& token : juce::StringArray::fromTokens(reading.toLowerCase(), " \t,.;:!?\r\n", {})) {
        if (token.containsOnly("abcdefghijklmnopqrstuvwxyz'-") && englishWord(token, result)) continue;
        // Continue the recorded vowel through a long-vowel mark in the same word.
        // A unit starting with the mark can still refer to the previous note.
        int start = 0;
        const auto first = result.size();
        for (int i = 0; i <= token.length(); ++i) {
            if (i < token.length() && token[i] != 0x30fc) continue;
            if (i > start) {
                const auto part = pronounce(token.substring(start, i));
                result.insert(result.end(), part.begin(), part.end());
            }
            if (i < token.length()) {
                if (result.size() > first && result.back().onset != closure) ++result.back().morae;
                else result.push_back({0,lengthMark});
            }
            start = i+1;
        }
    }
    return result;
}

inline bool isKatakanaReading(const juce::String& reading) {
    for (auto c : reading)
        if ((c < 0x30a1 || c > 0x30f6) && c != 0x30fc) return false;
    return reading.isNotEmpty();
}

inline std::vector<size_t> englishSyllableBoundaries(const std::vector<Phone>& word) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < word.size(); ++i) {
        if ((word[i].english & 63) > 15) continue;
        const bool hasOnset = i > 0 && (word[i-1].english & 63) > 15;
        starts.push_back(starts.empty() ? 0 : i - size_t(hasOnset));
    }
    starts.push_back(word.size());
    return starts;
}

// Use the written Japanese nucleus. Callers only use the result after the
// entire reading has matched, so a custom pronunciation is never half converted.
inline bool matchSungSyllable(const std::vector<Phone>& word, size_t begin, size_t end,
                              const std::vector<Phone>& kana, size_t& next, std::vector<Phone>& result,
                              bool useWordStress) {
    size_t nucleus = begin;
    while (nucleus < end && (word[nucleus].english & 63) > 15) ++nucleus;
    if (nucleus == end) return false;
    const auto main = nucleus > begin ? nucleus - 1 : begin;
    const int enVowel = word[nucleus].english & 63;

    auto support = [](const Phone& p, int en) {
        if (p.morae != 1 || p.glide) return false;
        if (p.onset == moraN) return en == 26 || en == 27 || en == 28;
        return p.onset == japaneseOnsetForEnglish(en)
            && (p.vowel == 2 || ((p.onset == t || p.onset == d) && p.vowel == 4));
    };
    for (size_t i = begin; i < main; ++i) {
        if (next == kana.size() || !support(kana[next++], word[i].english & 63)) return false;
        result.push_back(word[i]);
    }
    const auto onset = nucleus > begin ? japaneseOnsetForEnglish(word[nucleus-1].english & 63) : vowelOnly;
    if (next == kana.size()) return false;
    // A written "to-ru" can carry the unstressed /t ah l/ of "little".
    const bool syllabicL = enVowel == 3 && !(word[nucleus].english & 64) && nucleus+2 == end
        && (word[nucleus+1].english & 63) == 25 && onset == t && kana[next].vowel == 4;
    if ((!syllabicL && kana[next].vowel != japaneseVowelForEnglish(enVowel))
        || (kana[next].onset != onset && !(onset == sibilant && kana[next].vowel == 1 && kana[next].onset == sh))) return false;
    result.push_back(kana[next++]);
    if (useWordStress && (word[nucleus].english & 64)) result.back().vowelTiming = VowelTiming::stressed;
    const int secondVowel = enVowel == 5 || enVowel == 12 ? 2
                          : enVowel == 6 || enVowel == 9 || enVowel == 13 ? 1 : -1;
    if (next < kana.size() && kana[next].onset == vowelOnly && kana[next].vowel == secondVowel) {
        result.push_back(kana[next++]);
        result.back().vowelTiming = VowelTiming::offglide;
    }
    for (size_t i = nucleus + 1; i < end; ++i) {
        if (next == kana.size()) return false;
        // In ルック and レット the small ッ closes the same final stop as ク or ト.
        // The English stop already supplies its own closure and release burst.
        const int en = word[i].english & 63;
        const bool stop = en == 16 || en == 18 || en == 21 || en == 24 || en == 29 || en == 33;
        if (stop && kana[next].onset == closure && kana[next].morae == 1) {
            if (++next == kana.size()) return false;
        }
        const auto& ending = kana[next++];
        if (ending.onset == ts && ending.vowel == 2 && ending.morae == 1 && (word[i].english & 63) == 33
            && i+1 < end && (word[i+1].english & 63) == 31) {
            result.push_back(word[i++]); // One written "tsu" in "lights" covers both /t/ and /s/.
        } else if (!support(ending, word[i].english & 63)) return false;
        result.push_back(word[i]);
    }
    return true;
}

inline std::vector<Phone> sungReading(const juce::String& text, const juce::String& reading) {
    const auto& written = reading.isEmpty() ? text : reading;
    auto kana = lyricPhones(written);
    if (kana.size() < 2 || !isKatakanaReading(written)) return kana;
    if (text.isEmpty() || text == written) {
        auto supportKana = [](const Phone& p) {
            if (p.glide || p.morae != 1) return false;
            if (p.vowel == 4) return p.onset == t || p.onset == d;
            return p.vowel == 2 && (p.onset == sibilant || p.onset == f || p.onset == velar
                || p.onset == g || p.onset == b || p.onset == pStop || p.onset == m || p.onset == r);
        };
        if (std::count_if(kana.begin(), kana.end(), [](const Phone& p) { return p.morae > 1; }) == 1
            && std::all_of(kana.begin(), kana.end(), [&](const Phone& p) { return p.morae > 1 || supportKana(p); }))
            for (auto& p : kana)
                if (supportKana(p)) p.vowelTiming = VowelTiming::support;
        return kana;
    }
    if (!text.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'-")) return kana;
    std::vector<Phone> word, result;
    if (!englishWord(text.toLowerCase(), word)) return kana;
    const auto starts = englishSyllableBoundaries(word);
    size_t next = 0;
    for (size_t s = 1; s < starts.size(); ++s)
        if (!matchSungSyllable(word, starts[s-1], starts[s], kana, next, result, starts.size()>2)) return kana;
    return next == kana.size() ? result : kana;
}

// Approximate articulation time at normal singing tempo, in seconds.
inline double consonantSeconds(int phone) {
    switch (phone & 63) {
        case 16: case 18: case 21: case 24: case 29: case 33: return .032; // stops
        case 17: case 20: case 23: case 31: case 32: case 34: case 38: case 39: return .069; // noisy attacks
        default: return .049;
    }
}

inline std::vector<double> articulationPositions(const std::vector<Phone>& phones, double seconds) {
    std::vector<double> edges{0.0};
    int writtenMorae = 0;
    for (const auto& p : phones) writtenMorae += p.morae;
    // Literal support vowels keep their full time at 115 ms per mora and
    // above. Compress them progressively on crowded notes, keeping them voiced.
    const double msPerMora = 1000. * seconds / std::max(1, writtenMorae);
    const double supportWeight = .4 + .6 * std::clamp((msPerMora - 45.) / 70., 0., 1.);
    auto vowelWeight = [supportWeight](const Phone& p) {
        if (p.english) return (p.english & 64) ? 1.35 : 1.;
        if (p.vowelTiming == VowelTiming::support) return p.morae * supportWeight;
        if (p.vowelTiming == VowelTiming::offglide) return p.morae * .5;
        if (p.vowelTiming == VowelTiming::stressed) return p.morae * 1.35;
        return p.morae * (p.onset == closure ? .48 : p.onset == moraN ? .72 : 1.);
    };
    double consonants = 0, nuclei = 0;
    for (const auto& p : phones) {
        if ((p.english & 63) > 15) consonants += consonantSeconds(p.english);
        else nuclei += vowelWeight(p);
    }
    const double consonantTime = nuclei > 0 ? std::min(.55, consonants / std::max(.001, seconds)) : 1.;
    for (const auto& p : phones) {
        const double duration = (p.english & 63) > 15 ? consonantTime * consonantSeconds(p.english) / consonants
            : (1. - consonantTime) * vowelWeight(p) / nuclei;
        edges.push_back(edges.back() + duration);
    }
    edges.back() = 1.;
    return edges;
}
}
