#pragma once
#include "ui/AudioTrackInput.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

class DeviceSettingsWindow final : public juce::DocumentWindow {
public:
    explicit DeviceSettingsWindow(std::function<void(int)> outputsChanged = {});
    std::array<float,64> takeInputPeaks();
    void closeButtonPressed() override;
    bool enableTrackInput(const juce::MidiDeviceInfo&);
    juce::AudioDeviceManager& devices();
    juce::AudioDeviceManager& midiDevices();
};
