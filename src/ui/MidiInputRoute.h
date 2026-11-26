#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace ostfstudio {
struct MidiInputRoute {
    juce::String port, name;
    int channel = 0; // Zero accepts every channel on the selected input.
    bool enabled = true;
    bool accepts(const juce::String& source, int incomingChannel) const {
        return enabled && incomingChannel > 0 && incomingChannel <= 16 && (port.isEmpty() || port == source)
            && (channel == 0 || channel == incomingChannel);
    }
};
}
