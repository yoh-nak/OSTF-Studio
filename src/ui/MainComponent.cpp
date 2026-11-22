#include "ui/MainComponent.h"
#include "ui/DemoArrangement.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
constexpr auto bg = 0xff15181d;
constexpr auto panel = 0xff242a31;
constexpr auto muted = 0xffaab6c4;
constexpr auto accent = 0xff46bdb2;
constexpr auto hardwareAmber = 0xffe7b46c;
constexpr int menuHeight = 28;
constexpr int trackHeaderWidth = 286;
constexpr int trackNameWidth = 95;
constexpr int arrangementHeadingHeight = 24;

int arrangementLaneHeight(juce::Rectangle<int> inner, const ostfstudio::ArrangementView& view) {
    const auto rows = 1 + static_cast<int>(view.globalLanes.size() + view.trackLanes.size());
    return juce::jmin(42, (inner.getHeight() - arrangementHeadingHeight) / rows);
}

juce::Rectangle<int> workspaceBounds(juce::Rectangle<int> bounds) {
    bounds.reduce(18, 18);
    bounds.removeFromTop(54 + menuHeight);
    bounds.removeFromBottom(26);
    return bounds;
}

enum MenuCommand { openFile = 1, openDemoSong, showPiano, showMixer, showDevices, showAbout, showPlugins };

class PluginWindow final : public juce::DocumentWindow {
public:
    explicit PluginWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour(panel), DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};
}

MainComponent::MainComponent() {
    setSize(1100, 690);
    menuBar_.setModel(this);
    menuBar_.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff20262d));
    addAndMakeVisible(menuBar_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(recordButton_);
    addAndMakeVisible(pluginBrowserButton_);
    addAndMakeVisible(transportPosition_);
    transportPosition_.setText("Bar 1", juce::dontSendNotification);
    playButton_.onClick = [this] { startTransport(false); };
    stopButton_.onClick = [this] { stopTransport(); };
    recordButton_.onClick = [this] { startTransport(true); };
    recordButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
    pluginBrowserButton_.onClick = [this] { togglePluginBrowser(); };
    addChildComponent(pianoButton_);
    addChildComponent(mixerButton_);
    addChildComponent(trackPicker_);
    addChildComponent(mixer_);
    pluginFormats_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    trackPicker_.setTitle("Arrangement track");
    trackPicker_.setTextWhenNothingSelected("Select track");
    trackPicker_.onChange = [this] {
        if (const auto id = trackPicker_.getSelectedId(); id > 0) selectTrack(static_cast<std::size_t>(id - 1));
    };
    pianoButton_.onClick = [this] {
        mixerButton_.setToggleState(false, juce::dontSendNotification);
        refreshEditor();
    };
    mixerButton_.onClick = [this] {
        mixerButton_.setToggleState(true, juce::dontSendNotification);
        refreshEditor();
    };
    mixer_.channelChanged = [this](std::size_t index) {
        auto& controls = *trackControls_[index];
        const auto& mix = mixer_.channel(index);
        controls.mute.setToggleState(mix.muted, juce::dontSendNotification);
        controls.solo.setToggleState(mix.soloed, juce::dontSendNotification);
        controls.volume.setValue(mix.volumeDb, juce::dontSendNotification);
        updateTransportMix();
    };
    mixer_.routingChanged = [this] { updateTransportMix(); };
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel_.setFont(juce::FontOptions(13.0f));
    openDemo();
}

juce::StringArray MainComponent::getMenuBarNames() { return {"File", "View", "Settings", "Help"}; }

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu menu;
    switch (index) {
        case 0:
            menu.addItem(openFile, "Open OSTFP / Inspect OSTF...");
            menu.addItem(openDemoSong, "Open demo song");
            break;
        case 1:
            menu.addItem(showPiano, "Piano Roll", project_ != nullptr, !mixerButton_.getToggleState());
            menu.addItem(showMixer, "Mixer", project_ != nullptr, mixerButton_.getToggleState());
            menu.addItem(showPlugins, "Plug-in Browser", true, pluginBrowserButton_.getToggleState());
            break;
        case 2: menu.addItem(showDevices, "Audio / MIDI Devices..."); break;
        case 3: menu.addItem(showAbout, "About OSTF Studio"); break;
    }
    return menu;
}

