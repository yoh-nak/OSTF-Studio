#pragma once
#include "JapaneseMora.h"
#include "EnglishLexiconData.h"
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
    auto vowelWeight = [](const Phone& p) {
        if (p.english) return (p.english & 64) ? 1.35 : 1.;
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
