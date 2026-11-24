#pragma once
#include "JapaneseMora.h"
#include "EnglishLexiconData.h"
#include <cstring>

namespace ostfstudio::singing {
// The bundled dictionary stores a sorted word index followed by word/ARPAbet pairs.
// Lookup and decompression run while preparing the score, outside the audio callback.
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
            for (const auto* code = entry + std::strlen(entry) + 1; *code != 0; ++code) {
                Phone phone;
                phone.english = static_cast<unsigned char>(*code);
                result.push_back(phone);
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
        const auto rest = pronounce(token);
        result.insert(result.end(), rest.begin(), rest.end());
    }
    return result;
}

inline std::vector<double> articulationPositions(const std::vector<Phone>& phones, double seconds) {
    std::vector<double> edges{0.0};
    double consonants = 0, nuclei = 0;
    for (const auto& p : phones) {
        if ((p.english & 63) > 15) consonants += 1;
        else nuclei += p.english & 64 ? 1.35 : 1.;
    }
    const double consonantTime = nuclei > 0 ? std::min(.55, consonants * .055 / std::max(.001, seconds)) : 1.;
    for (const auto& p : phones) {
        const double duration = (p.english & 63) > 15 ? consonantTime / consonants
            : (1. - consonantTime) * (p.english & 64 ? 1.35 : 1.) / nuclei;
        edges.push_back(edges.back() + duration);
    }
    edges.back() = 1.;
    return edges;
}
}