void MainComponent::menuItemSelected(int item, int) {
    switch (item) {
        case openFile: inspectOSTF(); break;
        case openDemoSong: openDemo(); break;
        case showPiano:
        case showMixer:
            mixerButton_.setToggleState(item == showMixer, juce::dontSendNotification);
            refreshEditor();
            break;
        case showDevices:
            if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
            deviceSettings_->setVisible(true);
            deviceSettings_->toFront(true);
            break;
        case showPlugins: togglePluginBrowser(); break;
        case showAbout:
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "About OSTF Studio", "OSTF 5.0 preview with a timeline, piano roll and session mixer.");
            break;
    }
}

void MainComponent::togglePluginBrowser() {
    const bool shown = !pluginBrowserButton_.getToggleState();
    if (shown && !pluginBrowser_) {
        pluginBrowser_ = std::make_unique<PluginBrowser>();
        addAndMakeVisible(*pluginBrowser_);
        if (!pluginBrowser_->hasScanCache()) pluginBrowser_->scan();
    }
    pluginBrowserButton_.setToggleState(shown, juce::dontSendNotification);
    pluginBrowser_->setVisible(shown);
    resized();
    repaint();
}

juce::Rectangle<int> MainComponent::getWorkspace() const {
    auto bounds = workspaceBounds(getLocalBounds());
    if (pluginBrowserButton_.getToggleState()) bounds.removeFromRight(juce::jmin(230, bounds.getWidth() / 3) + 8);
    return bounds;
}

int MainComponent::getArrangementHeight(int workspaceHeight) const {
    if (!mixerButton_.getToggleState() || !project_) return workspaceHeight * 3 / 5;
    const auto rows = 1 + static_cast<int>(project_->globalLanes.size() + project_->trackLanes.size());
    return std::max(workspaceHeight / 2, 26 + arrangementHeadingHeight + rows * 12);
}

bool MainComponent::isInterestedInDragSource(const SourceDetails& details) {
    return project_ && pluginBrowser_ && details.description.toString().startsWith("ostf-vst3:");
}

void MainComponent::itemDropped(const SourceDetails& details) {
    if (!isInterestedInDragSource(details)) return;
    const auto plugin = pluginBrowser_->find(details.description.toString().substring(10));
    if (!plugin) return;
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (!trackRows_[i].contains(details.localPosition)) continue;
        auto& strip = *trackControls_[i];
        if (strip.loading) return;
        strip.pluginPath = juce::File(plugin->fileOrIdentifier);
        loadTrackPlugin(i, *plugin);
        return;
    }
}

void MainComponent::updateTransportMix() {
    if (!project_) return;
    for (std::size_t i = 0; i < trackControls_.size(); ++i)
        transport_.setChannelGain(i, mixer_.gains(i));
}

void MainComponent::startTransport(bool recording) {
    if (!project_ || (recording && !selectedTrack_)) return;
    if (transportActive_) stopTransport();
    if (!deviceSettings_) deviceSettings_ = std::make_unique<DeviceSettingsWindow>();
    updateTransportMix();
    const auto result = transport_.start(deviceSettings_->devices(), *project_, recording);
    if (result.failed()) {
        statusLabel_.setText(result.getErrorMessage(), juce::dontSendNotification);
        return;
    }
    transportActive_ = true;
    recordTrack_ = recording ? selectedTrack_ : std::nullopt;
    playButton_.setToggleState(!recording, juce::dontSendNotification);
    recordButton_.setToggleState(recording, juce::dontSendNotification);
    statusLabel_.setText(recording ? "Recording MIDI into the selected track" : "Playing pitched notes / preview instrument",
                         juce::dontSendNotification);
    startTimerHz(24);
}

void MainComponent::stopTransport() {
    if (!transportActive_) return;
    auto recorded = transport_.stop();
    if (recordTrack_ && project_) {
        auto& notes = project_->pitchedNotes[*recordTrack_];
        notes.insert(notes.end(), recorded.begin(), recorded.end());
        selectTrack(*recordTrack_);
        statusLabel_.setText(juce::String(static_cast<int>(recorded.size())) + " MIDI notes captured / current session",
                             juce::dontSendNotification);
    } else {
        statusLabel_.setText("Stopped", juce::dontSendNotification);
    }
    recordTrack_.reset();
    transportActive_ = false;
    playButton_.setToggleState(false, juce::dontSendNotification);
    recordButton_.setToggleState(false, juce::dontSendNotification);
    transportPosition_.setText("Bar 1", juce::dontSendNotification);
    stopTimer();
    repaint();
}

