#include "ui/PluginBrowser.h"
#include "ui/PluginLibrary.h"
#include "ui/PluginRoles.h"
#include "StockEffects.h"
#include <algorithm>

namespace {
int browserCategory(const juce::PluginDescription& plugin) {
    return plugin.isInstrument ? 2 : 3;
}

juce::String displayManufacturer(const juce::PluginDescription& plugin) {
    return plugin.manufacturerName.isEmpty() ? TRANS("Unknown manufacturer") : plugin.manufacturerName;
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
    Item(juce::String label, juce::String id = {}, int category = 0)
        : label_(std::move(label)), id_(std::move(id)), category_(category) {}
    bool mightContainSubItems() override { return id_.isEmpty(); }
    juce::String getUniqueName() const override { return id_.isEmpty() ? label_ : id_; }
    juce::var getDragSourceDescription() override {
        return id_.isEmpty() ? juce::var{} : juce::var("ostf-vst3:" + id_);
    }
    void paintItem(juce::Graphics& g, int width, int height) override {
        if (isSelected()) g.fillAll(juce::Colour(0xff364e61));
        g.setFont(juce::FontOptions(13.0f, id_.isEmpty() ? juce::Font::bold : juce::Font::plain));
        g.setColour(id_.isEmpty() ? juce::Colour(0xffe7b46c) : juce::Colours::white);
        const int textX = id_.isEmpty() ? 3 : 22;
        if (id_.isNotEmpty()) {
            const float y = static_cast<float>(height) / 2.0f;
            g.setColour(juce::Colour(category_ == 2 ? 0xff73d8cc : 0xffe7b46c));
            if (category_ == 4) {
                g.setColour(juce::Colour(0xff9fbaff));
                for (float x : {4.0f, 9.0f, 14.0f})
                    g.fillRoundedRectangle(x, y - 4.0f, 3.0f, 8.0f, 1.0f);
            } else if (category_ == 2) {
                g.drawRoundedRectangle(3.0f, y - 5.0f, 14.0f, 10.0f, 1.5f, 1.0f);
                for (float x : {7.0f, 12.0f}) g.fillRect(x, y - 4.5f, 2.0f, 7.0f);
            } else {
                juce::Path wave;
                wave.startNewSubPath(3.0f, y);
                wave.lineTo(6.0f, y);
                wave.lineTo(8.0f, y - 5.0f);
                wave.lineTo(12.0f, y + 5.0f);
                wave.lineTo(14.0f, y);
                wave.lineTo(17.0f, y);
                g.strokePath(wave, juce::PathStrokeType(1.4f));
            }
            g.setColour(juce::Colours::white);
        }
        g.drawFittedText(label_, textX, 0, width - textX - 3, height, juce::Justification::centredLeft, 1);
    }
private:
    juce::String label_, id_;
    int category_;
};
}

