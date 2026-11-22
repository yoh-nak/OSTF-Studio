#include "ui/DeviceSettingsWindow.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace {
class DeviceSettingsContent final : public juce::Component, private juce::ChangeListener {
public:
    DeviceSettingsContent() : selector_(devices_, 0, 64, 0, 64, true, true, true, false) {
        juce::PropertiesFile::Options options;
        options.applicationName = "OSTF Studio";
        options.filenameSuffix = ".settings";
        options.folderName = "OSTF Studio";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        properties_.setStorageParameters(options);

        const auto saved = properties_.getUserSettings()->getXmlValue("deviceSetup");
        const auto error = devices_.initialise(2, 2, saved.get(), true);
        status_.setText(error.isNotEmpty() ? error :
#if JUCE_WINDOWS && JUCE_ASIO
                        TRANS("For ASIO, choose ASIO and your interface's installed driver."),
#else
                        TRANS("Audio and MIDI settings are saved on this computer."),
#endif
                        juce::dontSendNotification);
        status_.setTooltip("Select the audio driver and devices, and enable the MIDI ports you want to use.");
        addAndMakeVisible(status_);
        viewport_.setViewedComponent(&selector_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        devices_.addChangeListener(this);
        setSize(590, 580);
    }

    ~DeviceSettingsContent() override {
        devices_.removeChangeListener(this);
        save();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        status_.setBounds(bounds.removeFromTop(24));
        viewport_.setBounds(bounds);
        selector_.setSize(juce::jmax(320, bounds.getWidth() - viewport_.getScrollBarThickness()), bounds.getHeight());
    }

    void save() {
        if (auto state = devices_.createStateXml()) {
            if (auto* settings = properties_.getUserSettings()) settings->setValue("deviceSetup", state.get());
            properties_.saveIfNeeded();
        }
    }

    juce::AudioDeviceManager& devices() { return devices_; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { save(); }

    juce::ApplicationProperties properties_;
    juce::AudioDeviceManager devices_;
    juce::AudioDeviceSelectorComponent selector_;
    juce::Viewport viewport_;
    juce::Label status_;
};
}

DeviceSettingsWindow::DeviceSettingsWindow()
    : DocumentWindow(TRANS("Audio / MIDI Settings"), juce::Colour(0xff1b2734), DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(new DeviceSettingsContent(), true);
    setResizable(true, true);
    setResizeLimits(460, 360, 1000, 1000);
    centreWithSize(getWidth(), getHeight());
}

void DeviceSettingsWindow::closeButtonPressed() { setVisible(false); }

juce::AudioDeviceManager& DeviceSettingsWindow::devices() {
    return static_cast<DeviceSettingsContent*>(getContentComponent())->devices();
}
