#pragma once
#include "ui/OstfpPreview.h"
#include "core/AudioPitchWarp.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

namespace ostfstudio {
// Both transport playback and clip consolidation use the same sample mapping.
struct AudioRegionPlayback {
    struct Source { juce::AudioBuffer<float> samples; double rate; };
    struct SourceCache {
        std::map<juce::String,std::shared_ptr<Source>> decoded;
        size_t editBytes=0;
        bool reserveEdit(int frames,int channels) {
            const size_t bytes=size_t(frames)*channels*sizeof(float);
            if (bytes>256u*1024*1024-editBytes) return false;
            editBytes+=bytes;
            return true;
        }
    };
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
        if (cache) if (auto found = cache->decoded.find(key); found != cache->decoded.end()) {
            source = found->second;
            return prepareEdits(clock,rate,cache);
        }
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
        if (cache) cache->decoded.emplace(key, source);
        return prepareEdits(clock,rate,cache);
    }

    juce::Result prepareEdits(const TempoMap& clock,double deviceRate,SourceCache* cache) {
        const int frames=source->samples.getNumSamples(), channels=source->samples.getNumChannels();
        const double rate=source->rate;
        if (!wavTiming.pitches.empty()) {
            if (cache && !cache->reserveEdit(frames,channels)) return juce::Result::fail("Audio edit render memory limit exceeded");
            auto changed=std::make_shared<Source>();
            changed->rate=rate;
            changed->samples.makeCopyOf(source->samples);
            for (const auto& p:wavTiming.pitches) {
                const int start=juce::roundToInt(p.start*frames), length=juce::roundToInt(p.end*frames)-start;
                if (!p.semitones || length<=0) continue;
                std::array<float*,2> dest{};
                for (int c=0;c<channels;++c) dest[c]=changed->samples.getWritePointer(c,start);
                renderPitchWarp(source->samples.getArrayOfReadPointers(),frames,dest.data(),length,channels,rate,p.semitones,
                                [start](int i){return double(start+i);});
                const int fade=std::min(length/2,std::max(1,juce::roundToInt(rate*.005)));
                for (int c=0;c<channels;++c) for (int i=0;i<fade;++i) {
                    const float wet=float(i)/fade;
                    for (int j:{i,length-1-i}) dest[c][j]=wet*dest[c][j]+(1-wet)*source->samples.getSample(c,start+j);
                }
            }
            source=std::move(changed);
        }
        if (!wavTiming.preservePitch || !wavTiming.cycleTicks || wavEnd<=wavStart) return juce::Result::ok();
        const double period=double(wavTiming.cycleTicks), onset=clock.secondsAt(wavStartTick);
        const double origin=wavStartTick-wavTiming.phaseOffset*period;
        const auto seconds=[&](double tick){
            const auto n=static_cast<juce::int64>(std::floor(tick));
            return clock.secondsAt(n)+(tick-n)*(clock.secondsAt(n+1)-clock.secondsAt(n));
        };
        double duration=(wavEnd-wavStart)/deviceRate;
        if (!wavTiming.loop) duration=std::min(duration,seconds(origin+period)-onset);
        const double size=std::round(duration*rate);
        if (size>8*1024*1024) return juce::Result::fail("Pitch-preserving warp exceeds the rendered region limit");
        if (size<=0) { wavEnd=wavStart; return juce::Result::ok(); }
        if (cache && !cache->reserveEdit(int(size),channels)) return juce::Result::fail("Audio edit render memory limit exceeded");
        auto warped=std::make_shared<Source>(); warped->rate=rate;
        warped->samples.setSize(channels,int(size));
        int start=0,previousStart=0,previousLength=0;
        double previousTempo=0;
        for (double base=origin;start<int(size);base+=period) {
            const double next=(seconds(base+period)-onset)*rate;
            const int end=int(std::clamp(std::round(next),0.,size));
            if (end<=start) {
                if (!wavTiming.loop || period*rate*(clock.secondsAt(wavStartTick+1)-onset)<1.)
                    return juce::Result::fail("Warp cycle is shorter than one sample");
                continue;
            }
            const int length=end-start;
            const auto change=clock.nextChangeAfter(static_cast<juce::int64>(std::floor(base)));
            const double tempo=(base>=wavStartTick && (!change || *change>=base+period) && next<=size+.5)
                ? clock.bpmAt(static_cast<juce::int64>(std::floor(base))) : 0.;
            if (tempo>0 && tempo==previousTempo && length==previousLength) {
                for (int c=0;c<channels;++c)
                    std::copy_n(warped->samples.getReadPointer(c,previousStart),length,warped->samples.getWritePointer(c,start));
            } else {
                std::array<float*,2> dest{};
                for (int c=0;c<channels;++c) dest[c]=warped->samples.getWritePointer(c,start);
                renderPitchWarp(source->samples.getArrayOfReadPointers(),frames,dest.data(),length,channels,rate,0,[&](int i){
                    const double tick=clock.tickAt(onset+(start+i)/rate);
                    return frames*wavTiming.sourcePhase(std::clamp((tick-base)/period,0.,1.));
                });
            }
            previousTempo=tempo; previousStart=start; previousLength=length; start=end;
        }
        source=std::move(warped);
        wavTiming={};
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
