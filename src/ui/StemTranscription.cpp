#include "ui/StemTranscription.h"
#include "ui/GlobalTimelineEdit.h"
#include <numeric>

namespace ostfstudio::scoreimport {
static juce::Result fail(const char* reason) { return juce::Result::fail(juce::translate(reason)); }

// Compare pitch-class energy in short Hann-windowed blocks with major/minor triads.
juce::Result detectTriads(juce::AudioFormatReader& reader, int ppq, List& results) {
    const double rate = reader.sampleRate;
    if (rate < 8000 || rate > 384000)
        return fail("Chord analysis supports 8 to 384 kHz.");
    const int hop = juce::roundToInt(rate * 0.5);
    constexpr int frames = 2048;
    const int stride = std::max(1, static_cast<int>(rate / 8000));
    const double sampleRate = rate / stride;
    std::array<double, 48> coeff{};
    for (int i = 0; i < static_cast<int>(coeff.size()); ++i)
        coeff[i] =
            2 * std::cos(juce::MathConstants<double>::twoPi * 440 * std::pow(2., (i + 36 - 69) / 12.) / sampleRate);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader.numChannels), hop);
    static constexpr const char* names[]{"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    for (Tick start = 0; start < reader.lengthInSamples; start += hop) {
        if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob(); job && job->shouldExit()) return fail("Analysis cancelled or timed out.");
        const auto count = static_cast<int>(std::min<Tick>(reader.lengthInSamples - start, hop));
        const int n = std::min(frames, count / stride);
        if (n < 512)
            continue;
        const int offset = (count - n * stride) / 2;
        if (!reader.read(&buffer, 0, count, start, true, true))
            return fail("Could not decode the audio file.");
        std::array<double, 12> chroma{};
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            std::array<double, frames> signal{};
            for (int j = 0; j < n; ++j) {
                const auto* samples = buffer.getReadPointer(ch, offset + j * stride);
                for (int k = 0; k < stride; ++k)
                    signal[j] += samples[k];
                signal[j] *= (0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * j / (n - 1))) / stride;
            }
            for (int p = 0; p < static_cast<int>(coeff.size()); ++p) {
                double a = 0, b = 0;
                for (int j = 0; j < n; ++j) {
                    const double next = signal[j] + coeff[p] * a - b;
                    b = a;
                    a = next;
                }
                chroma[p % 12] += std::max(0., a * a + b * b - coeff[p] * a * b);
            }
        }
        const double total = std::accumulate(chroma.begin(), chroma.end(), 0.);
        int root = -1, third = 0;
        double best = 0, runner = 0;
        for (int pc = 0; pc < 12; ++pc)
            for (int t : {3, 4}) {
                double a = chroma[pc], b = chroma[(pc + t) % 12], c = chroma[(pc + 7) % 12];
                if (std::min({a, b, c}) < total * 0.045)
                    continue;
                const double match = a + b + c;
                if (match > best) {
                    runner = best;
                    best = match;
                    root = pc;
                    third = t;
                } else
                    runner = std::max(runner, match);
            }
        if (root < 0 || total < n * n * 0.000001 || best < total * 0.65 || best - runner < total * 0.045)
            continue;
        const Tick at = std::llround(start * 2. * ppq / rate), end = std::llround((start + count) * 2. * ppq / rate);
        auto name = juce::String(names[root]) + (third == 3 ? "m" : "");
        if (!results.isEmpty()) {
            auto& last = results.getReference(results.size() - 1);
            const Tick previous = last["start"];
            if (last["label"].toString() == name && previous + static_cast<Tick>(last["duration"]) == at) {
                last.getDynamicObject()->setProperty("duration", end - previous);
                continue;
            }
        }
        results.add(obj({{"id", "chord-" + juce::String(results.size() + 1)},
                         {"label", name},
                         {"start", at},
                         {"duration", end - at},
                         {"root", parseSpelling(names[root])},
                         {"intervals", List{0, third, 7}}}));
    }
    return juce::Result::ok();
}

