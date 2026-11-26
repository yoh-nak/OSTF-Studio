#pragma once
#include "ui/OstfpPreview.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

namespace ostfstudio {
// Both transport playback and clip consolidation use the same sample mapping.
struct AudioRegionPlayback {
    struct Source { juce::AudioBuffer<float> samples; double rate; };
    using SourceCache = std::map<juce::String, std::shared_ptr<Source>>;
    std::shared_ptr<Source> source;
    int wavStart = 0, wavEnd = 0;
    juce::int64 wavStartTick = 0;
    ClipTiming wavTiming;
    float wavGain = 1;
    juce::int64 regionStart=0,regionDuration=0,fadeIn=0,fadeOut=0;

    juce::Result load(const ProjectAudio& audio, const TempoMap& clock, double rate, int lastSample, SourceCache* cache = nullptr) {
        if (!audio.timing.valid()) return juce::Result::fail("Invalid WAV timing");
        wavTiming = audio.timing;
        regionStart=audio.startTick; regionDuration=audio.durationTicks;
        fadeIn=audio.fadeInTicks; fadeOut=audio.fadeOutTicks;
        wavStartTick = audio.sourceTick();
        wavStart = juce::roundToInt(clock.secondsAt(wavStartTick) * rate);
        wavEnd = audio.durationTicks > 0 ? juce::roundToInt(clock.secondsAt(audio.startTick + audio.durationTicks) * rate) : lastSample;
        if (!wavTiming.loop && wavTiming.sourceLimitTicks)
            wavEnd = std::min(wavEnd, juce::roundToInt(clock.secondsAt(wavStartTick + wavTiming.sourceLimitTicks) * rate));
        wavGain = audio.muted ? 0.0f : juce::Decibels::decibelsToGain(static_cast<float>(audio.gainDb));
        const auto key = audio.mediaKey() + "|" + audio.sha256 + "|" + juce::String(audio.startFrame) + "|" + juce::String(audio.frameCount)
            + "|" + juce::String(audio.assetFrames) + "|" + juce::String(audio.sampleRate) + "|" + juce::String(audio.channels);
        if (cache) if (auto found = cache->find(key); found != cache->end()) { source = found->second; return juce::Result::ok(); }
        juce::WavAudioFormat wavFormat;
        auto stream = audio.embedded.isEmpty()
            ? std::unique_ptr<juce::InputStream>(audio.localFile.createInputStream().release())
            : std::make_unique<juce::MemoryInputStream>(audio.embedded, false);
        if (!stream) return juce::Result::fail("Missing WAV: " + audio.path);
        if (audio.sha256.isNotEmpty() && (stream->getTotalLength() > 16 * 1024 * 1024
            || juce::SHA256(*stream).toHexString() != audio.sha256 || !stream->setPosition(0)))
            return juce::Result::fail("WAV does not match the saved project: " + audio.path);
        std::unique_ptr<juce::AudioFormatReader> reader(wavFormat.createReaderFor(stream.release(), true));
        if (!reader || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024
            || reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate <= 0)
            return juce::Result::fail("Unsupported WAV: " + audio.path);
        if (audio.sha256.isNotEmpty() && (reader->sampleRate != audio.sampleRate
            || static_cast<int>(reader->numChannels) != audio.channels || reader->lengthInSamples != audio.assetFrames))
            return juce::Result::fail("WAV format does not match its descriptor: " + audio.path);
        const auto frames = audio.frameCount > 0 ? audio.frameCount : reader->lengthInSamples - audio.startFrame;
        if (audio.startFrame < 0 || frames > reader->lengthInSamples - audio.startFrame)
            return juce::Result::fail("Invalid WAV source range: " + audio.path);
        source = std::make_shared<Source>();
        auto& wav = source->samples;
        wav.setSize(static_cast<int>(reader->numChannels), static_cast<int>(frames));
        if (!reader->read(&wav, 0, wav.getNumSamples(), audio.startFrame, true, true))
            return juce::Result::fail("Could not decode WAV: " + audio.path);
        source->rate = reader->sampleRate;
        if (cache) cache->emplace(key, source);
        return juce::Result::ok();
    }

    void mixInto(juce::AudioBuffer<float>& block, int count, double first, double timelineStep, double rate, const TempoMap& clock) const {
        if (count <= 0 || first >= wavEnd || first + count * timelineStep <= wavStart) return;
        const auto& wav = source->samples;
        const auto wavRate = source->rate;
        for (int c = 0; c < 2; ++c) {
            const auto* source = wav.getReadPointer(std::min(c, wav.getNumChannels() - 1));
            auto* dest = block.getWritePointer(c);
            for (int i = 0; i < count; ++i) {
                const double position = first + i * timelineStep;
                if (position < wavStart) continue;
                if (position >= wavEnd) break;
                const auto nativeFrame = (position - wavStart) * wavRate / rate;
                const auto tick = wavTiming.cycleTicks ? clock.tickAt(position / rate) - wavStartTick : 0.0;
                const auto frame = wavTiming.frameAt(nativeFrame, std::max(0.0, tick), wav.getNumSamples());
                if (frame >= wav.getNumSamples()) break;
                const auto pos = static_cast<int>(frame);
                const auto next = std::min(pos + 1, wav.getNumSamples() - 1);
                float envelope=wavGain;
                if(fadeIn || fadeOut) {
                    const double elapsed=clock.tickAt(position/rate)-regionStart;
                    if(fadeIn) envelope*=float(std::clamp(elapsed/fadeIn,0.,1.));
                    if(fadeOut) envelope*=float(std::clamp((regionDuration-elapsed)/fadeOut,0.,1.));
                }
                dest[i] += (source[pos] + (source[next] - source[pos]) * static_cast<float>(frame - pos)) * envelope;
            }
        }
    }
};
}