PluginBrowser::PluginBrowser() {
    const auto directory = pluginDataDirectory();
    directory.createDirectory();
    cache_ = directory.getChildFile("vst3-cache.xml");
    if (const auto xml = juce::XmlDocument::parse(cache_)) plugins_.recreateFromXml(*xml);
    registerBundledPlugins();
    pluginTab_.setComponentID("browser-plugins");
    sampleTab_.setComponentID("browser-samples");
    addAndMakeVisible(pluginTab_);
    addAndMakeVisible(sampleTab_);
    addChildComponent(folder_);
    pluginTab_.onClick = [this] { showSamples(false); };
    sampleTab_.onClick = [this] { showSamples(true); };
    pluginTab_.setToggleState(true, juce::dontSendNotification);
    folder_.onClick = [this] {
        folderChooser_ = std::make_unique<juce::FileChooser>(TRANS("Select media folder..."), samples_->getRoot());
        folderChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<PluginBrowser>(this)](const juce::FileChooser& chooser) {
                if (safe && chooser.getResult().isDirectory()) safe->samples_->setRoot(chooser.getResult());
            });
    };
    addAndMakeVisible(scanButton_);
    addAndMakeVisible(scanOneButton_);
    addAndMakeVisible(filter_);
    addAndMakeVisible(manufacturer_);
    manufacturer_.setComponentID("plugin-manufacturer");
    manufacturer_.setTitle(TRANS("Manufacturer"));
    manufacturer_.onChange = [this] { refreshTree(); };
    addAndMakeVisible(search_);
    search_.setComponentID("plugin-search");
    search_.setTitle(TRANS("Search plug-ins"));
    search_.setTextToShowWhenEmpty(TRANS("Search by name or manufacturer"),juce::Colours::lightgrey);
    search_.onTextChange = [this] { refreshTree(); };
    search_.onEscapeKey = [this] { search_.clear(); };
    filter_.addItem(TRANS("All plug-ins"), 1);
    filter_.addItem(TRANS("Instruments"), 2);
    filter_.addItem(TRANS("Effects"), 3);
    filter_.setSelectedId(1, juce::dontSendNotification);
    filter_.onChange = [this] { refreshTree(); };
    scanOneButton_.onClick = [this] { choosePlugin(); };
    addAndMakeVisible(status_);
    addChildComponent(progress_);
    addAndMakeVisible(tree_);
    tree_.setRootItemVisible(false);
    tree_.setDefaultOpenness(true);
    scanButton_.onClick = [this] {
        if (!scanProcess_) scan();
        else if (scanProcess_->kill()) finishScan(TRANS("Scan canceled; saved results retained"));
        else status_.setText(TRANS("Could not stop the scanner"), juce::dontSendNotification);
    };
    refreshTree();
}

PluginBrowser::~PluginBrowser() {
    stopTimer();
    if (samples_) samples_->removeListener(this);
    tree_.setRootItem(nullptr);
    if (scanProcess_ && scanProcess_->isRunning()) scanProcess_->kill();
    if (scanStatus_ != juce::File{}) scanStatus_.deleteFile();
}

void PluginBrowser::showSamples(bool show) {
    if (show && !samples_) {
        using Browser = juce::FileBrowserComponent;
        samples_ = std::make_unique<Browser>(Browser::openMode | Browser::canSelectFiles | Browser::useTreeView
            | Browser::filenameBoxIsReadOnly, juce::File::getSpecialLocation(juce::File::userMusicDirectory), &sampleFilter_, nullptr);
        addChildComponent(*samples_);
        samples_->addListener(this);
        selectionChanged();
    }
    showingSamples_ = show;
    if (onTabChanged) onTabChanged(show);
    sampleTab_.setToggleState(show, juce::dontSendNotification);
    pluginTab_.setToggleState(!show, juce::dontSendNotification);
    resized();
}

void PluginBrowser::selectionChanged() {
    const auto file = samples_->getHighlightedFile();
    if (auto* tree = dynamic_cast<juce::FileTreeComponent*>(samples_->getDisplayComponent()))
        tree->setDragAndDropDescription(file.existsAsFile() && supportsSample(file)
            ? "ostf-media:" + file.getFullPathName() : juce::String{});
}

void PluginBrowser::resized() {
    auto area = getLocalBounds().reduced(6);
    auto tabs = area.removeFromTop(28);
    pluginTab_.setBounds(tabs.removeFromLeft(tabs.getWidth()/2).reduced(1,2));
    sampleTab_.setBounds(tabs.reduced(1,2));
    for (juce::Component* control : std::array<juce::Component*,7>{&scanButton_, &scanOneButton_, &filter_, &manufacturer_, &search_, &status_, &tree_})
        control->setVisible(!showingSamples_);
    progress_.setVisible(!showingSamples_ && scanProcess_ != nullptr);
    folder_.setVisible(showingSamples_);
    if (samples_) samples_->setVisible(showingSamples_);
    if (showingSamples_) {
        folder_.setBounds(area.removeFromTop(28).reduced(1,2));
        samples_->setBounds(area);
        return;
    }
    auto buttons = area.removeFromTop(24);
    scanOneButton_.setBounds(buttons.removeFromRight(buttons.getWidth() / 2).reduced(2, 0));
    scanButton_.setBounds(buttons.reduced(2, 0));
    filter_.setBounds(area.removeFromTop(28).reduced(2, 2));
    manufacturer_.setBounds(area.removeFromTop(28).reduced(2, 2));
    search_.setBounds(area.removeFromTop(27).reduced(2,1));
    status_.setBounds(area.removeFromTop(28));
    if (progress_.isVisible()) progress_.setBounds(area.removeFromTop(14).reduced(0, 2));
    tree_.setBounds(area);
}

