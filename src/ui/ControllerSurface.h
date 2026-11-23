#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

class ControllerSurface final : public juce::Component, private juce::Timer {
public:
    ControllerSurface() {
        addAndMakeVisible(label_);
        addAndMakeVisible(devices_);
        addAndMakeVisible(status);
        label_.setText(TRANS("Mackie Control MIDI input"), juce::dontSendNotification);
        devices_.setTitle(label_.getText());
        devices_.onChange = [this] {
            const int index = devices_.getSelectedId() - 2;
            if (selected) selected(index >= 0 && index < ports_.size() ? ports_[index] : juce::MidiDeviceInfo{});
        };
        addAndMakeVisible(outputLabel_);
        addAndMakeVisible(outputs_);
        outputLabel_.setText(TRANS("Mackie Control feedback output"), juce::dontSendNotification);
        outputs_.setTitle(outputLabel_.getText());
        outputs_.onChange = [this] {
            const int index = outputs_.getSelectedId() - 2;
            if (selectOutput) selectOutput(index >= 0 && index < outputPorts_.size()
                ? outputPorts_[index].identifier : juce::String{});
        };
        setSize(450, 154);
    }
    void refreshPorts(const juce::String& active) {
        ports_ = juce::MidiInput::getAvailableDevices();
        devices_.clear(juce::dontSendNotification);
        devices_.addItem(TRANS("Disconnected"), 1);
        int choice = 1;
        for (int i = 0; i < ports_.size(); ++i) {
            devices_.addItem(ports_[i].name, i + 2);
            if (ports_[i].identifier == active) choice = i + 2;
        }
        devices_.setSelectedId(choice, juce::dontSendNotification);
    }
    void refreshOutputs(const juce::String& active) {
        outputPorts_ = juce::MidiOutput::getAvailableDevices();
        outputs_.clear(juce::dontSendNotification);
        outputs_.addItem(TRANS("Disconnected"), 1);
        int choice = 1;
        for (int i = 0; i < outputPorts_.size(); ++i) {
            outputs_.addItem(outputPorts_[i].name, i + 2);
            if (outputPorts_[i].identifier == active) choice = i + 2;
        }
        outputs_.setSelectedId(choice, juce::dontSendNotification);
        if (active.isNotEmpty()) startTimerHz(15); else stopTimer();
    }
    void resized() override {
        auto area = getLocalBounds().reduced(10);
        label_.setBounds(area.removeFromTop(22));
        devices_.setBounds(area.removeFromTop(27));
        outputLabel_.setBounds(area.removeFromTop(22));
        outputs_.setBounds(area.removeFromTop(27));
        status.setBounds(area);
    }
    std::function<void(juce::MidiDeviceInfo)> selected;
    std::function<void(juce::String)> selectOutput;
    std::function<void()> feedback;
    juce::Label status;
private:
    void timerCallback() override { if (feedback) feedback(); }
    juce::Label label_, outputLabel_;
    juce::ComboBox outputs_;
    juce::Array<juce::MidiDeviceInfo> outputPorts_;
    juce::ComboBox devices_;
    juce::Array<juce::MidiDeviceInfo> ports_;
};
