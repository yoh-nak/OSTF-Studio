#include "ui/PluginBrowser.h"
#include <algorithm>

namespace {
juce::File pluginDataDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OSTF Studio");
}

juce::File statusFile(const juce::String& token) {
    return pluginDataDirectory().getChildFile("vst3-scan-" + token + ".xml");
}

bool writeXml(const juce::File& file, const juce::XmlElement& xml) {
    juce::TemporaryFile temporary(file);
    return xml.writeTo(temporary.getFile()) && temporary.overwriteTargetFileWithTemporary();
}

void writeStatus(const juce::File& file, const juce::String& message, double progress) {
    juce::XmlElement state("SCAN");
    state.setAttribute("message", message);
    state.setAttribute("progress", progress);
    writeXml(file, state);
}

class Item final : public juce::TreeViewItem {
public:
    Item(juce::String label, juce::String id = {}) : label_(std::move(label)), id_(std::move(id)) {}
    bool mightContainSubItems() override { return id_.isEmpty(); }
    juce::String getUniqueName() const override { return id_.isEmpty() ? label_ : id_; }
    juce::var getDragSourceDescription() override {
        return id_.isEmpty() ? juce::var{} : juce::var("ostf-vst3:" + id_);
    }
    void paintItem(juce::Graphics& g, int width, int height) override {
        if (isSelected()) g.fillAll(juce::Colour(0xff364e61));
        g.setFont(juce::FontOptions(13.0f, id_.isEmpty() ? juce::Font::bold : juce::Font::plain));
        g.setColour(id_.isEmpty() ? juce::Colour(0xffe7b46c) : juce::Colours::white);
        g.drawFittedText(label_, 3, 0, width - 6, height, juce::Justification::centredLeft, 1);
    }
private:
    juce::String label_, id_;
};
}

PluginBrowser::PluginBrowser() {
    const auto directory = pluginDataDirectory();
    directory.createDirectory();
    cache_ = directory.getChildFile("vst3-cache.xml");
    if (const auto xml = juce::XmlDocument::parse(cache_)) plugins_.recreateFromXml(*xml);
    addAndMakeVisible(scanButton_);
    addAndMakeVisible(status_);
    addChildComponent(progress_);
    addAndMakeVisible(tree_);
    tree_.setRootItemVisible(false);
    tree_.setDefaultOpenness(true);
    scanButton_.onClick = [this] { scan(); };
    refreshTree();
}

PluginBrowser::~PluginBrowser() { stopTimer(); }

void PluginBrowser::resized() {
    auto area = getLocalBounds().reduced(6);
    scanButton_.setBounds(area.removeFromTop(24));
    status_.setBounds(area.removeFromTop(28));
    if (progress_.isVisible()) progress_.setBounds(area.removeFromTop(14).reduced(0, 2));
    tree_.setBounds(area);
}

std::optional<juce::PluginDescription> PluginBrowser::find(const juce::String& identifier) const {
    for (const auto& plugin : plugins_.getTypes())
        if (plugin.createIdentifierString() == identifier) return plugin;
    return std::nullopt;
}

void PluginBrowser::scan() {
    if (scanProcess_) return;
    const auto token = juce::Uuid().toString();
    scanStatus_ = statusFile(token);
    auto process = std::make_unique<juce::ChildProcess>();
    const juce::StringArray command {juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
                                    "--scan-vst3", token};
    if (!process->start(command, 0)) {
        status_.setText("Could not start VST3 scanner", juce::dontSendNotification);
        return;
    }
    scanProcess_ = std::move(process);
    scanButton_.setEnabled(false);
    status_.setText("Starting VST3 scan...", juce::dontSendNotification);
    scanProgress_ = -1.0;
    progress_.setVisible(true);
    resized();
    startTimer(120);
}

void PluginBrowser::timerCallback() {
    if (const auto state = juce::XmlDocument::parse(scanStatus_)) {
        status_.setText(state->getStringAttribute("message"), juce::dontSendNotification);
        scanProgress_ = state->getDoubleAttribute("progress", -1.0);
    }
    if (scanProcess_->isRunning()) return;

    const auto exit = scanProcess_->getExitCode();
    const auto lastStatus = status_.getText();
    stopTimer();
    scanProcess_.reset();
    scanStatus_.deleteFile();
    scanButton_.setEnabled(true);
    progress_.setVisible(false);
    if (const auto xml = juce::XmlDocument::parse(cache_)) plugins_.recreateFromXml(*xml);
    refreshTree();
    if (exit != 0) status_.setText(exit == 2 ? "Another VST3 scan is running" : "Scan stopped: " + lastStatus,
                                   juce::dontSendNotification);
    resized();
}

int PluginBrowser::runScanWorker(const juce::String& token) {
    if (token.length() != 32 || !token.containsOnly("abcdefABCDEF0123456789")) return 1;
    const auto directory = pluginDataDirectory();
    if (directory.createDirectory().failed()) return 1;
    const auto status = statusFile(token);
    juce::InterProcessLock lock("OSTFStudio-VST3-library-scan");
    if (!lock.enter(0)) return 2;
    const auto cache = directory.getChildFile("vst3-cache.xml");
    juce::KnownPluginList library;
    if (const auto xml = juce::XmlDocument::parse(cache)) library.recreateFromXml(*xml);
    juce::VST3PluginFormat format;
    writeStatus(status, "Finding installed VST3 plug-ins...", -1.0);
    juce::PluginDirectoryScanner scanner(library, format, format.getDefaultLocationsToSearch(), true,
                                         directory.getChildFile("vst3-scan-in-progress.txt"));
    bool more;
    do {
        const auto next = scanner.getNextPluginFileThatWillBeScanned();
        if (next.isNotEmpty()) writeStatus(status, "Scanning: " + juce::File(next).getFileName(), scanner.getProgress());
        juce::String scanned;
        more = scanner.scanNextFile(true, scanned);
        if (const auto xml = library.createXml(); xml && !writeXml(cache, *xml)) return 1;
    } while (more);
    writeStatus(status, "VST3 scan complete", 1.0);
    return 0;
}

void PluginBrowser::refreshTree() {
    const auto types = plugins_.getTypes();
    auto next = std::make_unique<Item>("VST3");
    juce::StringArray manufacturers;
    for (const auto& plugin : types)
        manufacturers.addIfNotAlreadyThere(plugin.manufacturerName.isEmpty() ? "Unknown manufacturer" : plugin.manufacturerName);
    manufacturers.sort(true);
    for (const auto& maker : manufacturers) {
        auto group = std::make_unique<Item>(maker);
        std::vector<juce::PluginDescription> matches;
        for (const auto& plugin : types)
            if ((plugin.manufacturerName.isEmpty() ? "Unknown manufacturer" : plugin.manufacturerName) == maker) matches.push_back(plugin);
        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
        for (const auto& plugin : matches)
            group->addSubItem(new Item(plugin.name, plugin.createIdentifierString()));
        next->addSubItem(group.release());
    }
    tree_.setRootItem(next.get());
    root_ = std::move(next);
    status_.setText(types.isEmpty() ? "No cached VST3 plug-ins" : juce::String(types.size()) + " VST3 / drag to a track",
                    juce::dontSendNotification);
}