std::optional<juce::PluginDescription> PluginBrowser::find(const juce::String& identifier) const {
    for (const auto& plugin : catalog_)
        if (plugin.createIdentifierString() == identifier) return plugin;
    if (const auto saved = plugins_.getTypeForIdentifierString(identifier))
        for (const auto& plugin : catalog_)
            if (sameVst3Class(*saved, plugin)) return plugin;
    return std::nullopt;
}

void PluginBrowser::scan() {
    if (scanProcess_) return;
    skipped_ = 0;
    lastSkipped_.clear();
    if (!startScanner()) return;
    scanButton_.setButtonText(TRANS("Cancel scan"));
    scanOneButton_.setEnabled(false);
    progress_.setVisible(true);
    resized();
    startTimer(120);
}

void PluginBrowser::choosePlugin() {
    if (scanProcess_) return;
    const auto locations = juce::VST3PluginFormat{}.getDefaultLocationsToSearch();
    juce::File start;
    for (int i = 0; i < locations.getNumPaths(); ++i) {
        if (locations[i].isDirectory()) {
            start = locations[i];
            break;
        }
    }
    // The Windows native dialog cannot select both files and bundle directories.
    chooser_ = std::make_unique<juce::FileChooser>(TRANS("Select a VST3 file or bundle"), start, "*.vst3", useNativeVst3Chooser);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<PluginBrowser>(this)](const juce::FileChooser& chooser) {
        if (safe == nullptr || safe->scanProcess_) return;
        const auto file = chooser.getResult();
        if (file == juce::File{}) return;
        if (!file.exists() || !file.hasFileExtension("vst3")) {
            safe->status_.setText(TRANS("Select a .vst3 file or bundle"), juce::dontSendNotification);
            return;
        }
        safe->scanTarget_ = file.getFullPathName();
        safe->scan();
        if (!safe->scanProcess_) safe->scanTarget_.clear();
    });
}

bool PluginBrowser::startScanner() {
    const auto token = juce::Uuid().toString();
    scanStatus_ = statusFile(token);
    auto process = std::make_unique<juce::ChildProcess>();
    const auto scanner = studioToolsExecutable();
    if (!scanner.existsAsFile()) {
        status_.setText(TRANS("Install OSTF Studio to scan new plug-ins."), juce::dontSendNotification);
        return false;
    }
    juce::StringArray command {scanner.getFullPathName(),
                               "--scan-vst3", token};
    if (scanTarget_.isNotEmpty()) command.add(scanTarget_);
    if (!process->start(command, 0)) {
        status_.setText(TRANS("Could not start VST3 scanner"), juce::dontSendNotification);
        return false;
    }
    scanProcess_ = std::move(process);
    lastStatusWrite_ = {};
    progressUpdatedAt_ = juce::Time::getMillisecondCounterHiRes();
    lastScanMessage_ = skipped_ == 0 ? TRANS("Starting VST3 scan...") : TRANS("Resuming after ") + lastSkipped_;
    status_.setText(lastScanMessage_, juce::dontSendNotification);
    scanProgress_ = -1.0;
    return true;
}