void MainComponent::timerCallback() {
    if (!transport_.isRunning()) { stopTransport(); return; }
    transportPosition_.setText("Bar " + juce::String(1 + transport_.positionTick() / project_->ticksPerBar), juce::dontSendNotification);
    repaint();
}

void MainComponent::openDemo() {
    juce::MemoryInputStream stream(BinaryData::_10comprehensivecore_ostfp,
                                   BinaryData::_10comprehensivecore_ostfpSize, false);
    openPreview(stream);
}

void MainComponent::inspectOSTF() {
    chooser_ = std::make_unique<juce::FileChooser>("Open an OSTFP package or inspect OSTF JSON", juce::File{}, "*.ostfp;*.ostf;*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser) {
            if (self == nullptr) return;
            auto selected = chooser.getResult();
            if (selected == juce::File{}) return;
            juce::FileInputStream input(selected);
            if (!input.openedOk()) {
                self->statusLabel_.setText("Could not read: " + selected.getFileName(), juce::dontSendNotification);
                return;
            }
            if (selected.hasFileExtension(".ostfp")) {
                self->openPreview(input);
                return;
            }
            juce::var parsed;
            auto result = juce::JSON::parse(input.readEntireStreamAsString(), parsed);
            if (result.wasOk() && !parsed.isVoid()) {
                self->statusLabel_.setText("JSON object or array read: " + selected.getFileName(), juce::dontSendNotification);
            } else {
                self->statusLabel_.setText("Expected a JSON object or array: " + selected.getFileName(), juce::dontSendNotification);
            }
        });
}

void MainComponent::openPreview(juce::InputStream& input) {
    stopTransport();
    const auto result = ostfstudio::readOstfpPreview(input, project_);
    if (result.wasOk()) {
        ++projectGeneration_;
        selectedTrack_.reset();
        trackRows_.clear();
        trackPicker_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < project_->trackLanes.size(); ++i)
            trackPicker_.addItem(juce::String::fromUTF8(project_->trackLanes[i].label), static_cast<int>(i + 1));
        trackPicker_.setEnabled(!project_->trackLanes.empty());
        mixer_.setTracks(*project_);
        rebuildTrackControls();
        const auto first = std::find_if(project_->pitchedNotes.begin(), project_->pitchedNotes.end(),
                                        [](const auto& notes) { return !notes.empty(); });
        if (first != project_->pitchedNotes.end())
            selectTrack(static_cast<std::size_t>(first - project_->pitchedNotes.begin()));
        else if (!project_->trackLanes.empty())
            selectTrack(0);
        else {
            mixerButton_.setToggleState(false, juce::dontSendNotification);
            refreshEditor();
        }
    }
    statusLabel_.setText(result.wasOk() ? project_->title + " / " + juce::String(project_->bars.endBar - 1) + " bars / Read-only"
                                       : result.getErrorMessage(),
                         juce::dontSendNotification);
    repaint();
}

void MainComponent::rebuildTrackControls() {
    trackControls_.clear();
    for (std::size_t i = 0; i < project_->trackLanes.size(); ++i) {
        auto controls = std::make_unique<TrackControls>();
        const auto name = juce::String::fromUTF8(project_->trackLanes[i].label);
        controls->mute.setTitle(name + " mute");
        controls->mute.setTooltip("Mute " + name);
        controls->mute.setClickingTogglesState(true);
        controls->mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffa94f43));
        controls->solo.setTitle(name + " solo");
        controls->solo.setTooltip("Solo " + name);
        controls->solo.setClickingTogglesState(true);
        controls->solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8a622b));
        controls->volume.setName(name + " volume");
        controls->volume.setTooltip("Volume in dB. Double-click to reset to 0 dB.");
        controls->volume.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        controls->volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 41, 16);
        controls->volume.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(hardwareAmber));
        controls->volume.setRange(-60.0, 6.0, 0.1);
        controls->volume.setValue(0.0, juce::dontSendNotification);
        controls->volume.setDoubleClickReturnValue(true, 0.0);
        controls->mute.onClick = [this, i, control = &controls->mute] { mixer_.setMuted(i, control->getToggleState()); };
        controls->solo.onClick = [this, i, control = &controls->solo] { mixer_.setSoloed(i, control->getToggleState()); };
        controls->volume.onValueChange = [this, i, control = &controls->volume] { mixer_.setVolumeDb(i, control->getValue()); };
        controls->plugin.setTooltip("Assign a local plug-in to " + name);
        controls->plugin.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25221e));
        controls->plugin.setColour(juce::TextButton::textColourOffId, juce::Colour(hardwareAmber));
        controls->plugin.onClick = [this, i] { showTrackPluginMenu(i); };
        addAndMakeVisible(controls->mute);
        addAndMakeVisible(controls->solo);
        addAndMakeVisible(controls->volume);
        addAndMakeVisible(controls->plugin);
        trackControls_.push_back(std::move(controls));
    }
    resized();
}

