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

// Keep the written Japanese nucleus. Substituting a consonant-only phone
// requires a complete match to the dictionary word, including its coda.
inline std::vector<Phone> sungReading(const juce::String& text, const juce::String& reading) {
    const auto& written = reading.isEmpty() ? text : reading;
    auto kana = lyricPhones(written);
    if (kana.size() < 2) return kana;
    for (auto c : written)
        if ((c < 0x30a1 || c > 0x30f6) && c != 0x30fc) return kana;
    if (text.isEmpty() || text == written) {
        // With only kana, preserve every recorded syllable. A single explicit
        // long vowel surrounded by short support syllables identifies the hold.
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
    std::vector<Phone> word;
    if (!englishWord(text.toLowerCase(), word)) return kana;
    const auto isVowel = [](const Phone& p) { return (p.english & 63) <= 15; };
    if (std::count_if(word.begin(), word.end(), isVowel) != 1) return kana;
    const auto nucleus = size_t(std::find_if(word.begin(), word.end(), isVowel) - word.begin());
    const auto main = nucleus ? nucleus - 1 : 0;
    const int enVowel = word[nucleus].english & 63;
    if (main >= kana.size() || kana[main].vowel != japaneseVowelForEnglish(enVowel)
        || kana[main].onset != (nucleus ? japaneseOnsetForEnglish(word[nucleus-1].english & 63) : vowelOnly)) return kana;

    auto support = [](const Phone& p, int en) {
        if (p.morae != 1 || p.glide) return false;
        if (p.onset == moraN) return en == 26 || en == 27 || en == 28;
        return p.onset == japaneseOnsetForEnglish(en)
            && (p.vowel == 2 || ((p.onset == t || p.onset == d) && p.vowel == 4));
    };
    std::vector<Phone> result;
    for (size_t i = 0; i < main; ++i) {
        if (!support(kana[i], word[i].english & 63)) return kana;
        result.push_back(word[i]);
    }
    result.push_back(kana[main]);
    size_t next = main + 1;
    const int secondVowel = enVowel == 5 || enVowel == 12 ? 2
                          : enVowel == 6 || enVowel == 9 || enVowel == 13 ? 1 : -1;
    if (next < kana.size() && kana[next].onset == vowelOnly && kana[next].vowel == secondVowel) {
        result.push_back(kana[next++]);
        result.back().vowelTiming = VowelTiming::offglide;
    }
    for (size_t i = nucleus + 1; i < word.size(); ++i) {
        if (next == kana.size()) return kana;
        const auto& ending = kana[next++];
        if (ending.onset == ts && ending.vowel == 2 && ending.morae == 1 && (word[i].english & 63) == 33
            && i+1 < word.size() && (word[i+1].english & 63) == 31) {
            result.push_back(word[i++]); // One written "tsu" in "lights" covers both /t/ and /s/.
        } else if (!support(ending, word[i].english & 63)) return kana;
        result.push_back(word[i]);
    }
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
