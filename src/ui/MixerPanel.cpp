#include "ui/MixerPanel.h"
#include "ui/ConsoleMeterFace.h"
#include "ui/ConsoleLookAndFeel.h"
#include <algorithm>
#include <numeric>

namespace {
constexpr int stripWidth = 246, outputWidth = 200, masterWidth = 304;

const char* groupName(ostfstudio::MixerTrackGroup group) {
    using enum ostfstudio::MixerTrackGroup;
    switch (group) {
        case voicing: return "Voicing";
        case melody: return "Melody";
        case vocal: return "Vocal";
        case pitched: return "Pitched";
        case other: return "Arrangement";
    }
    return "";
}
}

MixerPanel::Send::Send() {
    addAndMakeVisible(name);
    addAndMakeVisible(knob);
    knob.getProperties().set("consoleHardware", true);
    knob.setRange(0,100,1);
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,46,16);
    knob.setTextValueSuffix("%");
    name.setFont(juce::FontOptions(11.f,juce::Font::bold));
}
void MixerPanel::Send::resized() {
    auto row=getLocalBounds();
    name.setBounds(row.removeFromTop(15));
    knob.setBounds(row.removeFromLeft(74));
    dial=row;
}
void MixerPanel::Send::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff546068));
    g.drawHorizontalLine(0,0.f,float(getWidth()));
    ostfstudio::console::drawChannelVu(g,dial.reduced(1,2).toFloat(),vu,2,"SEND");
}

MixerPanel::Strip::Strip(const juce::String& title, const juce::String& group, bool track) : isTrack(track) {
    for (auto* label : {&name, &classification, &status}) label->setInterceptsMouseClicks(false, false);
    name.setText(title, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    classification.setText(juce::translate(group), juce::dontSendNotification);
    classification.setJustificationType(juce::Justification::centred);
    classification.setColour(juce::Label::textColourId, juce::Colour(0xffe7b46c));
    classification.setFont(juce::FontOptions(11.0f));
    status.setJustificationType(juce::Justification::centred);
    volume.getProperties().set("consoleHardware", true);
    pan.getProperties().set("consoleHardware", true);
    volume.setName(title + " volume");
    volume.setSliderStyle(juce::Slider::LinearVertical);
    volume.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 18);
    volume.setRange(-60.0, 6.0, 0.1);
    volume.setValue(0.0, juce::dontSendNotification);
    volume.setDoubleClickReturnValue(true, 0.0);
    volume.setTextValueSuffix(" dB");
    pluginViewport.setViewedComponent(&pluginRack, false);
    pluginViewport.setScrollBarsShown(true, false);
    pluginViewport.setScrollBarThickness(6);
    addAndMakeVisible(pluginViewport);
    addAndMakeVisible(name);
    addAndMakeVisible(classification);
    addAndMakeVisible(volume);
    addAndMakeVisible(meter);
    addAndMakeVisible(status);
    mute.setClickingTogglesState(true);
    solo.setClickingTogglesState(true);
    cue.setClickingTogglesState(true);
    mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffce42));
    mute.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff211f15));
    solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff277bdf));
    mute.setTitle(title + " mute");
    if (group == "MONITOR") {
        listenOnMain = std::make_unique<juce::TextButton>(TRANS("Listen on 1-2"));
        listenOnMain->setTitle(TRANS("Listen to Cue on outputs 1-2"));
        listenOnMain->setTooltip(TRANS("Switch outputs 1-2 between Master and Cue."));
        listenOnMain->setClickingTogglesState(true);
        listenOnMain->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff216d68));
        addAndMakeVisible(*listenOnMain);
    } else addAndMakeVisible(mute);
    if (isTrack) {
        arm.setClickingTogglesState(false);
        arm.setTitle(title + " mixer record arm");
        arm.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb63e50));
        arm.setTooltip(TRANS("Arm this track for recording"));
        solo.setTitle(title + " solo");
        cue.setTitle(title + " pre-fader cue");
        cue.setTooltip("Send to the independent Cue output pair.");
        output.setTitle(title + " output");
        output.addItem(TRANS("Out: Master"), 1);
        output.setSelectedId(1, juce::dontSendNotification);
        sendViewport.setViewedComponent(&sendRack,false);
        sendViewport.setScrollBarsShown(true,false);
        sendViewport.setScrollBarThickness(7);
        addAndMakeVisible(sendViewport);
        pan.setName(title + " pan");
        pan.setRange(-100, 100, 1);
        pan.setDoubleClickReturnValue(true, 0.0);
        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 16);
        addAndMakeVisible(pan);
        addAndMakeVisible(solo);
        addAndMakeVisible(cue);
        addAndMakeVisible(output);
    }
}

