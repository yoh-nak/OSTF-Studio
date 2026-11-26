#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace ostfstudio {
// Complementary spectral masks. All parts keep the original clock and stereo image.
// This is an offline estimate: sources sharing a time/frequency bin can bleed.
inline juce::Result estimateStemAudio(juce::AudioFormatReader &reader, std::array<juce::MemoryBlock, 4> &audio) {
    constexpr int order = 11, size = 1 << order, hop = size / 2;
    const int channels = int(reader.numChannels);
    if (channels < 1 || channels > 2 || reader.lengthInSamples <= 0 ||
        reader.lengthInSamples * channels * 2 + 128 > 16 * 1024 * 1024)
        return juce::Result::fail(TRANS("Each decoded stem must fit within 16 MiB."));
    juce::WavAudioFormat wav;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, 4> writers;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withNumChannels(channels)
                             .withSampleRate(reader.sampleRate)
                             .withBitsPerSample(16);
    for (size_t i = 0; i < writers.size(); ++i) {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(audio[i], false);
        writers[i] = wav.createWriterFor(stream, options);
        if (!writers[i])
            return juce::Result::fail(TRANS("Could not create stem audio."));
    }
    juce::dsp::FFT fft(order);
    std::array<float, size> window;
    for (int i = 0; i < size; ++i)
        window[i] = std::sin(float(juce::MathConstants<double>::pi * (i + .5) / size));
    std::array<float, hop + 1> history{}, energy{};
    std::array<std::array<float, 2 * size>, 2> spectrum{};
    std::array<std::array<float, hop + 1>, 4> weights{};
    std::array<std::array<std::array<float, hop>, 2>, 4> tail{};
    std::array<float, 2 * size> inverse{};
    juce::AudioBuffer<float> input(channels, size), output(channels, hop);
    for (juce::int64 pos = -hop; pos < reader.lengthInSamples; pos += hop) {
        if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob(); job && job->shouldExit())
            return juce::Result::fail(TRANS("Analysis cancelled or timed out."));
        input.clear();
        const int offset = int(std::max<juce::int64>(0, -pos));
        const auto start = std::max<juce::int64>(0, pos);
        const int length = int(std::min<juce::int64>(size - offset, reader.lengthInSamples - start));
        if (!reader.read(&input, offset, length, start, true, true))
            return juce::Result::fail(TRANS("Could not read WAV samples."));
        for (int ch = 0; ch < channels; ++ch) {
            for (int j = 0; j < size; ++j)
                spectrum[ch][j] = input.getSample(ch, j) * window[j];
            fft.performRealOnlyForwardTransform(spectrum[ch].data(), true);
        }
        for (int k = 0; k <= hop; ++k) {
            const int j = 2 * k;
            energy[k] = std::hypot(spectrum[0][j], spectrum[0][j + 1]);
            if (channels == 2)
                energy[k] += std::hypot(spectrum[1][j], spectrum[1][j + 1]);
        }
        for (int k = 0; k <= hop; ++k) {
            const float e = energy[k] + 1.e-7f;
            const float hz = float(k * reader.sampleRate / size);
            float nearby = 0;
            for (int n = -3; n <= 3; ++n)
                nearby += energy[std::clamp(k + n, 0, hop)];
            const float attack = std::clamp((e - history[k]) / e, 0.f, 1.f);
            history[k] = .65f * history[k] + .35f * e;
            const float percussion = std::clamp(attack * std::min(1.f, nearby / (5.f * e)), 0.f, .96f);
            const float bass = (1 - percussion) / (1 + std::pow(hz / 175.f, 4.f));
            const int j = 2 * k;
            const float centre =
                channels == 1
                    ? 1.f
                    : std::hypot(spectrum[0][j] + spectrum[1][j], spectrum[0][j + 1] + spectrum[1][j + 1]) / e;
            const float voiceBand = std::clamp((hz - 100.f) / 180.f, 0.f, 1.f) / (1 + std::pow(hz / 3300.f, 4.f));
            const float voice = (1 - percussion - bass) * voiceBand * std::min(1.f, centre * centre) *
                                std::clamp(3.f * e / (nearby + 1.e-7f), .05f, 1.f);
            weights[0][k] = percussion;
            weights[1][k] = bass;
            weights[2][k] = 1 - percussion - bass - voice;
            weights[3][k] = voice;
        }
        for (size_t stem = 0; stem < writers.size(); ++stem) {
            for (int ch = 0; ch < channels; ++ch) {
                for (int k = 0; k <= hop; ++k)
                    for (int x : {2 * k, 2 * k + 1})
                        inverse[x] = spectrum[ch][x] * weights[stem][k];
                fft.performRealOnlyInverseTransform(inverse.data());
                for (int j = 0; j < hop; ++j) {
                    output.setSample(ch, j, tail[stem][ch][j] + inverse[j] * window[j]);
                    tail[stem][ch][j] = inverse[j + hop] * window[j + hop];
                }
            }
            if (pos >= 0 && !writers[stem]->writeFromAudioSampleBuffer(
                                output, 0, int(std::min<juce::int64>(hop, reader.lengthInSamples - pos))))
                return juce::Result::fail(TRANS("Could not create stem audio."));
        }
    }
    return juce::Result::ok();
}
} // namespace ostfstudio
