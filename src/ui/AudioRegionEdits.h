#pragma once
#include "ui/AudioRegionPlayback.h"
#include "ui/ExternalAudioEdit.h"

namespace ostfstudio {
inline ProjectAudio rightSlice(const ProjectAudio& original, juce::int64 tick, juce::int64 end,
                               const TempoMap& clock, double sourceSeconds) {
    auto right = original;
    right.regionId = "region-" + juce::Uuid().toString();
    right.startTick = tick;
    right.durationTicks = end - tick;
    const auto onset = original.sourceTick();
    auto& timing = right.timing;
    timing.leadingTicks = std::max<juce::int64>(0, onset - tick);
    if (tick <= onset) return right;
    const auto elapsed = tick - onset;
    const auto phase = timing.phaseOffset + (timing.cycleTicks ? double(elapsed) / timing.cycleTicks
        : (clock.secondsAt(tick) - clock.secondsAt(onset)) / sourceSeconds);
    timing.phaseOffset = timing.loop ? phase - std::floor(phase) : std::min(1., phase);
    if (!timing.loop && timing.sourceLimitTicks) {
        if (elapsed >= timing.sourceLimitTicks) timing.phaseOffset = 1;
        timing.sourceLimitTicks = std::max<juce::int64>(0, timing.sourceLimitTicks - elapsed);
    }
    return right;
}

// Joining different sources or independent warp maps writes a new 32-bit WAV.
// Neither source file is modified. Gaps are rendered as silence.
inline juce::Result consolidateRegions(const ProjectAudio& left, const ProjectAudio& right, juce::int64 end,
                                       const TempoMap& clock, ProjectAudio& joined) {
    const auto leftFile = sourceWavBytes(left), rightFile = sourceWavBytes(right);
    const auto a = wavReader(leftFile), b = wavReader(rightFile);
    if (!a || !b) return juce::Result::fail("Missing WAV");
    if (left.startTick + left.durationTicks == right.startTick && leftFile == rightFile
        && left.startFrame == right.startFrame && left.frameCount == right.frameCount
        && left.gainDb == right.gainDb && left.muted == right.muted) {
        const auto frames = left.frameCount ? left.frameCount : a->lengthInSamples - left.startFrame;
        const auto slice = rightSlice(left, right.startTick, end, clock, frames / a->sampleRate);
        if (slice.timing == right.timing) {
            joined = left;
            joined.durationTicks = end - left.startTick;
            return juce::Result::ok();
        }
    }
    const auto rate = std::max(a->sampleRate, b->sampleRate);
    const auto first = std::llround(clock.secondsAt(left.startTick) * rate);
    const auto last = std::llround(clock.secondsAt(end) * rate);
    if (first < 0 || last > std::numeric_limits<int>::max() || last <= first || last - first > (16 * 1024 * 1024 - 4096) / 8)
        return juce::Result::fail("Joined audio exceeds the 16 MiB WAV limit.");
    AudioRegionPlayback parts[2];
    if (auto r = parts[0].load(left, clock, rate, int(last)); r.failed()) return r;
    if (auto r = parts[1].load(right, clock, rate, int(last)); r.failed()) return r;
    ProjectAudio mix{left.track, "Joined.wav", {}, {}, left.startTick, end - left.startTick};
    mix.regionId = left.regionId;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(mix.embedded.writable(), false);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(32);
    auto writer = juce::WavAudioFormat{}.createWriterFor(stream, options);
    if (!writer) return juce::Result::fail("Could not create joined WAV.");
    juce::AudioBuffer<float> block(2, 2048);
    for (auto at = first; at < last; at += block.getNumSamples()) {
        const int count = int(std::min<long long>(block.getNumSamples(), last - at));
        block.clear();
        for (const auto& p : parts) p.mixInto(block, count, double(at), 1., rate, clock);
        if (!writer->writeFromAudioSampleBuffer(block, 0, count)) return juce::Result::fail("Could not write joined WAV.");
    }
    writer.reset();
    joined = std::move(mix);
    return juce::Result::ok();
}
}