void MixerPanel::Strip::resized() {
    auto area = getLocalBounds().reduced(5, 3);
    classification.setBounds(area.removeFromTop(15));
    name.setBounds(area.removeFromTop(18));
    status.setBounds(area.removeFromBottom(17));
    if (listenOnMain) listenOnMain->setBounds(area.removeFromBottom(21));
    if (!isTrack && mute.isVisible()) mute.setBounds(area.removeFromBottom(21).withSizeKeepingCentre(40, 21));
    diagnostics = isMaster ? juce::Rectangle<int>{} : area.removeFromTop(isTrack ? 71 : 63);
    auto pluginSpace = area.removeFromTop(22 * std::min<int>(2, static_cast<int>(entries.size())));
    pluginViewport.setBounds(pluginSpace);
    const int rowHeight = 22, rows = int(pluginRows.size());
    pluginRack.setSize(std::max(1,pluginSpace.getWidth() - (rows > 2 ? 6 : 0)), rows * rowHeight);
    for (int i = 0; i < rows; ++i)
        pluginRows[size_t(i)]->setBounds(0,i*rowHeight,pluginRack.getWidth(),rowHeight-1);
    if (isMaster) {
        analysisButton.setBounds(area.removeFromTop(23).reduced(1));
        diagnostics = area.removeFromTop(std::min(300, area.getHeight() * 3 / 4));
    }
    if (isTrack) {
        output.setBounds(area.removeFromTop(21).reduced(0, 1));
        const int count = int(sends.size());
        sendViewport.setBounds(area.removeFromTop(89 * std::min(2,count)));
        sendRack.setSize(std::max(1, sendViewport.getWidth() - (count > 2 ? 7 : 0)), 89 * count);
        for (int i=0;i<count;++i) sends[size_t(i)]->setBounds(0,89*i,sendRack.getWidth(),89);
        auto switches = area.removeFromBottom(21);
        mute.setBounds(switches.removeFromLeft(switches.getWidth() / (arm.isVisible() ? 4 : 3)));
        solo.setBounds(switches.removeFromLeft(switches.getWidth() / (arm.isVisible() ? 3 : 2)));
        if (arm.isVisible()) arm.setBounds(switches.removeFromLeft(switches.getWidth() / 2));
        cue.setBounds(switches);
        auto panArea = area.removeFromLeft(std::min(80,area.getWidth()/3));
        pan.setBounds(panArea.removeFromTop(96).withTrimmedTop(14));
    }
    if (isMaster) area = area.withSizeKeepingCentre(std::min(100, area.getWidth()), area.getHeight());
    else meter.setBounds(area.removeFromRight(36).withTrimmedBottom(20).reduced(0, 2));
    volume.setBounds(area);
}

void MixerPanel::updateMeters(const ostfstudio::MeterFrame& frame, double now) {
    for (std::size_t i = 0; i < std::min(strips_.size(), frame.tracks.size()); ++i) {
        auto& strip = *strips_[i];
        strip.meter.update(frame.tracks[i], now);
        if (strip.vu != frame.vu[i]) { strip.vu = frame.vu[i]; strip.repaint(strip.diagnostics); }
        for (size_t r=0;r<strip.sends.size();++r) {
            auto& row = *strip.sends[r];
            if (row.vu != frame.sendVu[i][r]) { row.vu = frame.sendVu[i][r]; row.repaint(row.dial); }
        }
    }
    auto output = [&](Strip& strip, const auto& vu, const auto& peak) {
        strip.meter.update(peak, now);
        if (strip.vu != vu) { strip.vu = vu; strip.repaint(strip.diagnostics); }
    };
    for (size_t i = 0; i < buses_.size(); ++i) {
        const auto slot = ostfstudio::busOutput(i);
        output(*buses_[i], frame.outputVu[slot], frame.outputs[slot]);
    }
    for (size_t i = 0; i < returns_.size(); ++i) {
        const auto slot = ostfstudio::returnOutput(i);
        output(*returns_[i], frame.outputVu[slot], frame.outputs[slot]);
    }
    if (master_) { master_->meter.update(frame.outputs[0], now); master_->analysis = frame.master; master_->repaint(master_->diagnostics); }
    if (cue_) output(*cue_,frame.cueVu,frame.cue);
}

