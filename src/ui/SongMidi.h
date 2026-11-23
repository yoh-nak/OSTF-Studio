#pragma once

#include "ui/OstfpPreview.h"

namespace ostfstudio {
enum class SmfFormat { merged, multitrack };

juce::Result writeSongMidi(const OstfpPreview&, SmfFormat, const std::vector<int>& channels, juce::OutputStream&, std::optional<std::size_t> onlyTrack = {});
}
