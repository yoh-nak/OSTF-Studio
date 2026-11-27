#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

namespace ostfstudio::audioexport {
enum class Format { wav, aiff, flac, vorbis, opus, mp3, aac };
struct Settings {
  Format format = Format::wav;
  int rate = 48000;
  int quality = 1; // Index into options() for this format.
};
const char *extension(Format);
const char *name(Format);
juce::Array<Format> availableFormats();
juce::Array<int> rates(Format);
juce::StringArray options(Format);
int defaultQuality(Format);
constexpr int blockSize = 512;
// The callback fills exactly count stereo frames and returns false to cancel.
using Render = std::function<bool(juce::AudioBuffer<float> &, int)>;
juce::Result encode(const juce::File &file, Settings, juce::int64 frames,
                    const Render &,
                    const std::function<bool()> &cancelled = {});
} // namespace ostfstudio::audioexport