bool MixerPanel::showingAnalysis() const {
    return isShowing() && master_ && master_->analysisButton.getToggleState();
}
void MixerPanel::setSpectrum(const std::array<float,31>& bands) {
    if (master_) { master_->bands = bands; master_->repaint(master_->diagnostics); }
}

void MixerPanel::setStripPlugins(bool output, size_t index, int channels, std::vector<PluginEntry> entries) {
    Strip* strip = nullptr;
    if (!output && index < strips_.size()) strip = strips_[index].get();
    if (output && routing_.hasOutput(index)) {
        if (index == 0) strip = master_.get();
        else if (ostfstudio::isReturnOutput(index)) strip = returns_[ostfstudio::outputReturnIndex(index)].get();
        else strip = buses_[ostfstudio::outputBusIndex(index)].get();
    }
    if (!strip) return;
    if (strip->meterChannels != channels) {
        strip->meterChannels = channels; strip->repaint(strip->diagnostics);
    }
    if (strip->entries == entries) return;
    strip->entries = std::move(entries);
    strip->pluginRows.clear();
    for (const auto& entry : strip->entries) {
        auto row = std::make_unique<juce::TextButton>(entry.name);
        row->setTitle(entry.name);
        row->setTooltip(entry.name);
        row->setEnabled(entry.available);
        row->setColour(juce::TextButton::buttonColourId,juce::Colour(entry.slot < 0 ? 0xff23484b : 0xff29343b));
        row->onClick = [this,output,index,slot=entry.slot] { if (openPlugin) openPlugin(output,index,slot); };
        strip->pluginRack.addAndMakeVisible(*row);
        strip->pluginRows.push_back(std::move(row));
    }
    strip->resized();
}

void MixerPanel::setRecordState(std::size_t track, bool armed, bool enabled) {
    if (track >= strips_.size()) return;
    auto& button = strips_[track]->arm;
    button.setToggleState(armed, juce::dontSendNotification);
    button.setEnabled(enabled);
    button.setTooltip(armed ? TRANS("Record armed. Press Record to start.") : TRANS("Arm this track for recording"));
}

void MixerPanel::clearMeters() {
    auto clear = [](Strip& strip) {
        strip.meter.clear(); strip.vu = {}; strip.repaint();
        for (auto& send : strip.sends) { send->vu = {}; send->repaint(); }
    };
    for (auto& strip : strips_) clear(*strip);
    for (auto& strip : buses_) clear(*strip);
    for (auto& strip : returns_) clear(*strip);
    if (master_) { master_->analysis = {}; master_->bands = {}; clear(*master_); }
    if (cue_) clear(*cue_);
}

void MixerPanel::Strip::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const auto face = getLookAndFeel().isColourSpecified(ostfstudio::console::LookAndFeel::panelColourId)
        ? findColour(ostfstudio::console::LookAndFeel::panelColourId) : juce::Colour(0xff364448);
    ostfstudio::console::LookAndFeel::verticalGradient(g, bounds, face.brighter(.08f).getARGB(), face.darker(.48f).getARGB());
    g.fillRoundedRectangle(bounds, 4.f);
    g.setColour(face.brighter(.30f));
    g.drawRoundedRectangle(bounds.reduced(.5f), 4.f, 1.f);
    if (!diagnostics.isEmpty()) {
        auto box = diagnostics.reduced(1, 2).toFloat();
        if (isMaster && analysisButton.getToggleState()) {
            ostfstudio::console::drawMasterDetails(g, box, analysis, bands);
        } else if (isMaster) {
            auto pair = box.removeFromTop(std::min(box.getWidth() * .255f, box.getHeight() * .44f));
            auto left = pair.removeFromLeft(pair.getWidth() * .5f);
            ostfstudio::console::drawVu(g, left.reduced(1, 0), analysis.vu[0], "L   VU");
            ostfstudio::console::drawVu(g, pair.reduced(1, 0), analysis.vu[1], "R   VU");
            ostfstudio::console::drawPhaseScope(g, box.withTrimmedTop(4), analysis);
        } else {
            ostfstudio::console::drawChannelVu(g, box, vu, meterChannels, "VU");
        }
    }
    if (isTrack) {
        g.setColour(juce::Colour(0xffe1e4de));
        g.setFont(juce::FontOptions(11.f));
        g.drawText(TRANS("Pan"),pan.getX(),pan.getY()-14,pan.getWidth(),14,juce::Justification::centred);
    }
}

