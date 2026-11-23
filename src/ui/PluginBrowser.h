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
    bool showingSplice() const { return splicePage_.isVisible(); }
    void selectSplice(bool show);
    void showSpliceEditor(std::unique_ptr<juce::AudioProcessorEditor>);
    void showSpliceMessage(const juce::String&);
    std::function<void()> onSplice, onPageChanged;
    std::optional<juce::PluginDescription> find(const juce::String& identifier) const;
    juce::Array<juce::PluginDescription> types() const { return plugins_.getTypes(); }
    static int runScanWorker(const juce::String& token, const juce::String& target = {});

private:
    void timerCallback() override;
    void refreshTree();
    void registerBundledPlugins();
    void choosePlugin();
    bool startScanner();
    void finishScan(const juce::String& message = {});

    juce::Component pluginPage_, splicePage_;
    juce::TextButton pluginsTab_{TRANS("Plug-ins")}, spliceTab_{"Splice Sounds"};
    juce::Viewport spliceView_;
    juce::Label spliceMessage_;
    juce::TextButton installSplice_{TRANS("Get Splice Sounds")}, scanSplice_{TRANS("Scan VST3")};
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
