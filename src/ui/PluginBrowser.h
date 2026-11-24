#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PluginBrowser final : public juce::Component, private juce::Timer, private juce::FileBrowserListener {
public:
    PluginBrowser();
    ~PluginBrowser() override;
    void resized() override;
    bool hasScanCache() const { return cache_.existsAsFile(); }
    void scan();
    void showSamples(bool);
    std::function<void(bool)> onTabChanged;
    static bool supportsSample(const juce::File& file) { return file.hasFileExtension("wav;aif;aiff;flac;mp3"); }
    std::optional<juce::PluginDescription> find(const juce::String& identifier) const;
    juce::Array<juce::PluginDescription> types() const { return plugins_.getTypes(); }
    static int runScanWorker(const juce::String& token, const juce::String& target = {});

private:
    void timerCallback() override;
    void selectionChanged() override;
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File&) override {}
    void browserRootChanged(const juce::File&) override { selectionChanged(); }
    juce::TextButton pluginTab_ {TRANS("Plug-ins")}, sampleTab_ {TRANS("Audio files")}, folder_ {TRANS("Select sample folder...")};
    juce::WildcardFileFilter sampleFilter_ {"*.wav;*.aif;*.aiff;*.flac;*.mp3", "*", TRANS("Audio samples")};
    std::unique_ptr<juce::FileBrowserComponent> samples_;
    std::unique_ptr<juce::FileChooser> folderChooser_;
    bool showingSamples_ = false;
    void refreshTree();
    void registerBundledPlugins();
    void choosePlugin();
    bool startScanner();
    void finishScan(const juce::String& message = {});

    juce::File cache_, scanStatus_;
    juce::KnownPluginList plugins_;
    std::unique_ptr<juce::ChildProcess> scanProcess_;
    juce::Time lastStatusWrite_;
    double progressUpdatedAt_ = 0.0;
    juce::String lastScanMessage_, lastSkipped_;
    int skipped_ = 0;
    juce::TextButton scanButton_ {TRANS("Scan VST3")};
    juce::TextButton scanOneButton_ {TRANS("Scan one...")};
    juce::ComboBox filter_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String scanTarget_;
    juce::Label status_;
    double scanProgress_ = -1.0;
    juce::ProgressBar progress_ {scanProgress_};
    std::unique_ptr<juce::TreeViewItem> root_;
    juce::TreeView tree_;
};