MixerPanel::MixerPanel() {
    addAndMakeVisible(caption_);
    addAndMakeVisible(addBusButton_);
    addAndMakeVisible(addReturnButton_);
    addAndMakeVisible(masterViewport_);
    masterViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    addAndMakeVisible(outputViewport_);
    viewport_.setViewedComponent(&rack_, false);
    viewport_.setScrollBarsShown(true, true);
    outputViewport_.setViewedComponent(&outputRack_, false);
    outputViewport_.setScrollBarsShown(true, true);
    addBusButton_.setTitle(TRANS("Add bus track"));
    addReturnButton_.setTitle(TRANS("Add return track"));
    addBusButton_.onClick = [this] { if (requestAddOutput) requestAddOutput(false); };
    addReturnButton_.onClick = [this] { if (requestAddOutput) requestAddOutput(true); };
}

void MixerPanel::setAddOutputAvailability(bool canAddBus, bool canAddReturn) {
    addBusButton_.setEnabled(canAddBus);
    addReturnButton_.setEnabled(canAddReturn);
}

void MixerPanel::renameTrack(std::size_t index, const juce::String& name) {
    if (index < strips_.size()) {
        strips_[index]->name.setText(name, juce::dontSendNotification);
        strips_[index]->name.setTooltip(name);
    }
}

void MixerPanel::setTracks(const ostfstudio::OstfpPreview& project) {
    strips_.clear();
    buses_.clear();
    returns_.clear();
    routing_ = {};
    outputNames_ = {};
    routing_.returnCount = routing_.busCount = 0;
    const auto& tracks = project.trackLanes;
    channels_.assign(tracks.size(), {});
    trackOrder_.resize(tracks.size());
    std::iota(trackOrder_.begin(), trackOrder_.end(), std::size_t{0});
    std::stable_sort(trackOrder_.begin(), trackOrder_.end(), [&](auto a, auto b) { return project.mixerGroups[a] < project.mixerGroups[b]; });
    caption_.setText(project.chordVoicings > 0
        ? TRANS("Global Timeline: ") + juce::String(project.chordVoicings) + TRANS(" chord voicings")
        : TRANS("Global Timeline tracks"), juce::dontSendNotification);

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto strip = std::make_unique<Strip>(juce::String::fromUTF8(tracks[i].label), project.trackKinds[i] == ostfstudio::TrackKind::audio ? "Audio" : groupName(project.mixerGroups[i]), true);
        strip->contextMenu = [this, i] { if (rowMenu) rowMenu(false, i); };
        using namespace ostfstudio;
        strip->name.setTooltip(strip->name.getText());
        strip->volume.setTooltip(TRANS("Volume"));
        strip->pan.setTooltip(TRANS("Pan"));
        strip->mute.setTooltip(TRANS("Mute"));
        strip->solo.setTooltip(TRANS("Solo"));
        strip->cue.setTooltip(TRANS("Send this track to Cue before its fader."));
        auto* controls = strip.get();
        strip->volume.onValueChange = [this, i, controls] { setVolumeDb(i, controls->volume.getValue()); };
        strip->pan.onValueChange = [this, i, controls] { setPan(i, static_cast<float>(controls->pan.getValue() / 100.0)); };
        strip->mute.onClick = [this, i, controls] { setMuted(i, controls->mute.getToggleState()); };
        strip->solo.onClick = [this, i, controls] { setSoloed(i, controls->solo.getToggleState()); };
        if (i < project.sourceTrackCount()) {
            strip->addAndMakeVisible(strip->arm);
            strip->arm.onClick = [this, i, controls] {
                if (recordArmChanged) recordArmChanged(i, !controls->arm.getToggleState());
            };
        }
        strip->cue.onClick = [this, i, controls] {
            setCue(i, controls->cue.getToggleState());
        };
        strip->output.onChange = [this, i, controls] {
            setOutputBus(i, controls->output.getSelectedId() - 2);
        };
        rack_.addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }

    addBus();
    addBus();
    master_ = std::make_unique<Strip>(TRANS("Master"), "OUTPUT", false);
    master_->isMaster = true;
    master_->addAndMakeVisible(master_->analysisButton);
    master_->analysisButton.setClickingTogglesState(true);
    master_->analysisButton.onClick = [this] { master_->repaint(master_->diagnostics); };
    cue_ = std::make_unique<Strip>("Cue", "MONITOR", false);
    cue_->status.setTitle(TRANS("Cue output"));
    master_->contextMenu = [this] { if (rowMenu) rowMenu(true, 0); };
    master_->mute.onClick = [this] { setOutputMuted(0, master_->mute.getToggleState()); };
    master_->status.setText(TRANS("Out 1-2"), juce::dontSendNotification);
    cue_->listenOnMain->setToggleState(listeningOnMain_, juce::dontSendNotification);
    cue_->listenOnMain->onClick = [this] {
        listeningOnMain_ = cue_->listenOnMain->getToggleState();
        updateCueStatus();
        if (cueMonitorChanged) cueMonitorChanged(listeningOnMain_);
    };
    updateCueStatus();
    master_->volume.onValueChange = [this] {
        routing_.masterDb = master_->volume.getValue();
        if (routingChanged) routingChanged();
    };
    cue_->volume.onValueChange = [this] {
        routing_.cueDb = cue_->volume.getValue();
        if (routingChanged) routingChanged();
    };
    master_->meter.setVisible(false);
    master_->setComponentID("master-strip");
    masterViewport_.setViewedComponent(master_.get(), false);
    outputRack_.addAndMakeVisible(*cue_);
    addReturn();
    refreshAudibility();
    viewport_.setViewPosition(0, 0);
    outputViewport_.setViewPosition(0, 0);
}

