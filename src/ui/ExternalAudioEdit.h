#pragma once
#include "ui/OstfpPreview.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

namespace ostfstudio {
inline juce::MemoryBlock sourceWavBytes(const ProjectAudio& clip) {
    if (!clip.embedded.isEmpty()) return clip.embedded;
    juce::MemoryBlock bytes;
    if (clip.localFile.getSize() <= 16 * 1024 * 1024 && clip.localFile.loadFileAsData(bytes)) return bytes;
    return {};
}

inline juce::int64 editableClipFrames(const ProjectAudio& clip, const TempoMap& clock, juce::int64 frames, double rate, juce::int64& first) {
    first = clip.startFrame;
    if (clip.durationTicks <= 0) return frames;
    auto end = clip.startTick + clip.durationTicks;
    if (!clip.timing.loop && clip.timing.sourceLimitTicks) end = std::min(end, clip.sourceTick() + clip.timing.sourceLimitTicks);
    const double fraction = clip.timing.cycleTicks ? double(end - clip.sourceTick()) / clip.timing.cycleTicks
        : (clock.secondsAt(end) - clock.secondsAt(clip.sourceTick())) * rate / frames;
    const auto start = clip.timing.phaseOffset;
    // A range crossing a loop seam edits the shared source cycle.
    if (clip.timing.loop && start + fraction > 1.) return frames;
    const auto offset = std::llround(clip.timing.sourcePhase(start) * frames);
    first += offset;
    const auto last = std::llround(clip.timing.sourcePhase(std::clamp(start + fraction, start, 1.)) * frames);
    return std::max<juce::int64>(0, last - offset);
}
inline std::unique_ptr<juce::AudioFormatReader> wavReader(const juce::MemoryBlock& data) {
    return std::unique_ptr<juce::AudioFormatReader>(juce::WavAudioFormat{}.createReaderFor(new juce::MemoryInputStream(data, false), true));
}
inline std::unique_ptr<juce::AudioFormatWriter> wavWriter(juce::MemoryBlock& output, juce::AudioFormatReader& source) {
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(output, false);
    return juce::WavAudioFormat{}.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(source.sampleRate).withNumChannels(int(source.numChannels)).withBitsPerSample(int(source.bitsPerSample)));
}
inline juce::MemoryBlock clipWav(const ProjectAudio& clip, const juce::MemoryBlock& source, const TempoMap& clock,
                                 juce::int64& frames, juce::int64* sourceStart = nullptr) {
    auto reader = wavReader(source);
    if (!reader || clip.startFrame < 0 || clip.startFrame >= reader->lengthInSamples) return {};
    const auto available = reader->lengthInSamples - clip.startFrame;
    juce::int64 first = 0;
    frames = editableClipFrames(clip, clock, clip.frameCount > 0 ? std::min(available, clip.frameCount) : available, reader->sampleRate, first);
    if (sourceStart) *sourceStart = first;
    if (!frames) return {};
    if (first == 0 && frames == reader->lengthInSamples) return source;
    juce::MemoryBlock bytes;
    auto writer = wavWriter(bytes, *reader);
    if (!writer || !writer->writeFromAudioReader(*reader, first, frames)) return {};
    writer.reset();
    return bytes;
}

inline juce::Result prepareEditedWav(const ProjectAudio& clip, const juce::File& file,
                                     const juce::String& originalHash, ProjectAudio& result, juce::int64 frames = -1, juce::int64 sourceStart = -1) {
    const auto original = sourceWavBytes(clip);
    if (original.isEmpty() || juce::SHA256(original).toHexString() != originalHash)
        return juce::Result::fail("The source has changed. Create a new working copy.");
    juce::MemoryBlock edited;
    if (file.getSize() > 16 * 1024 * 1024 || !file.loadFileAsData(edited) || edited.isEmpty())
        return juce::Result::fail("Unsupported WAV");
    auto before = wavReader(original), after = wavReader(edited);
    if (!before || !after) return juce::Result::fail("Unsupported WAV");
    const auto first = frames < 0 ? 0 : (sourceStart < 0 ? clip.startFrame : sourceStart);
    if (frames < 0) frames = before->lengthInSamples;
    if (first < 0 || frames <= 0 || frames > before->lengthInSamples - first ||
        after->lengthInSamples != frames || before->sampleRate != after->sampleRate || before->numChannels != after->numChannels)
        return juce::Result::fail("Keep the clip length, channels and sample rate when exporting from Melodyne.");
    if (first != 0 || frames != before->lengthInSamples) {
        juce::MemoryBlock joined;
        auto writer = wavWriter(joined, *before);
        const auto tail = before->lengthInSamples - first - frames;
        if (!writer || (first && !writer->writeFromAudioReader(*before, 0, first))
            || !writer->writeFromAudioReader(*after, 0, frames)
            || (tail && !writer->writeFromAudioReader(*before, first + frames, tail)))
            return juce::Result::fail("Could not save the working WAV");
        writer.reset();
        edited = std::move(joined);
    }
    if (edited == original) return juce::Result::fail("Use Replace Audio in Melodyne, then Apply edited WAV");
    result = clip;
    result.embedded = std::move(edited);
    result.localFile = juce::File{};
    result.path = file.getFileName();
    result.sha256 = juce::SHA256(result.embedded).toHexString();
    result.channels = int(before->numChannels);
    result.sampleRate = int(before->sampleRate);
    result.assetFrames = before->lengthInSamples;
    return juce::Result::ok();
}
}
