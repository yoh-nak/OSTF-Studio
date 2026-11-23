#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <BinaryData.h>
#include <array>
#include <cmath>
#include <vector>
#include <numeric>

namespace ostfstudio {
enum class DetectedPart { drums, bass, accompaniment, melody };
struct DetectedNote {
    DetectedPart part;
    int pitch, velocity;
    double start, end; // elapsed seconds in the recording
    int gmFamily = -1;
};

// Match an isolated note against the original harmonic balances used by Studio's GM preview bank.
inline int instrumentFamily(const std::array<float, 1024>& spectrum, double fundamentalBin) {
    constexpr size_t harmonics = 7;
    using Balance = std::array<double, harmonics>;
    static const auto reference = [] {
        std::array<Balance, 16> profiles{};
        const auto bank = juce::JSON::parse(juce::String::fromUTF8(BinaryData::GMFamilies_json, BinaryData::GMFamilies_jsonSize));
        for (int family = 0; family < std::min(16, bank.size()); ++family) {
            const auto gains = bank[family][1];
            for (int i = 1; i <= int(harmonics) && i < gains.size(); ++i)
                profiles[family][i - 1] = double(gains[i]);
        }
        return profiles;
    }();
    if (fundamentalBin < 4 || fundamentalBin * (harmonics + 1) + 2 >= spectrum.size()) return -1;
    auto magnitude = [&](double bin) {
        const auto k = juce::roundToInt(bin);
        return double(spectrum[k - 1] + spectrum[k] + spectrum[k + 1]);
    };
    const auto base = magnitude(fundamentalBin);
    if (base <= 0) return -1;
    Balance observed{};
    double energy = 0;
    for (size_t i = 0; i < harmonics; ++i) {
        observed[i] = magnitude(fundamentalBin * (i + 2)) / base;
        energy += observed[i] * observed[i];
    }
    if (energy < .01) return -1;
    double best = 100, next = 100;
    int candidate = -1;
    for (size_t f = 0; f < reference.size(); ++f) {
        double distance = 0;
        for (size_t i = 0; i < harmonics; ++i) {
            const auto difference = observed[i] - reference[f][i];
            distance += difference * difference;
        }
        if (distance < best) {
            next = best;
            best = distance;
            candidate = int(f);
        } else next = std::min(next, distance);
    }
    return best < .08 * energy && best < .55 * next ? candidate : -1;
}

// Estimate simultaneous fundamentals directly from the recording. Sum spectral
// energy across channels so a panned or polarity-inverted instrument survives.
inline juce::Result detectScoreNotes(juce::AudioFormatReader& reader, std::vector<DetectedNote>& notes) {
    constexpr int order = 11, size = 1 << order, hop = 128;
    const int stride = std::max(1, int(reader.sampleRate / 11025.0));
    const double rate = reader.sampleRate / stride;
    const int frames = int((reader.lengthInSamples + stride - 1) / stride);
    const int channels = int(reader.numChannels);
    juce::AudioBuffer<float> samples(channels, frames);
    samples.clear();
    juce::AudioBuffer<float> input(channels, 8192);
    for (juce::int64 start = 0; start < reader.lengthInSamples; start += input.getNumSamples()) {
        const int n = int(std::min<juce::int64>(input.getNumSamples(), reader.lengthInSamples - start));
        if (!reader.read(&input, 0, n, start, true, true))
            return juce::Result::fail(TRANS("Could not decode the audio file."));
        for (int ch = 0; ch < channels; ++ch) {
            auto* dest = samples.getWritePointer(ch);
            const auto* src = input.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                dest[(start + i) / stride] += src[i] / stride;
        }
    }

    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(size, juce::dsp::WindowingFunction<float>::hann, false);
    std::array<float, 2 * size> work{};
    std::array<float, size / 2> spectrum{};
    struct Active {
        int start = -1, last = 0, leadFrames = 0, count = 0;
        float peak = 0;
        std::array<int, 16> familyVotes{};
    };
    std::array<Active, 128> active{};
    double previousLevel = 0;
    int lastHit = -size;
    for (int pos = 0; pos < frames + size / 2; pos += hop) {
        spectrum.fill(0.f);
        const int first = std::max(0, pos - size / 2), end = std::min(frames, pos + size / 2);
        for (int ch = 0; ch < channels; ++ch) {
            work.fill(0.f);
            std::copy_n(samples.getReadPointer(ch) + first, end - first, work.begin() + (first - pos + size / 2));
            window.multiplyWithWindowingTable(work.data(), size);
            fft.performFrequencyOnlyForwardTransform(work.data(), true);
            for (size_t k = 1; k < spectrum.size(); ++k)
                spectrum[k] += work[k];
        }
        std::array<float, 128> pitches{};
        const float strongest = *std::max_element(spectrum.begin(), spectrum.end());
        int top = -1, fundamentals = 0;
        double topBin = 0;
        for (int k = 2; k + 1 < int(spectrum.size()); ++k) {
            const float a = spectrum[k - 1], b = spectrum[k], c = spectrum[k + 1];
            if (b < std::max(1.8f * channels, strongest * .105f) || b <= a || b < c) continue;
            const double bin = k + .5 * (a - c) / (a - 2 * b + c);
            const double hz = bin * rate / size;
            const int pitch = juce::roundToInt(69 + 12 * std::log2(hz / 440.));
            if (pitch < 24 || pitch > 103) continue;
            bool overtone = false;
            for (int harmonic = 2; harmonic <= 8 && !overtone; ++harmonic) {
                const int lower = juce::roundToInt(pitch - 12 * std::log2(double(harmonic)));
                if (lower >= 0 && pitches[lower] > b * 1.4f)
                    overtone = true;
            }
            if (!overtone) {
                if (pitches[pitch] == 0) ++fundamentals;
                pitches[pitch] = std::max(pitches[pitch], b);
                topBin = bin;
                top = pitch;
            }
        }
        const int family = fundamentals == 1 ? instrumentFamily(spectrum, topBin) : -1;
        // A short, sharp, noise-rich onset can be a drum even with pitched notes underneath.
        if (pos < frames) {
            const int n = std::min(hop, frames - pos);
            int loudest = 0;
            if (channels == 2 && samples.getRMSLevel(1, pos, n) > samples.getRMSLevel(0, pos, n)) loudest = 1;
            const auto level = samples.getRMSLevel(loudest, pos, n);
            if (level > std::max(.015, previousLevel * 2.2) && pos - lastHit > rate * .045) {
                const double sum = std::accumulate(spectrum.begin() + 1, spectrum.end(), 0.);
                if (strongest > 0 && sum / strongest > 18.) {
                    const auto split = std::clamp(int(1800 * size / rate), 2, size / 2 - 1);
                    const auto bassEnd = std::clamp(int(180 * size / rate), 1, split - 1);
                    const auto bass = std::accumulate(spectrum.begin(), spectrum.begin() + bassEnd, 0.);
                    const auto high = std::accumulate(spectrum.begin() + split, spectrum.end(), 0.);
                    const int drum = bass > .38 * sum ? 36 : high > .57 * sum ? 42 : 38;
                    notes.push_back({DetectedPart::drums, drum, std::clamp(juce::roundToInt(127 * std::sqrt(level)), 1, 127),
                                     pos / rate, std::min(frames / rate, pos / rate + .055)});
                    lastHit = pos;
                }
            }
            previousLevel = .5 * level + .5 * previousLevel;
        }
        for (int p = 24; p <= 103; ++p) {
            auto& note = active[p];
            if (pitches[p] > 0 && pos < frames) {
                if (note.start < 0) note.start = pos;
                note.last = pos;
                note.peak = std::max(note.peak, pitches[p]);
                note.leadFrames += p == top;
                if (family >= 0) ++note.familyVotes[size_t(family)];
                ++note.count;
            } else if (note.start >= 0 && (pos - note.last >= 3 * hop || pos >= frames)) {
                if (note.count * hop >= .075 * rate) {
                    const auto role = p < 55 ? DetectedPart::bass : 2 * note.leadFrames >= note.count
                                                                     ? DetectedPart::melody : DetectedPart::accompaniment;
                    const auto vote = std::max_element(note.familyVotes.begin(), note.familyVotes.end());
                    const int timbre = *vote >= 6 && *vote * 2 > note.count ? int(vote - note.familyVotes.begin()) : -1;
                    notes.push_back({role, p, std::clamp(juce::roundToInt(125 * std::sqrt(note.peak / (channels * size * .25f))), 1, 127),
                                     note.start / rate, std::min(frames, note.last + hop) / rate, timbre});
                }
                note = {};
            }
        }
        if (notes.size() > 60000) return juce::Result::fail(TRANS("This import exceeds the note limit."));
        if (pos >= frames) break;
    }
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return juce::Result::ok();
}
}