juce::String MixerPanel::busName(std::size_t index) {
    return TRANS("Bus ") + juce::String::charToString(static_cast<juce::juce_wchar>('A' + index));
}

juce::String MixerPanel::outputName(std::size_t slot) const {
    if (outputNames_[slot].isNotEmpty()) return outputNames_[slot];
    if (slot == 0) return TRANS("Master");
    if (ostfstudio::isReturnOutput(slot)) return TRANS("Return ") + juce::String(static_cast<int>(ostfstudio::outputReturnIndex(slot) + 1));
    return busName(ostfstudio::outputBusIndex(slot));
}

void MixerPanel::renameOutput(std::size_t slot, const juce::String& name) {
    if (!routing_.hasOutput(slot)) return;
    outputNames_[slot] = name;
    const auto label = outputName(slot);
    const bool send = ostfstudio::isReturnOutput(slot);
    auto* strip = slot == 0 ? master_.get() : send ? returns_[ostfstudio::outputReturnIndex(slot)].get()
                                                  : buses_[ostfstudio::outputBusIndex(slot)].get();
    strip->name.setText(label, juce::dontSendNotification);
    if (send) { updateReturnOptions(); return; }
    for (auto& track : strips_) {
        auto& selector = track->output;
        const auto id = slot == 0 ? 1 : static_cast<int>(ostfstudio::outputBusIndex(slot) + 2);
        const bool selected = selector.getSelectedId() == id;
        selector.changeItemText(id, TRANS("Out: ") + label);
        if (selected) selector.setSelectedId(id, juce::dontSendNotification);
    }
}

void MixerPanel::addBus() {
    if (routing_.busCount >= ostfstudio::maxMixerBuses) return;
    const auto index = routing_.busCount++;
    auto strip = std::make_unique<Strip>(busName(index), "BUS", false);
    strip->contextMenu = [this, index] { if (rowMenu) rowMenu(true, ostfstudio::busOutput(index)); };
    strip->mute.onClick = [this, index, button = &strip->mute] {
        setOutputMuted(ostfstudio::busOutput(index), button->getToggleState());
    };
    strip->status.setText(TRANS("To Master"), juce::dontSendNotification);
    strip->volume.onValueChange = [this, index, control = &strip->volume] {
        routing_.busDb[index] = control->getValue();
        if (routingChanged) routingChanged();
    };
    outputRack_.addAndMakeVisible(*strip);
    buses_.push_back(std::move(strip));
    for (auto& track : strips_)
        track->output.addItem(TRANS("Out: ") + busName(index), static_cast<int>(index + 2));
    resized();
}

