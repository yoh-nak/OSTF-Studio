#pragma once
#include "core/Smk25Preset.h"
#include <juce_core/juce_core.h>

namespace ostfstudio::smkble {
struct Device { juce::String name; std::uint64_t address; };
struct Result {
    juce::String message;
    std::vector<Device> devices;
    std::optional<Smk25Preset> preset;
    juce::File backup;
};
#if JUCE_WINDOWS && defined(_MSC_VER)
Result discover();
Result transfer(const Device&, int slot, const std::optional<Smk25Preset>& toWrite);
#endif
}