void PluginBrowser::timerCallback() {
    const auto timestamp = scanStatus_.getLastModificationTime();
    if (timestamp != lastStatusWrite_) {
        if (const auto state = juce::XmlDocument::parse(scanStatus_)) {
            lastStatusWrite_ = timestamp;
            progressUpdatedAt_ = juce::Time::getMillisecondCounterHiRes();
            lastScanMessage_ = state->getStringAttribute("message");
            status_.setText(lastScanMessage_, juce::dontSendNotification);
            scanProgress_ = state->getDoubleAttribute("progress", -1.0);
        }
    }
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - progressUpdatedAt_;
    const bool running = scanProcess_->isRunning();
    const bool loadingPlugin = lastScanMessage_.startsWith("Scanning: ");
    if (running && elapsed < (loadingPlugin ? 25000.0 : 60000.0)) {
        if (loadingPlugin && elapsed >= 6000.0)
            status_.setText(TRANS("Waiting: ") + lastScanMessage_.substring(10), juce::dontSendNotification);
        return;
    }
    if (running && !scanProcess_->kill()) {
        status_.setText(TRANS("Scanner did not respond to stop"), juce::dontSendNotification);
        return;
    }

    const auto exit = scanProcess_->getExitCode();
    if (scanTarget_.isEmpty() && exit != 2 && (running || exit != 0) && loadingPlugin && skipped_ < 12
        && lastSkipped_ != lastScanMessage_.substring(10)) {
        lastSkipped_ = lastScanMessage_.substring(10);
        ++skipped_;
        scanStatus_.deleteFile();
        scanProcess_.reset();
        if (startScanner()) return;
        finishScan(TRANS("Could not restart scanner; saved results retained"));
        return;
    }
    finishScan(exit == 2 ? TRANS("Another VST3 scan is running")
               : exit == 3 ? TRANS("No VST3 type found in ") + juce::File(scanTarget_).getFileName()
               : running || exit != 0 ? TRANS("Scan stopped: ") + lastScanMessage_
               : lastScanMessage_.startsWith("Scan complete") ? lastScanMessage_
               : skipped_ > 0 ? TRANS("Scan complete; skipped ") + juce::String(skipped_) + TRANS(" unresponsive plug-ins") : juce::String{});
}

void PluginBrowser::finishScan(const juce::String& message) {
    stopTimer();
    scanProcess_.reset();
    scanStatus_.deleteFile();
    scanButton_.setButtonText(TRANS("Scan VST3"));
    scanOneButton_.setEnabled(true);
    scanTarget_.clear();
    progress_.setVisible(false);
    if (const auto xml = juce::XmlDocument::parse(cache_)) plugins_.recreateFromXml(*xml);
    registerBundledPlugins();
    refreshTree();
    if (message.isNotEmpty()) status_.setText(message, juce::dontSendNotification);
    resized();
}

int PluginBrowser::runScanWorker(const juce::String& token, const juce::String& target) {
    if (token.length() != 32 || !token.containsOnly("abcdefABCDEF0123456789")) return 1;
    const auto directory = pluginDataDirectory();
    if (directory.createDirectory().failed()) return 1;
    const auto status = statusFile(token);
    juce::InterProcessLock lock("OSTFStudio-VST3-library-scan");
    if (!lock.enter(1000)) return 2;
    const auto cache = directory.getChildFile("vst3-cache.xml");
    juce::KnownPluginList library;
    if (const auto xml = juce::XmlDocument::parse(cache)) library.recreateFromXml(*xml);
    juce::VST3PluginFormat format;
    writeStatus(status, "Finding installed VST3 plug-ins...", -1.0);
    const auto guard = directory.getChildFile("vst3-scan-in-progress.txt");
    if (target.isNotEmpty()) {
        const juce::File plugin(target);
        if (!plugin.exists() || !plugin.hasFileExtension("vst3")) return 3;
        library.removeFromBlacklist(target);
        auto previous = juce::StringArray::fromLines(guard.loadFileAsString());
        previous.removeString(target);
        if (!guard.replaceWithText(previous.joinIntoString("\n"))) return 1;
    }
    const bool single = target.isNotEmpty();
    juce::PluginDirectoryScanner scanner(library, format, format.getDefaultLocationsToSearch(), true,
                                         guard);
    if (single) scanner.setFilesOrIdentifiersToScan({target});
    int blocked = 0;
    bool more;
    do {
        const auto next = scanner.getNextPluginFileThatWillBeScanned();
        if (library.getBlacklistedFiles().contains(next)) {
            ++blocked;
            more = scanner.skipNextFile();
            continue;
        }
        if (next.isNotEmpty()) writeStatus(status, "Scanning: " + juce::File(next).getFileName(), scanner.getProgress());
        juce::String scanned;
        more = scanner.scanNextFile(!single, scanned);
        if (const auto xml = library.createXml(); xml && !writeXml(cache, *xml)) return 1;
    } while (more);
    if (single && (library.getBlacklistedFiles().contains(target) || !scanner.getFailedFiles().isEmpty())) return 3;
    const auto missing = blocked + scanner.getFailedFiles().size();
    writeStatus(status, missing > 0 ? "Scan complete; " + juce::String(missing) + " skipped or not recognized"
                                    : single ? "Scan complete: " + juce::File(target).getFileName()
                                             : "VST3 scan complete", 1.0);
    return 0;
}

