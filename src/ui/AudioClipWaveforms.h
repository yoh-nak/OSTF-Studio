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
    double endTick(const ostfstudio::ProjectAudio& clip, const ostfstudio::TempoMap& clock) const {
        if (clip.durationTicks > 0) return double(clip.startTick + clip.durationTicks);
        const auto it = entries_.find(clip.track);
        return it == entries_.end() ? double(clip.startTick)
             : clock.tickAt(clock.secondsAt(clip.startTick) + sourceFrames(clip, it->second) / it->second.rate);
    }

    void draw(juce::Graphics& g, juce::Rectangle<int> area, const ostfstudio::ProjectAudio& clip,
              const ostfstudio::TempoMap& clock, double firstTick, double lastTick) const {
        const auto it = entries_.find(clip.track);
        if (it == entries_.end() || area.isEmpty() || lastTick <= firstTick) return;
        const auto& data = it->second;
        const auto onset = clock.secondsAt(clip.startTick);
        const auto sourceEnd = clock.tickAt(onset + sourceFrames(clip, data) / data.rate);
        const auto end = std::min(lastTick, sourceEnd);
        const auto xAt = [&](double tick) {
            return area.getX() + juce::roundToInt((tick - firstTick) / (lastTick - firstTick) * area.getWidth());
        };
        const auto secondsAt = [&](double tick) {
            const auto n = static_cast<juce::int64>(std::floor(tick));
            return clip.startFrame / data.rate + clock.secondsAt(n) - onset
                   + (tick - n) * (clock.secondsAt(n + 1) - clock.secondsAt(n));
        };
        g.setColour(juce::Colour(0xffa8e7df));
        // A native WAV runs in seconds; each constant-tempo span has its own mapping to pixels.
        for (double tick = firstTick; tick < end;) {
            const auto change = clock.nextChangeAfter(static_cast<juce::int64>(std::floor(tick)));
            const auto next = change ? std::min(end, double(*change)) : end;
            const auto segment = area.withLeft(xAt(tick)).withRight(xAt(next));
            if (!segment.isEmpty())
                data.thumbnail->drawChannels(g, segment, secondsAt(tick), secondsAt(next), 1.f);
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