void MainComponent::showTrackPluginMenu(std::size_t index) {
    auto& control = *trackControls_[index];
    juce::PopupMenu menu;
    menu.addItem(1, "Choose plug-in file...", !control.loading);
    menu.addItem(3, "Choose plug-in bundle folder...", !control.loading);
    menu.addItem(4, "Load VST3", control.pluginPath.hasFileExtension("vst3") && !control.loading);
    menu.addItem(5, "Open plug-in editor", control.instance != nullptr && !control.loading);
    menu.addSeparator();
    menu.addItem(2, "Clear assignment", control.pluginPath != juce::File{} && !control.loading);
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&control.plugin),
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_](int selection) {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            if (selection == 1 || selection == 3) self->chooseTrackPlugin(index, selection == 3);
            if (selection == 4) self->scanTrackPlugin(index);
            if (selection == 5) self->openTrackPluginEditor(index);
            if (selection == 2) {
                auto& strip = *self->trackControls_[index];
                strip.editor.reset();
                strip.instance.reset();
                strip.pluginPath = juce::File{};
                strip.plugin.setButtonText("Plug-in");
                strip.plugin.setTooltip("Assign a local plug-in to this track");
                self->statusLabel_.setText("Plug-in assignment cleared", juce::dontSendNotification);
            }
        });
}

void MainComponent::chooseTrackPlugin(std::size_t index, bool bundleDirectory) {
#if JUCE_MAC
    const auto directory = juce::File("/Library/Audio/Plug-Ins");
    const auto filter = "*.vst3;*.component";
#else
    const auto commonFiles = juce::SystemStats::getEnvironmentVariable("CommonProgramFiles", {});
    const auto directory = commonFiles.isEmpty() ? juce::File{} : juce::File(commonFiles).getChildFile("VST3");
    const auto filter = "*.vst3";
#endif
    pluginChooser_ = std::make_unique<juce::FileChooser>("Assign a local plug-in", directory, filter);
    const auto selectionMode = bundleDirectory ? juce::FileBrowserComponent::canSelectDirectories
                                               : juce::FileBrowserComponent::canSelectFiles;
    pluginChooser_->launchAsync(juce::FileBrowserComponent::openMode | selectionMode,
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_, filter](const juce::FileChooser& picker) {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            const auto file = picker.getResult();
            if (file == juce::File{}) return;
            const bool supported = file.hasFileExtension("vst3")
#if JUCE_MAC
                || file.hasFileExtension("component")
#endif
                ;
            if (!file.exists() || !supported) {
                self->statusLabel_.setText("Choose a plug-in matching " + juce::String(filter), juce::dontSendNotification);
                return;
            }
            auto& strip = *self->trackControls_[index];
            strip.editor.reset();
            strip.instance.reset();
            strip.pluginPath = file;
            strip.plugin.setButtonText(file.getFileNameWithoutExtension());
            strip.plugin.setTooltip(file.getFileName() + " / assigned");
            const auto detail = file.hasFileExtension("vst3") ? " / Choose Load VST3 to configure" : " / AU assignment only";
            self->statusLabel_.setText("Assigned " + file.getFileName() + detail, juce::dontSendNotification);
        });
}

