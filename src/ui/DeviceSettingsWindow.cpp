#include "ui/DeviceSettingsWindow.h"
#include "ui/MidiActivityLamp.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace {
class AudioInputPeaks final : public juce::AudioIODeviceCallback {
public:
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        const auto mask = device->getActiveInputChannels();
        count = 0;
        for (int i = 0; i < 64; ++i) if (mask[i]) physical[count++] = i;
        clear();
    }
    void audioDeviceStopped() override { clear(); }
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                         int numOutputs, int samples, const juce::AudioIODeviceCallbackContext&) override {
        for (int i = 0; i < numOutputs; ++i) if (outputs[i]) juce::FloatVectorOperations::clear(outputs[i], samples);
        for (int i = 0; i < std::min(count, numInputs); ++i) {
            if (!inputs[i] || samples == 0) continue;
            const auto range = juce::FloatVectorOperations::findMinAndMax(inputs[i], samples);
            const float peak = std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
            auto& slot = peaks[physical[i]];
            auto old = slot.load();
            while (old < peak && !slot.compare_exchange_weak(old, peak)) {}
        }
    }
    std::array<float, 64> take() {
        std::array<float, 64> result{};
        for (size_t i = 0; i < peaks.size(); ++i) result[i] = peaks[i].exchange(0);
        return result;
    }
private:
    void clear() { for (auto& p : peaks) p.store(0); }
    int count = 0;
    std::array<int,64> physical{};
    std::array<std::atomic<float>,64> peaks{};
};

class DeviceSettingsContent final : public juce::Component, private juce::ChangeListener, private juce::MidiInputCallback, private juce::AsyncUpdater {
public:
    DeviceSettingsContent() {
        juce::PropertiesFile::Options options;
        options.applicationName = "OSTF Studio";
        options.filenameSuffix = ".settings";
        options.folderName = "OSTF Studio";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        properties_.setStorageParameters(options);

        saved_ = properties_.getUserSettings()->getXmlValue("deviceSetup");
        if (saved_) {
            for (const auto* input : saved_->getChildWithTagNameIterator("MIDIINPUT")) {
                const auto id = input->getStringAttribute("identifier");
                if (id.isNotEmpty()) devices_.setMidiInputDeviceEnabled(id, true);
            }
        }
        devices_.addMidiInputDeviceCallback({}, this);
        activity_.setText(TRANS("MIDI input activity"), juce::dontSendNotification);
        addAndMakeVisible(lamp_);
        addAndMakeVisible(activity_);
        setSize(590, 610);
    }

    AudioInputPeaks peaks;