void MixerPanel::addReturn() {
    if (routing_.returnCount >= ostfstudio::maxMixerReturns) return;
    const auto index = routing_.returnCount++;
    auto strip = std::make_unique<Strip>(TRANS("Return ") + juce::String(static_cast<int>(index + 1)), "RETURN", false);
    strip->contextMenu = [this, index] { if (rowMenu) rowMenu(true, ostfstudio::returnOutput(index)); };
    strip->mute.onClick = [this, index, button = &strip->mute] { setOutputMuted(index + 3, button->getToggleState()); };
    strip->status.setText(TRANS("To Master"), juce::dontSendNotification);
    strip->volume.onValueChange = [this, index, control = &strip->volume] {
        routing_.returnDb[index] = control->getValue();
        if (routingChanged) routingChanged();
    };
    outputRack_.addAndMakeVisible(*strip);
    returns_.push_back(std::move(strip));
    updateReturnOptions();
    resized();
}

void MixerPanel::updateReturnOptions() {
    for (size_t t=0;t<strips_.size();++t) {
        auto& strip = *strips_[t];
        strip.sends.resize(routing_.returnCount);
        for (size_t r=0;r<strip.sends.size();++r) {
            auto& send = strip.sends[r];
            if (!send) {
                send = std::make_unique<Send>();
                strip.sendRack.addAndMakeVisible(*send);
                send->knob.onValueChange = [this,t,r,knob=&send->knob] {
                    setSendLevel(t,r,float(knob->getValue()/100.));
                };
            }
            const auto label = TRANS("Send: ") + outputName(ostfstudio::returnOutput(r));
            send->name.setText(label,juce::dontSendNotification);
            send->name.setTooltip(label);
            send->knob.setName(strip.name.getText() + " / " + label);
            send->knob.setTitle(send->knob.getName());
            send->knob.setTooltip(label);
            send->knob.setValue(channels_[t].returnSends[r]*100.,juce::dontSendNotification);
        }
        strip.resized();
    }
}

void MixerPanel::setVolumeDb(std::size_t index, double db) {
    channels_[index].volumeDb = db;
    strips_[index]->volume.setValue(db, juce::dontSendNotification);
    if (channelChanged) channelChanged(index);
}

void MixerPanel::displayPan(std::size_t index, float pan) {
    if (index < strips_.size() && !strips_[index]->pan.isMouseButtonDown())
        strips_[index]->pan.setValue(pan * 100.0, juce::dontSendNotification);
}

void MixerPanel::setPan(std::size_t index, float pan) {
    channels_[index].pan = std::clamp(pan, -1.0f, 1.0f);
    strips_[index]->pan.setValue(channels_[index].pan * 100.0, juce::dontSendNotification);
    if (channelChanged) channelChanged(index);
}

void MixerPanel::setOutputBus(std::size_t index, int bus) {
    channels_[index].outputBus = std::clamp(bus, -1, static_cast<int>(routing_.busCount) - 1);
    strips_[index]->output.setSelectedId(channels_[index].outputBus + 2, juce::dontSendNotification);
    if (routingChanged) routingChanged();
}

void MixerPanel::setSendLevel(std::size_t index, std::size_t target, float level) {
    if (target >= routing_.returnCount) return;
    channels_[index].returnSends[target] = std::clamp(level, 0.0f, 1.0f);
    strips_[index]->sends[target]->knob.setValue(channels_[index].returnSends[target] * 100.0, juce::dontSendNotification);
    if (routingChanged) routingChanged();
}

void MixerPanel::setCue(std::size_t index, bool enabled) {
    channels_.at(index).cue = enabled;
    strips_.at(index)->cue.setToggleState(enabled, juce::dontSendNotification);
    if (channelChanged) channelChanged(index);
}