void MainComponent::scanTrackPlugin(std::size_t index) {
    auto& strip = *trackControls_[index];
    if (strip.loading || !strip.pluginPath.exists() || !strip.pluginPath.hasFileExtension("vst3")) return;
    strip.loading = true;
    strip.plugin.setButtonText("Reading...");
    statusLabel_.setText("Reading VST3 components: " + strip.pluginPath.getFileName(), juce::dontSendNotification);
    juce::Timer::callAfterDelay(65, [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_] {
        if (self != nullptr && self->projectGeneration_ == generation) self->discoverTrackPlugin(index);
    });
}

void MainComponent::discoverTrackPlugin(std::size_t index) {
    auto& strip = *trackControls_[index];
    juce::OwnedArray<juce::PluginDescription> found;
    pluginFormats_.getFormat(0)->findAllTypesForFile(found, strip.pluginPath.getFullPathName());
    strip.loading = false;
    strip.plugin.setButtonText(strip.instance ? strip.instance->getName() : strip.pluginPath.getFileNameWithoutExtension());
    if (found.isEmpty()) {
        statusLabel_.setText("No VST3 component found in " + strip.pluginPath.getFileName(), juce::dontSendNotification);
        return;
    }
    if (found.size() == 1) {
        loadTrackPlugin(index, *found[0]);
        return;
    }
    juce::PopupMenu menu;
    std::vector<juce::PluginDescription> choices;
    for (const auto* description : found) {
        choices.push_back(*description);
        menu.addItem(static_cast<int>(choices.size()), description->name);
    }
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&strip.plugin),
        [self = juce::Component::SafePointer<MainComponent>(this), choices = std::move(choices),
         index, generation = projectGeneration_](int item) {
            if (self != nullptr && self->projectGeneration_ == generation && item > 0)
                self->loadTrackPlugin(index, choices[static_cast<std::size_t>(item - 1)]);
        });
}

void MainComponent::loadTrackPlugin(std::size_t index, const juce::PluginDescription& description) {
    auto& strip = *trackControls_[index];
    if (strip.loading || description.fileOrIdentifier != strip.pluginPath.getFullPathName()) return;
    strip.loading = true;
    strip.plugin.setButtonText("Loading...");
    statusLabel_.setText("Loading plug-in: " + description.name, juce::dontSendNotification);
    juce::Timer::callAfterDelay(65,
        [self = juce::Component::SafePointer<MainComponent>(this), index, generation = projectGeneration_, description] {
            if (self == nullptr || self->projectGeneration_ != generation) return;
            self->pluginFormats_.createPluginInstanceAsync(description, 44100.0, 512,
                [self, index, generation](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                    if (self == nullptr || self->projectGeneration_ != generation) return;
                    auto& target = *self->trackControls_[index];
                    target.loading = false;
                    if (!instance) {
                        target.plugin.setButtonText(target.instance ? target.instance->getName()
                                                                    : target.pluginPath.getFileNameWithoutExtension());
                        self->statusLabel_.setText("VST3 load failed: " + error, juce::dontSendNotification);
                        return;
                    }
                    target.editor.reset();
                    target.instance = std::move(instance);
                    target.plugin.setButtonText(target.instance->getName());
                    self->openTrackPluginEditor(index);
                });
        });
}

void MainComponent::openTrackPluginEditor(std::size_t index) {
    auto& strip = *trackControls_[index];
    if (!strip.instance) return;
    if (!strip.editor) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(strip.instance->createEditorAndMakeActive());
        if (!editor) editor = std::make_unique<juce::GenericAudioProcessorEditor>(*strip.instance);
        auto window = std::make_unique<PluginWindow>(strip.instance->getName());
        window->setContentOwned(editor.release(), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        strip.editor = std::move(window);
    }
    strip.editor->setVisible(true);
    strip.editor->toFront(true);
    statusLabel_.setText("VST3 loaded / playback routing pending", juce::dontSendNotification);
}

void MainComponent::mouseDown(const juce::MouseEvent& event) {
    for (std::size_t i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_[i].contains(event.getPosition())) {
            selectTrack(i);
            return;
        }
    }
}

void MainComponent::selectTrack(std::size_t index) {
    if (!project_ || index >= project_->trackLanes.size()) return;
    selectedTrack_ = index;
    trackPicker_.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification);
    mixerButton_.setToggleState(false, juce::dontSendNotification);
    refreshEditor();
}