    void prepareAudio() {
        if (selector_) return;
        // A control surface can already be open while the transport is stopped.
        // Restoring the saved audio setup must preserve those current MIDI inputs.
        const auto ports = juce::MidiInput::getAvailableDevices();
        juce::StringArray enabled;
        for (const auto& port : ports)
            if (devices_.isMidiInputDeviceEnabled(port.identifier)) enabled.add(port.identifier);
        const auto error = devices_.initialise(2, 2, saved_.get(), true);
        for (const auto& port : ports)
            if (devices_.isMidiInputDeviceEnabled(port.identifier) != enabled.contains(port.identifier))
                devices_.setMidiInputDeviceEnabled(port.identifier, enabled.contains(port.identifier));
        selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(devices_, 0, 64, 0, 64, true, true, true, false);
        status_.setText(error.isNotEmpty() ? error :
#if JUCE_WINDOWS && JUCE_ASIO
                        TRANS("For ASIO, choose ASIO and your interface's installed driver."),
#else
                        TRANS("Audio and MIDI settings are saved on this computer."),
#endif
                        juce::dontSendNotification);
        status_.setTooltip("Select the audio driver and devices, and enable the MIDI ports you want to use.");
        addAndMakeVisible(status_);
        viewport_.setViewedComponent(selector_.get(), false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        devices_.addAudioCallback(&peaks);
        devices_.addChangeListener(this);
        resized();
    }

    ~DeviceSettingsContent() override {
        devices_.removeMidiInputDeviceCallback({}, this);
        cancelPendingUpdate();
        devices_.removeChangeListener(this);
        if (selector_) { devices_.removeAudioCallback(&peaks); save(); }
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        status_.setBounds(bounds.removeFromTop(24));
        auto signal = bounds.removeFromTop(23);
        lamp_.setBounds(signal.removeFromLeft(20));
        activity_.setBounds(signal);
        viewport_.setBounds(bounds);
        if (selector_) selector_->setSize(juce::jmax(320, bounds.getWidth() - viewport_.getScrollBarThickness()), bounds.getHeight());
    }

    void save() {
        if (auto state = devices_.createStateXml()) {
            if (auto* settings = properties_.getUserSettings()) settings->setValue("deviceSetup", state.get());
            properties_.saveIfNeeded();
        }
    }

    juce::AudioDeviceManager& devices() { return devices_; }

    bool enableTrackInput(const juce::MidiDeviceInfo& port) {
        devices_.setMidiInputDeviceEnabled(port.identifier, true);
        if (!devices_.isMidiInputDeviceEnabled(port.identifier)) return false;
        if (selector_) { save(); return true; }
        // Opening a track input alone must preserve the saved audio driver and channels.
        if (!saved_) saved_ = std::make_unique<juce::XmlElement>("DEVICESETUP");
        for (auto* elm : saved_->getChildWithTagNameIterator("MIDIINPUT"))
            if (elm->getStringAttribute("identifier") == port.identifier) return true;
        auto* entry = saved_->createNewChildElement("MIDIINPUT");
        entry->setAttribute("identifier", port.identifier);
        entry->setAttribute("name", port.name);
        properties_.getUserSettings()->setValue("deviceSetup", saved_.get());
        properties_.saveIfNeeded();
        return true;
    }

private:
    MidiActivityLamp lamp_;
    juce::Label activity_;
    juce::SpinLock activityLock_;
    juce::String lastPort_;
    int lastChannel_ = 0;
    void handleIncomingMidiMessage(juce::MidiInput* input, const juce::MidiMessage& message) override {
        if (!input) return;
        {
            const juce::SpinLock::ScopedLockType lock(activityLock_);
            lastPort_ = input->getName(); lastChannel_ = message.getChannel();
        }
        triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override {
        if (!isShowing()) return;
        juce::String port;
        int channel;
        {
            const juce::SpinLock::ScopedLockType lock(activityLock_);
            port = lastPort_; channel = lastChannel_;
        }
        lamp_.pulse();
        activity_.setText("MIDI IN  " + port + (channel > 0 ? "  Ch " + juce::String(channel) : juce::String{}), juce::dontSendNotification);
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { save(); }

    juce::ApplicationProperties properties_;
    juce::AudioDeviceManager devices_;
    std::unique_ptr<juce::XmlElement> saved_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
    juce::Viewport viewport_;
    juce::Label status_;
};
}

DeviceSettingsWindow::DeviceSettingsWindow()
    : DocumentWindow(TRANS("Audio / MIDI Settings"), juce::Colour(0xff1b2734), DocumentWindow::closeButton, false) {
    setUsingNativeTitleBar(true);
    setContentOwned(new DeviceSettingsContent(), true);
    setResizable(true, true);
    setResizeLimits(460, 360, 1000, 1000);
    centreWithSize(getWidth(), getHeight());
}

void DeviceSettingsWindow::closeButtonPressed() { setVisible(false); }

juce::AudioDeviceManager& DeviceSettingsWindow::devices() {
    static_cast<DeviceSettingsContent*>(getContentComponent())->prepareAudio();
    return midiDevices();
}

juce::AudioDeviceManager& DeviceSettingsWindow::midiDevices() {
    return static_cast<DeviceSettingsContent*>(getContentComponent())->devices();
}

bool DeviceSettingsWindow::enableTrackInput(const juce::MidiDeviceInfo& port) {
    return static_cast<DeviceSettingsContent*>(getContentComponent())->enableTrackInput(port);
}

std::array<float,64> DeviceSettingsWindow::takeInputPeaks() {
    return static_cast<DeviceSettingsContent*>(getContentComponent())->peaks.take();
}
