#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio {
struct OstfpPreview;
enum class SynthVParts { lead, chorus, all };
juce::Result writeSynthV(const OstfpPreview&, SynthVParts, juce::OutputStream&);
}