void MixerPanel::setActiveOutputCount(int count) {
    activeOutputCount_ = count;
    updateCueStatus();
}

void MixerPanel::updateCueStatus() {
    if (!cue_) return;
    juce::String status;
    if (activeOutputCount_ < 0) status = TRANS("Cue bus");
    else if (activeOutputCount_ == 0) status = TRANS("No audio output");
    else if (listeningOnMain_) status = activeOutputCount_ >= 4 ? TRANS("Out 1-2 + 3-4") : TRANS("Out 1-2");
    else status = activeOutputCount_ >= 4 ? TRANS("Out 3-4") : TRANS("Out 3-4 inactive");
    cue_->status.setText(status, juce::dontSendNotification);
}

void MixerPanel::setMuted(std::size_t index, bool muted) {
    channels_[index].muted = muted;
    strips_[index]->mute.setToggleState(muted, juce::dontSendNotification);
    refreshAudibility();
    if (channelChanged) channelChanged(index);
}

void MixerPanel::setSoloed(std::size_t index, bool soloed) {
    channels_[index].soloed = soloed;
    strips_[index]->solo.setToggleState(soloed, juce::dontSendNotification);
    refreshAudibility();
    if (channelChanged) channelChanged(index);
}

ostfstudio::SessionMix MixerPanel::snapshot() const {
    ostfstudio::SessionMix mix;
    mix.tracks = channels_;
    mix.routing = routing_;
    mix.outputNames = outputNames_;
    return mix;
}

void MixerPanel::restore(const ostfstudio::SessionMix& mix) {
    if (mix.tracks.size() != channels_.size() && mix.tracks.size() + 1 != channels_.size()) return;
    const auto buses = std::clamp(mix.routing.busCount, ostfstudio::minMixerOutputsPerKind, ostfstudio::maxMixerBuses);
    const auto returns = std::clamp(mix.routing.returnCount, ostfstudio::minMixerOutputsPerKind, ostfstudio::maxMixerReturns);
    if (routing_.busCount > buses) {
        for (auto& strip : strips_) {
            strip->output.clear(juce::dontSendNotification);
            strip->output.addItem(TRANS("Out: Master"), 1);
            for (std::size_t i = 0; i < buses; ++i)
                strip->output.addItem(TRANS("Out: ") + busName(i), static_cast<int>(i + 2));
        }
        buses_.resize(buses);
        routing_.busCount = buses;
    }
    while (routing_.busCount < buses) addBus();
    if (routing_.returnCount > returns) {
        returns_.resize(returns);
        routing_.returnCount = returns;
        updateReturnOptions();
    }
    while (routing_.returnCount < returns) addReturn();
    outputNames_ = mix.outputNames;
    for (std::size_t slot = 0; slot < outputNames_.size(); ++slot)
        if (routing_.hasOutput(slot)) renameOutput(slot, mix.outputNames[slot]);
    routing_.masterDb = mix.routing.masterDb;
    routing_.cueDb = mix.routing.cueDb;
    routing_.busDb = mix.routing.busDb;
    routing_.returnDb = mix.routing.returnDb;
    master_->volume.setValue(routing_.masterDb, juce::dontSendNotification);
    cue_->volume.setValue(routing_.cueDb, juce::dontSendNotification);
    for (std::size_t i = 0; i < buses_.size(); ++i) buses_[i]->volume.setValue(routing_.busDb[i], juce::dontSendNotification);
    for (std::size_t i = 0; i < returns_.size(); ++i) returns_[i]->volume.setValue(routing_.returnDb[i], juce::dontSendNotification);
    std::copy(mix.tracks.begin(), mix.tracks.end(), channels_.begin());
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        const auto& track = channels_[i];
        auto& strip = *strips_[i];
        strip.volume.setValue(track.volumeDb, juce::dontSendNotification);
        strip.pan.setValue(track.pan * 100.0, juce::dontSendNotification);
        strip.mute.setToggleState(track.muted, juce::dontSendNotification);
        strip.solo.setToggleState(track.soloed, juce::dontSendNotification);
        strip.cue.setToggleState(track.cue, juce::dontSendNotification);
        strip.output.setSelectedId(track.outputBus + 2, juce::dontSendNotification);
        for (size_t r=0;r<strip.sends.size();++r)
            strip.sends[r]->knob.setValue(track.returnSends[r]*100.,juce::dontSendNotification);
        if (channelChanged) channelChanged(i);
    }
    refreshAudibility();
    routing_.outputMuted = mix.routing.outputMuted;
    master_->mute.setToggleState(routing_.outputMuted[0], juce::dontSendNotification);
    for (std::size_t i = 0; i < buses_.size(); ++i)
        buses_[i]->mute.setToggleState(routing_.outputMuted[ostfstudio::busOutput(i)], juce::dontSendNotification);
    for (std::size_t i = 0; i < returns_.size(); ++i)
        returns_[i]->mute.setToggleState(routing_.outputMuted[3 + i], juce::dontSendNotification);
    resized();
    if (routingChanged) routingChanged();
}