void MainComponent::refreshEditor() {
    const bool showMixer = mixerButton_.getToggleState();
    pianoButton_.setToggleState(!showMixer, juce::dontSendNotification);
    pianoButton_.setVisible(project_ != nullptr);
    mixerButton_.setVisible(project_ != nullptr);
    trackPicker_.setVisible(project_ != nullptr);
    mixer_.setVisible(project_ != nullptr && showMixer);
    resized();
    repaint();
}

void MainComponent::resized() {
    menuBar_.setBounds(getLocalBounds().removeFromTop(menuHeight));
    auto transportRow = getLocalBounds().withTrimmedTop(menuHeight + 14).removeFromTop(28).withTrimmedLeft(221).withTrimmedRight(18);
    playButton_.setBounds(transportRow.removeFromLeft(54).reduced(2, 0));
    stopButton_.setBounds(transportRow.removeFromLeft(54).reduced(2, 0));
    recordButton_.setBounds(transportRow.removeFromLeft(94).reduced(2, 0));
    transportPosition_.setBounds(transportRow.removeFromLeft(72));
    pluginBrowserButton_.setBounds(transportRow.removeFromRight(78));
    statusLabel_.setBounds(20, getHeight() - 37, getWidth() - 40, 24);
    auto editor = getWorkspace();
    if (pluginBrowser_) {
        const auto full = workspaceBounds(getLocalBounds());
        pluginBrowser_->setBounds(full.withLeft(editor.getRight() + 8));
    }
    const auto arrangerHeight = getArrangementHeight(editor.getHeight());
    auto arrangement = editor.withHeight(arrangerHeight).reduced(13);
    if (project_) {
        const auto view = project_->view();
        const auto height = arrangementLaneHeight(arrangement, view);
        const int trackY = arrangement.getY() + (1 + static_cast<int>(view.globalLanes.size())) * height + arrangementHeadingHeight;
        for (std::size_t i = 0; i < trackControls_.size(); ++i) {
            auto row = juce::Rectangle<int>(arrangement.getX() + trackNameWidth, trackY + static_cast<int>(i) * height,
                                            trackHeaderWidth - trackNameWidth - 3, height - 4);
            auto& strip = *trackControls_[i];
            strip.mute.setBounds(row.removeFromLeft(29).reduced(2));
            strip.solo.setBounds(row.removeFromLeft(29).reduced(2));
            strip.volume.setBounds(row.removeFromLeft(74).reduced(2, 0));
            strip.plugin.setBounds(row.reduced(1));
        }
    }
    editor.removeFromTop(arrangerHeight + 8);
    editor.reduce(6, 4);
    auto toolbar = editor.removeFromTop(26);
    pianoButton_.setBounds(toolbar.removeFromLeft(96));
    toolbar.removeFromLeft(5);
    mixerButton_.setBounds(toolbar.removeFromLeft(74));
    toolbar.removeFromLeft(12);
    trackPicker_.setBounds(toolbar.removeFromLeft(180));
    mixer_.setBounds(editor);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(bg));
    auto area = getLocalBounds().reduced(18);
    area.removeFromTop(menuHeight);
    auto top = area.removeFromTop(54);
    auto title = top.removeFromLeft(190);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    g.drawText("OSTF Studio", title.removeFromTop(29), juce::Justification::centredLeft);
    g.setColour(juce::Colour(muted));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(project_ ? "GLOBAL MUSICAL TIMELINE" : "GLOBAL MUSICAL TIMELINE / DEMO", title, juce::Justification::centredLeft, 1);
    area = getWorkspace();
    trackRows_.clear();
    if (!project_) {
        drawArrangement(g, area, ostfstudio::demo::arrangement);
        return;
    }

    const auto arrangementArea = area.removeFromTop(getArrangementHeight(area.getHeight()));
    area.removeFromTop(8);
    drawArrangement(g, arrangementArea, project_->view());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff303840), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.removeFromTop(30);
    if (!mixerButton_.getToggleState()) drawPianoRoll(g, area);
}

