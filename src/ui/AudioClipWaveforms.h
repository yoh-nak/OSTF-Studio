#pragma once
#include "ui/OstfpPreview.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>

class AudioClipWaveforms final : private juce::ChangeListener {
public:
    std::function<void()> onChange;

    void load(const std::vector<ostfstudio::ProjectAudio>& audio) {
        entries_.clear();
        std::map<juce::String, std::shared_ptr<Entry>> media;
        juce::WavAudioFormat wav;
        for (const auto& clip : audio) {
            const auto key = clip.mediaKey();
            if (auto it = media.find(key); it != media.end()) {
                entries_.emplace(clip.regionId, it->second);
                continue;
            }
            std::unique_ptr<juce::InputStream> input;
            if (!clip.embedded.isEmpty()) input = std::make_unique<juce::MemoryInputStream>(clip.embedded, true);
            else input = clip.localFile.createInputStream();
            if (!input) continue;
            std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(input.release(), true));
            if (!reader || reader->sampleRate <= 0 || reader->numChannels < 1 || reader->numChannels > 2
                || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024) continue;
            auto shared = std::make_shared<Entry>();
            media.emplace(key, shared);
            entries_.emplace(clip.regionId, shared);
            auto& entry = *shared;
            entry.rate = reader->sampleRate;
            entry.frames = reader->lengthInSamples;
            entry.thumbnail = std::make_unique<juce::AudioThumbnail>(64, formats_, cache_);
            entry.thumbnail->addChangeListener(this);
            entry.thumbnail->setReader(reader.release(), ++serial_);
        }
    }

    bool available(const ostfstudio::ProjectAudio& clip) const { return entries_.count(clip.regionId) > 0; }
    double nativeSeconds(const ostfstudio::ProjectAudio& clip) const {
        const auto it = entries_.find(clip.regionId);
        return it == entries_.end() ? 0. : double(sourceFrames(clip, *it->second)) / it->second->rate;
    }
    double nativeTicks(const ostfstudio::ProjectAudio& clip, const ostfstudio::TempoMap& clock) const {
        const auto it = entries_.find(clip.regionId);
        if (it == entries_.end()) return 0;
        return clock.tickAt(clock.secondsAt(clip.sourceTick()) + double(sourceFrames(clip, *it->second)) / it->second->rate) - clip.sourceTick();
    }
    double endTick(const ostfstudio::ProjectAudio& clip, const ostfstudio::TempoMap& clock) const {
        if (clip.durationTicks > 0) return double(clip.startTick + clip.durationTicks);
        const auto it = entries_.find(clip.regionId);
        return it == entries_.end() ? double(clip.startTick)
             : clock.tickAt(clock.secondsAt(clip.sourceTick()) + sourceFrames(clip, *it->second) / it->second->rate);
    }

    static constexpr juce::uint32 seamColour = 0xfff1d398;
    void draw(juce::Graphics& g, juce::Rectangle<int> area, const ostfstudio::ProjectAudio& clip,
              const ostfstudio::TempoMap& clock, double firstTick, double lastTick,
              const ostfstudio::ClipTiming* preview = nullptr) const {
        const auto it = entries_.find(clip.regionId);
        if (it == entries_.end() || area.isEmpty() || lastTick <= firstTick) return;
        const auto& data = *it->second;
        const auto onset = clock.secondsAt(clip.sourceTick());
        const auto seconds = double(sourceFrames(clip, data)) / data.rate;
        if (seconds <= 0) return;
        const auto secondsAt = [&](double tick) {
            const auto n = static_cast<juce::int64>(std::floor(tick));
            return clock.secondsAt(n) + (tick - n) * (clock.secondsAt(n + 1) - clock.secondsAt(n));
        };
        const auto& timing = preview ? *preview : clip.timing;
        const auto phaseAt = [&](double tick) {
            return timing.phaseOffset + (timing.cycleTicks ? (tick - clip.sourceTick()) / timing.cycleTicks : (secondsAt(tick) - onset) / seconds);
        };
        const auto tickAt = [&](double phase) {
            return timing.cycleTicks ? clip.sourceTick() + (phase - timing.phaseOffset) * timing.cycleTicks : clock.tickAt(onset + (phase - timing.phaseOffset) * seconds);
        };
        const auto xAt = [&](double tick) {
            return area.getX() + juce::roundToInt((tick - firstTick) / (lastTick - firstTick) * area.getWidth());
        };
        double end = timing.loop ? lastTick : std::min(lastTick, tickAt(1));
        if (!timing.loop && timing.sourceLimitTicks) end = std::min(end, double(clip.sourceTick() + timing.sourceLimitTicks));
        const double offset = clip.startFrame / data.rate;
        for (double tick = std::max(firstTick, double(clip.sourceTick())); tick < end - 1e-7;) {
            g.setColour(juce::Colour(0xffa8e7df));
            const double phase = std::max(0.0, phaseAt(tick));
            const double cycle = std::floor(phase + 1e-9);
            const double local = std::max(0.0, phase - cycle);
            auto marker = std::upper_bound(timing.markers.begin(), timing.markers.end(), local + 1e-9,
                [](double p, const auto& point) { return p < point.position; });
            double next = std::min(end, tickAt(cycle + (marker == timing.markers.end() ? 1. : marker->position)));
            if (!timing.cycleTicks)
                if (auto change = clock.nextChangeAfter(static_cast<juce::int64>(std::floor(tick))))
                    next = std::min(next, double(*change));
            const auto left = xAt(tick);
            if (timing.loop && xAt(tickAt(cycle + 1)) <= left && xAt(tickAt(cycle + 2)) <= left + 1) {
                // At overview zoom a pixel can contain thousands of complete cycles.
                const double perPixel = (lastTick - firstTick) / area.getWidth();
                next = std::min(end, tick + perPixel);
                data.thumbnail->drawChannels(g, area.withX(left).withWidth(1), offset, offset + seconds, 1.f);
            } else {
                const auto segment = area.withLeft(left).withRight(xAt(next));
                if (!segment.isEmpty())
                    data.thumbnail->drawChannels(g, segment, offset + seconds * timing.sourcePhase(local),
                        offset + seconds * timing.sourcePhase(std::clamp(phaseAt(next) - cycle, 0., 1.)), 1.f);
            }
            if (timing.loop && cycle >= 1 && local < 1e-8 && xAt(tickAt(cycle + 1)) - left >= 3) {
                g.setColour(juce::Colour(seamColour));
                g.drawVerticalLine(left, float(area.getY()), float(area.getBottom()));
                g.fillRect(left, area.getY(), 4, 3);
            }
            if (next <= tick + 1e-8) break;
            tick = next;
        }
    }

private:
    struct Entry {
        double rate = 0;
        juce::int64 frames = 0;
        std::unique_ptr<juce::AudioThumbnail> thumbnail;
    };
    static juce::int64 sourceFrames(const ostfstudio::ProjectAudio& clip, const Entry& e) {
        const auto remaining = std::max<juce::int64>(0, e.frames - clip.startFrame);
        return clip.frameCount > 0 ? std::min(remaining, clip.frameCount) : remaining;
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { if (onChange) onChange(); }
    juce::AudioFormatManager formats_;
    juce::AudioThumbnailCache cache_{1};
    juce::int64 serial_ = 0;
    std::map<juce::String, std::shared_ptr<Entry>> entries_;
};