// YIN's first dip in the normalised difference function estimates one fundamental.
// Limit the search to the tested melodic range and reject unvoiced/quiet frames.
static int fundamental(const float *samples, int rate) {
    constexpr int window = 256, maxLag = 115;
    float power = 0;
    for (int i = 0; i < window; ++i)
        power += samples[i] * samples[i];
    if (power / window < 0.00006f)
        return -1;
    std::array<float, maxLag + 1> difference{};
    float sum = 0;
    for (int lag = 1; lag <= maxLag; ++lag) {
        float distance = 0;
        for (int i = 0; i < window; ++i) {
            const auto delta = samples[i] - samples[i + lag];
            distance += delta * delta;
        }
        sum += distance;
        difference[lag] = sum > 0 ? distance * lag / sum : 1.0f;
        if (lag > 8 && difference[lag - 1] < 0.13f && difference[lag] >= difference[lag - 1]) {
            const float a = difference[lag - 2], b = difference[lag - 1], c = difference[lag];
            const float curve = a - 2 * b + c;
            const double refined = lag - 1 + (curve > 0.000001f ? 0.5f * (a - c) / curve : 0.f);
            return juce::roundToInt(69.0 + 12.0 * std::log2(rate / (refined * 440.0)));
        }
    }
    return -1;
}
juce::Result transcribe(juce::AudioFormatReader &reader, ImportSong &song, bool boundedSolo, int rate) {
    if (boundedSolo && reader.lengthInSamples / reader.sampleRate > 120.0)
        return fail("Use up to two minutes of isolated melody for transcription.");
    // Block-average at the voice or bass analysis rate. Keep one short mono analysis buffer regardless of source rate.
    const auto size = static_cast<std::size_t>(std::ceil(reader.lengthInSamples * double(rate) / reader.sampleRate));
    std::vector<float> mono(size, 0.f);
    juce::AudioBuffer<float> block(static_cast<int>(reader.numChannels), 4096);
    for (Tick pos = 0; pos < reader.lengthInSamples;) {
        const auto count = static_cast<int>(std::min<Tick>(4096, reader.lengthInSamples - pos));
        if (!reader.read(&block, 0, count, pos, true, true))
            return fail("Could not read WAV samples.");
        const int channel =
            reader.numChannels == 2 && block.getRMSLevel(1, 0, count) > block.getRMSLevel(0, 0, count) ? 1 : 0;
        for (int i = 0; i < count; ++i) {
            const auto j = static_cast<std::size_t>((pos + i) * double(rate) / reader.sampleRate);
            mono[j] += block.getSample(channel, i) * static_cast<float>(double(rate) / reader.sampleRate);
        }
        pos += count;
    }
    Part melody;
    melody.name = TRANS("Transcribed melody");
    int active = -1;
    std::size_t start = 0;
    auto change = [&](int pitch, std::size_t at) {
        if (pitch == active)
            return;
        if (active >= 0 && at >= start + rate * .06) {
            const Tick first = static_cast<Tick>(std::llround(start * 2.0 * song.ppq / rate));
            const Tick end = static_cast<Tick>(std::llround(at * 2.0 * song.ppq / rate));
            melody.notes.add(obj({{"id", "draft-" + juce::String(++song.count)},
                                  {"start", first},
                                  {"duration", end - first},
                                  {"pitch", active}}));
        }
        active = pitch;
        start = at;
    };
    for (std::size_t i = 0; i + 371 <= mono.size(); i += rate / 100)
        change(fundamental(mono.data() + i, rate), i + 128);
    change(-1, mono.size());
    if (boundedSolo && melody.notes.isEmpty())
        return fail("No stable melody detected. Choose a single-note recording or use WAV reference.");
    song.parts.push_back(std::move(melody));
    return juce::Result::ok();
}

void draftDrumHits(juce::AudioFormatReader &reader, ImportSong &song, List &notes) {
    const int hop = std::max(1, juce::roundToInt(reader.sampleRate * .01));
    juce::AudioBuffer<float> block(int(reader.numChannels), hop);
    double floor = 0;
    Tick last = -1000;
    for (Tick frame = 0; frame < reader.lengthInSamples; frame += hop) {
        const int n = int(std::min<Tick>(hop, reader.lengthInSamples - frame));
        if (!reader.read(&block, 0, n, frame, true, true))
            break;
        int channel = block.getNumChannels() == 2 && block.getRMSLevel(1, 0, n) > block.getRMSLevel(0, 0, n) ? 1 : 0;
        const double level = block.getRMSLevel(channel, 0, n);
        const Tick tick = std::llround(frame * 2. * song.ppq / reader.sampleRate);
        if (level > std::max(.009, floor * 2.3) && tick - last >= song.ppq / 12) {
            int crossings = 0;
            const auto *x = block.getReadPointer(channel);
            for (int j = 1; j < n; ++j)
                if ((x[j] > 0) != (x[j - 1] > 0))
                    ++crossings;
            const double frequency = crossings * reader.sampleRate / (2 * n);
            const int pitch = frequency < 230 ? 36 : frequency > 2400 ? 42 : 38;
            song.drums.insert(pitch);
            notes.add(obj({{"id", "hit-" + juce::String(++song.count)},
                           {"start", tick},
                           {"duration", std::max<Tick>(1, std::min<Tick>(song.ppq / 16, song.length - tick))},
                           {"velocity", std::clamp(juce::roundToInt(127 * std::sqrt(level)), 1, 127)},
                           {"soundSlotId", "gm-" + juce::String(pitch)}}));
            last = tick;
        }
        floor = .78 * floor + .22 * level;
    }
}
} // namespace ostfstudio::scoreimport