void MainComponent::drawArrangement(juce::Graphics& g, juce::Rectangle<int> area,
                                    const ostfstudio::ArrangementView& view) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff313941), 0.0f, static_cast<float>(area.getY()),
                                          juce::Colour(panel), 0.0f, static_cast<float>(area.getBottom()), false));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 13);
    const int laneHeight = arrangementLaneHeight(area, view);
    auto labels = area.removeFromLeft(project_ ? trackHeaderWidth : 108);
    if (laneHeight < 12 || area.getWidth() < 40) return;
    const int rowHeight = laneHeight - 4;

    auto drawRow = [&](const char* label, int y) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(juce::String::fromUTF8(label),
                         {labels.getX() + 5, y, trackNameWidth - 10, rowHeight},
                         juce::Justification::centredLeft, 1);
        const auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        return row;
    };

    const auto bars = view.bars;
    auto clipBounds = [bars](ostfstudio::BarSpan clip, juce::Rectangle<int> row) {
        const auto extent = ostfstudio::projectBarSpan(clip, bars);
        if (!extent) return juce::Rectangle<int>{};
        const auto left = juce::roundToInt(extent->start * row.getWidth());
        const auto right = juce::roundToInt(extent->end * row.getWidth());
        return juce::Rectangle<int>(row.getX() + left, row.getY(), right - left, row.getHeight());
    };

    const int tickStep = juce::jmax(1, (bars.endBar - bars.firstBar) / (area.getWidth() < 420 ? 4 : 8));
    const auto ruler = drawRow("BARS", area.getY());
    for (int bar = bars.firstBar; bar < bars.endBar; bar += tickStep) {
        const auto cell = clipBounds({bar, bar + tickStep}, ruler);
        g.setColour(juce::Colour(muted));
        g.drawText(juce::String(bar), cell.withTrimmedLeft(5), juce::Justification::centredLeft);
    }

    auto drawLane = [&](const ostfstudio::ArrangementLane& lane, int y, juce::Colour colour) {
        const auto row = drawRow(lane.label, y);
        g.setColour(juce::Colour(0xff3a4e5e));
        for (int bar = bars.firstBar + tickStep; bar < bars.endBar; bar += tickStep) {
            const auto cell = clipBounds({bar, bar + tickStep}, row);
            g.drawVerticalLine(cell.getX(), static_cast<float>(row.getY()), static_cast<float>(row.getBottom()));
        }

        for (const auto& region : lane.regions) {
            const auto bounds = clipBounds(region.bars, row).reduced(2, 2);
            if (bounds.isEmpty()) continue;
            g.setColour(colour);
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.93f));
            g.drawFittedText(juce::String::fromUTF8(region.label), bounds.reduced(4, 0), juce::Justification::centredLeft, 1);
        }
    };

    int y = area.getY() + laneHeight;
    const juce::uint32 globalColours[] {0xff314d72, 0xff493c64, 0xff31495a, 0xff31404e, 0xff31404e, 0xff2c6058};
    for (std::size_t i = 0; i < view.globalLanes.size(); ++i) {
        drawLane(view.globalLanes[i], y, juce::Colour(globalColours[i % std::size(globalColours)]));
        y += laneHeight;
    }

    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(project_ ? "ARRANGEMENT TRACKS / OSTFP" : "ARRANGEMENT TRACKS / DEMO", labels.getX() + 4, y,
               labels.getWidth() + area.getWidth() - 4, arrangementHeadingHeight, juce::Justification::centredLeft);
    y += arrangementHeadingHeight;

    const juce::uint32 trackColours[] {0xff2a657d, 0xff265e57, 0xff4b517f, 0xff744366, 0xff526070};
    for (std::size_t i = 0; i < view.trackLanes.size(); ++i) {
        const auto channel = juce::Rectangle<int>(labels.getX(), y, labels.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff353d45));
        g.fillRoundedRectangle(channel.toFloat().reduced(1.0f), 3.0f);
        g.setColour(juce::Colour(0xff525d67));
        g.drawHorizontalLine(y + 1, static_cast<float>(channel.getX() + 4), static_cast<float>(channel.getRight() - 4));
        drawLane(view.trackLanes[i], y, juce::Colour(trackColours[i % std::size(trackColours)]));
        if (project_) {
            const juce::Rectangle<int> hit(labels.getX(), y, labels.getWidth() + area.getWidth(), rowHeight);
            trackRows_.push_back(hit);
            if (selectedTrack_ == i) {
                g.setColour(juce::Colour(accent));
                g.drawRoundedRectangle(hit.toFloat().reduced(0.5f), 4.0f, 1.5f);
            }
        }
        y += laneHeight;
    }
    if (transportActive_ && project_) {
        const auto ticks = static_cast<double>(project_->ticksPerBar) * (bars.endBar - bars.firstBar);
        const auto x = area.getX() + juce::roundToInt(static_cast<double>(transport_.positionTick()) / ticks * area.getWidth());
        g.setColour(juce::Colour(hardwareAmber));
        g.drawVerticalLine(x, static_cast<float>(ruler.getY()), static_cast<float>(y - 4));
    }
}

