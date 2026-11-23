#pragma once
#include "ui/OstfpPreview.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>

class AudioClipWaveforms final : private juce::ChangeListener {
public:
    std::function<void()> onChange;

    void load(const std::vector<ostfstudio::ProjectAudio>& audio) {
        entries_.clear();
        juce::WavAudioFormat wav;
        for (const auto& clip : audio) {
            std::unique_ptr<juce::InputStream> input;
            if (!clip.embedded.isEmpty()) input = std::make_unique<juce::MemoryInputStream>(clip.embedded, true);
            else input = clip.localFile.createInputStream();
            if (!input) continue;
            std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(input.release(), true));
            if (!reader || reader->sampleRate <= 0 || reader->numChannels < 1 || reader->numChannels > 2
                || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024) continue;
            auto& entry = entries_[clip.track];
            entry.rate = reader->sampleRate;
            entry.frames = reader->lengthInSamples;
            entry.thumbnail = std::make_unique<juce::AudioThumbnail>(64, formats_, cache_);
            entry.thumbnail->addChangeListener(this);
            entry.thumbnail->setReader(reader.release(), ++serial_);
        }
    }

    bool available(std::size_t track) const { return entries_.count(track) > 0; }
    double nativeTicks(const ostfstudio::ProjectAudio& clip, const ostfstudio::TempoMap& clock) const {
        const auto it = entries_.find(clip.track);
        if (it == entries_.end()) return 0;
        return clock.tickAt(clock.secondsAt(clip.startTick) + double(sourceFrames(clip, it->second)) / it->second.rate) - clip.startTick;
    }
    double endTick(const ostfstudio::ProjectAudio& clip, const ostfstudio::TempoMap& clock) const {
        if (clip.durationTicks > 0) return double(clip.startTick + clip.durationTicks);
        const auto it = entries_.find(clip.track);
        return it == entries_.end() ? double(clip.startTick)
             : clock.tickAt(clock.secondsAt(clip.startTick) + sourceFrames(clip, it->second) / it->second.rate);
    }

    void draw(juce::Graphics& g, juce::Rectangle<int> area, const ostfstudio::ProjectAudio& clip,
              const ostfstudio::TempoMap& clock, double firstTick, double lastTick,
              const ostfstudio::ClipTiming* preview = nullptr) const {
        const auto it = entries_.find(clip.track);
        if (it == entries_.end() || area.isEmpty() || lastTick <= firstTick) return;
        const auto& data = it->second;
        const auto onset = clock.secondsAt(clip.startTick);
        const auto seconds = double(sourceFrames(clip, data)) / data.rate;
        if (seconds <= 0 || firstTick < clip.startTick - 0.001) return;
        const auto secondsAt = [&](double tick) {
            const auto n = static_cast<juce::int64>(std::floor(tick));
            return clock.secondsAt(n) + (tick - n) * (clock.secondsAt(n + 1) - clock.secondsAt(n));
        };
        const auto& timing = preview ? *preview : clip.timing;
        const auto phaseAt = [&](double tick) {
            return timing.cycleTicks ? (tick - clip.startTick) / timing.cycleTicks : (secondsAt(tick) - onset) / seconds;
        };
        const auto tickAt = [&](double phase) {
            return timing.cycleTicks ? clip.startTick + phase * timing.cycleTicks : clock.tickAt(onset + phase * seconds);
        };
        const auto xAt = [&](double tick) {
            return area.getX() + juce::roundToInt((tick - firstTick) / (lastTick - firstTick) * area.getWidth());
        };
        const double end = timing.loop ? lastTick : std::min(lastTick, tickAt(1));
        const double offset = clip.startFrame / data.rate;
        g.setColour(juce::Colour(0xffa8e7df));
        for (double tick = firstTick; tick < end - 1e-7;) {
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
    std::map<std::size_t, Entry> entries_;
};
