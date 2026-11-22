#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PluginBrowser final : public juce::Component, private juce::Timer {
public:
    PluginBrowser();
    ~PluginBrowser() override;
    void resized() override;
    bool hasScanCache() const { return cache_.existsAsFile(); }
    void scan();
    std::optional<juce::PluginDescription> find(const juce::String& identifier) const;
    static int runScanWorker(const juce::String& token);

private:
    void timerCallback() override;
    void refreshTree();

    juce::File cache_, scanStatus_;
    juce::KnownPluginList plugins_;
    std::unique_ptr<juce::ChildProcess> scanProcess_;
    juce::TextButton scanButton_ {"Scan VST3"};
    juce::Label status_;
    double scanProgress_ = -1.0;
    juce::ProgressBar progress_ {scanProgress_};
    std::unique_ptr<juce::TreeViewItem> root_;
    juce::TreeView tree_;
};