void PluginBrowser::registerBundledPlugins() {
    registerBundledVst3(plugins_, "OSTF GM Sound Module.vst3");
    for (const auto& instrument : ostfstudio::stockInstruments) registerBundledVst3(plugins_, juce::String(instrument.name) + ".vst3");
    for (const auto& fx : ostfstudio::stockEffects) registerBundledVst3(plugins_, juce::String(fx.name) + ".vst3");
    catalog_ = pluginCatalog(plugins_.getTypes(),
        studioBundleDirectory(juce::File::getSpecialLocation(juce::File::currentExecutableFile)));
}

void PluginBrowser::refreshTree() {
    juce::StringArray allMakers;
    for (const auto& type : catalog_) allMakers.addIfNotAlreadyThere(displayManufacturer(type), true);
    allMakers.sort(true);
    const auto selected = manufacturer_.getSelectedId() > 1 ? manufacturer_.getText() : juce::String{};
    manufacturer_.clear(juce::dontSendNotification);
    manufacturer_.addItem(TRANS("All manufacturers"), 1);
    manufacturer_.addItemList(allMakers, 2);
    const auto index = allMakers.indexOf(selected, true);
    manufacturer_.setSelectedId(index + 2, juce::dontSendNotification);
    auto types = catalog_;
    const auto filter = filter_.getSelectedId();
    const auto words = juce::StringArray::fromTokens(search_.getText().trim()," \t",{});
    for (int i = types.size(); --i >= 0;) {
        const auto& plugin = types.getReference(i);
        const auto text = plugin.name + " " + displayManufacturer(plugin) + " " + plugin.category;
        if ((index >= 0 && !displayManufacturer(plugin).equalsIgnoreCase(selected))
            || (filter != 1 && browserCategory(plugin) != filter)
            || std::any_of(words.begin(),words.end(),[&](const auto& word){return !text.containsIgnoreCase(word);}))
            types.remove(i);
    }
    auto next = std::make_unique<Item>("VST3");
    juce::StringArray manufacturers;
    for (const auto& plugin : types)
        manufacturers.addIfNotAlreadyThere(displayManufacturer(plugin));
    manufacturers.sort(true);
    for (const auto& maker : manufacturers) {
        auto group = std::make_unique<Item>(maker);
        std::vector<juce::PluginDescription> matches;
        for (const auto& plugin : types)
            if (displayManufacturer(plugin) == maker) matches.push_back(plugin);
        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
        for (const auto& plugin : matches)
            group->addSubItem(new Item(plugin.name, plugin.createIdentifierString(), browserCategory(plugin)));
        next->addSubItem(group.release());
    }
    tree_.setRootItem(next.get());
    root_ = std::move(next);
    if (!scanProcess_)
        status_.setText(types.isEmpty() ? TRANS("No VST3 in this view") : juce::String(types.size()) + TRANS(" VST3 / drag to a track"),
                        juce::dontSendNotification);
}