void MixerPanel::resized() {
    auto area = getLocalBounds().reduced(6, 0);
    auto toolbar = area.removeFromTop(24);
    addReturnButton_.setBounds(toolbar.removeFromRight(94).reduced(1));
    addBusButton_.setBounds(toolbar.removeFromRight(94).reduced(1));
    caption_.setBounds(toolbar);
    const int height = std::max(396 + 89 * std::min<int>(2,routing_.returnCount), area.getHeight() - viewport_.getScrollBarThickness());
    masterViewport_.setBounds(area.removeFromRight(std::min(masterWidth, area.getWidth() / 3)));
    if (master_) {
        const int scroll = height > masterViewport_.getHeight() ? masterViewport_.getScrollBarThickness() : 0;
        master_->setSize(std::max(1, masterViewport_.getWidth() - scroll), height);
    }
    area.removeFromRight(6);
    const auto outputCount = static_cast<int>(buses_.size() + returns_.size()) + 1;
    const auto groupWidth = std::min(outputWidth * outputCount, area.getWidth() * 2 / 5);
    outputViewport_.setBounds(area.removeFromRight(groupWidth));
    area.removeFromRight(6);
    viewport_.setBounds(area);
    rack_.setSize(stripWidth * static_cast<int>(strips_.size()), height);
    outputRack_.setSize(outputWidth * outputCount, height);
    auto tracks = rack_.getLocalBounds();
    for (const auto index : trackOrder_)
        strips_[index]->setBounds(tracks.removeFromLeft(stripWidth).reduced(2, 1));
    auto outputs = outputRack_.getLocalBounds();
    const auto place = [&](Strip& strip) { strip.setBounds(outputs.removeFromLeft(outputWidth).reduced(2, 1)); };
    for (auto& bus : buses_) place(*bus);
    for (auto& strip : returns_) place(*strip);
    if (cue_) place(*cue_);
}

void MixerPanel::refreshAudibility() {
    for (std::size_t i = 0; i < strips_.size(); ++i) {
        const auto label = channels_[i].muted ? TRANS("Muted") : isAudible(i) ? TRANS("Active") : TRANS("Excluded");
        strips_[i]->status.setText(label, juce::dontSendNotification);
    }
}

double MixerPanel::outputVolume(std::size_t index) const {
    if (index == 0) return routing_.masterDb;
    if (!ostfstudio::isReturnOutput(index)) return routing_.busDb[ostfstudio::outputBusIndex(index)];
    return routing_.returnDb[index - 3];
}

void MixerPanel::setOutputVolume(std::size_t index, double db) {
    if (index == 0) master_->volume.setValue(db, juce::sendNotificationSync);
    else if (!ostfstudio::isReturnOutput(index)) buses_[ostfstudio::outputBusIndex(index)]->volume.setValue(db, juce::sendNotificationSync);
    else if (index - 3 < returns_.size()) returns_[index - 3]->volume.setValue(db, juce::sendNotificationSync);
}

void MixerPanel::setOutputMuted(std::size_t index, bool muted) {
    routing_.outputMuted[index] = muted;
    if (index == 0) master_->mute.setToggleState(muted, juce::dontSendNotification);
    else if (!ostfstudio::isReturnOutput(index)) buses_[ostfstudio::outputBusIndex(index)]->mute.setToggleState(muted, juce::dontSendNotification);
    else if (index - 3 < returns_.size()) returns_[index - 3]->mute.setToggleState(muted, juce::dontSendNotification);
    if (routingChanged) routingChanged();
}
