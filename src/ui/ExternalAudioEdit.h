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

// Verify a working copy and prepare a complete replacement before touching the active region.
inline juce::Result prepareEditedWav(const ProjectAudio& clip, const juce::File& file,
                                     const juce::String& originalHash, ProjectAudio& result) {
    const auto original = sourceWavBytes(clip);
    if (original.isEmpty() || juce::SHA256(original).toHexString() != originalHash)
        return juce::Result::fail("The source has changed. Create a new working copy.");
    juce::MemoryBlock edited;
    if (file.getSize() > 16 * 1024 * 1024 || !file.loadFileAsData(edited) || edited.isEmpty())
        return juce::Result::fail("Unsupported WAV");
    if (edited == original)
        return juce::Result::fail("Use Replace Audio in Melodyne, then Apply edited WAV");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> before(wav.createReaderFor(new juce::MemoryInputStream(original, false), true));
    std::unique_ptr<juce::AudioFormatReader> after(wav.createReaderFor(new juce::MemoryInputStream(edited, false), true));
    if (!before || !after || after->sampleRate != before->sampleRate || after->lengthInSamples != before->lengthInSamples
        || after->numChannels < 1 || after->numChannels > 2)
        return juce::Result::fail("Export the full WAV with its original length and sample rate");
    result = clip;
    result.embedded = std::move(edited);
    result.localFile = juce::File{};
    result.path = file.getFileName();
    result.sha256 = juce::SHA256(result.embedded).toHexString();
    result.channels = int(after->numChannels);
    result.sampleRate = int(after->sampleRate);
    result.assetFrames = after->lengthInSamples;
    return juce::Result::ok();
}
}