void MainComponent::drawPianoRoll(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 8);
    const auto heading = area.removeFromTop(24);
    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    const auto name = selectedTrack_ ? "PIANO ROLL  /  " + juce::String::fromUTF8(project_->trackLanes[*selectedTrack_].label)
                                     : juce::String("PIANO ROLL");
    g.drawFittedText(name, heading, juce::Justification::centredLeft, 1);
    if (!selectedTrack_ || project_->pitchedNotes[*selectedTrack_].empty()) {
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(selectedTrack_ ? "This track has no pitched notes." : "Select an arrangement track to inspect its notes.",
                   area, juce::Justification::centred);
        return;
    }

    const auto& notes = project_->pitchedNotes[*selectedTrack_];
    auto firstTick = notes.front().startTick;
    auto lastTick = notes.front().endTick;
    int low = notes.front().pitch;
    int high = low;
    for (const auto& note : notes) {
        firstTick = std::min(firstTick, note.startTick);
        lastTick = std::max(lastTick, note.endTick);
        low = std::min(low, note.pitch);
        high = std::max(high, note.pitch);
    }
    low = std::max(0, low - 3);
    high = std::min(127, high + 3);
    const auto barLength = project_->ticksPerBar;
    const auto firstBar = firstTick / barLength;
    const auto endBar = (lastTick - 1) / barLength + 1;
    const auto origin = firstBar * barLength;
    const auto length = static_cast<double>((endBar - firstBar) * barLength);
    auto keys = area.removeFromLeft(52);
    const auto ruler = area.removeFromTop(18);
    keys.removeFromTop(18);
    if (area.isEmpty()) return;

    const auto rowHeight = static_cast<float>(area.getHeight()) / static_cast<float>(high - low + 1);
    auto noteY = [&](int pitch) { return static_cast<float>(area.getY()) + static_cast<float>(high - pitch) * rowHeight; };
    auto tickX = [&](juce::int64 tick) {
        return static_cast<float>(area.getX() + static_cast<double>(tick - origin) / length * area.getWidth());
    };
    constexpr bool isBlack[] {false, true, false, true, false, false, true, false, true, false, true, false};
    constexpr const char* pitchNames[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    g.setFont(juce::FontOptions(10.0f));
    for (int pitch = high; pitch >= low; --pitch) {
        const auto y = noteY(pitch);
        const bool black = isBlack[pitch % 12];
        g.setColour(juce::Colour(black ? 0xff192735 : 0xff233443));
        g.fillRect(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), rowHeight);
        const auto key = juce::Rectangle<float>(static_cast<float>(keys.getX()), y, static_cast<float>(keys.getWidth() - 2), rowHeight);
        g.setColour(juce::Colour(black ? 0xff243242 : 0xff526578));
        g.fillRect(key.reduced(0, 0.5f));
        if (rowHeight >= 11.0f || (pitch % 12 == 0 && rowHeight * 12.0f >= 12.0f)) {
            g.setColour(juce::Colours::white);
            g.drawText(juce::String(pitchNames[pitch % 12]) + juce::String(pitch / 12 - 1),
                       key.toNearestInt(), juce::Justification::centred);
        }
    }

    const auto barStep = std::max<juce::int64>(1, (endBar - firstBar + 7) / 8);
    for (auto bar = firstBar; bar < endBar; bar += barStep) {
        const auto x = juce::roundToInt(tickX(bar * barLength));
        g.setColour(juce::Colour(0xff526578));
        g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
        g.setColour(juce::Colour(muted));
        g.drawText(juce::String(bar + 1), x + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft);
    }

    g.saveState();
    g.reduceClipRegion(area);
    g.setColour(juce::Colour(accent));
    for (const auto& note : notes) {
        const auto x = tickX(note.startTick);
        const auto width = std::max(2.0f, tickX(note.endTick) - x);
        g.fillRoundedRectangle(x, noteY(note.pitch), width, std::max(1.0f, rowHeight - 1.0f), 2.0f);
    }
    g.restoreState();
}
