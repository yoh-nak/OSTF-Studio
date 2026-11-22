#include "ui/MixerPanel.h"
#include <algorithm>
#include <numeric>

namespace {
constexpr int stripWidth = 116;

const char* groupName(ostfstudio::MixerTrackGroup group) {
    using enum ostfstudio::MixerTrackGroup;
    switch (group) {
        case voicing: return "Voicing";
        case vocal: return "Vocal";
        case pitched: return "Pitched";
        case other: return "Arrangement";
    }
    return "";
}
}

MixerPanel::Strip::Strip(const juce::String& title, const juce::String& group, bool track) : isTrack(track) {
    name.setText(title, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    classification.setText(group, juce::dontSendNotification);
    classification.setJustificationType(juce::Justification::centred);
    classification.setColour(juce::Label::textColourId, juce::Colour(0xffe7b46c));
    classification.setFont(juce::FontOptions(11.0f));
    status.setJustificationType(juce::Justification::centred);
    volume.setName(title + " volume");
    volume.setSliderStyle(juce::Slider::LinearVertical);
    volume.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 18);
    volume.setRange(-60.0, 6.0, 0.1);
    volume.setValue(0.0, juce::dontSendNotification);
    volume.setDoubleClickReturnValue(true, 0.0);
    volume.setTextValueSuffix(" dB");
    volume.setColour(juce::Slider::thumbColourId, juce::Colour(0xffd9d4ca));
    volume.setColour(juce::Slider::trackColourId, juce::Colour(0xffdba963));
    addAndMakeVisible(name);
    addAndMakeVisible(classification);
    addAndMakeVisible(volume);
    addAndMakeVisible(status);
    mute.setTitle(title + " mute");
    if (group != "MONITOR") addAndMakeVisible(mute);
    if (isTrack) {
        solo.setTitle(title + " solo");
        cue.setTitle(title + " pre-fader cue");
        cue.setTooltip("Send to the independent Cue output pair.");
        output.setTitle(title + " output");
        output.addItem("Out: Master", 1);
        output.addItem("Out: Bus A", 2);
        output.addItem("Out: Bus B", 3);
        output.setSelectedId(1, juce::dontSendNotification);
        returnTarget.setTitle(title + " return send");
        sendLevel.setName(title + " send level");
        sendLevel.setSliderStyle(juce::Slider::LinearHorizontal);
        sendLevel.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 16);
        sendLevel.setTextValueSuffix("%");
        sendLevel.setRange(0, 100, 1);
        pan.setName(title + " pan");
        pan.setRange(-100, 100, 1);
        pan.setDoubleClickReturnValue(true, 0.0);
        pan.setSliderStyle(juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle(juce::Slider::TextBoxRight, false, 37, 15);
        addAndMakeVisible(pan);
        addAndMakeVisible(solo);
        addAndMakeVisible(cue);
        addAndMakeVisible(output);
        addAndMakeVisible(returnTarget);
        addAndMakeVisible(sendLevel);
    }
}

void MixerPanel::Strip::resized() {
    auto area = getLocalBounds().reduced(5, 3);
    classification.setBounds(area.removeFromTop(15));
    name.setBounds(area.removeFromTop(18));
    status.setBounds(area.removeFromBottom(17));
    if (!isTrack && mute.isVisible()) mute.setBounds(area.removeFromBottom(21).withSizeKeepingCentre(40, 21));
    if (isTrack) {
        output.setBounds(area.removeFromTop(21).reduced(0, 1));
        returnTarget.setBounds(area.removeFromTop(20).reduced(0, 1));
        sendLevel.setBounds(area.removeFromTop(18));
        pan.setBounds(area.removeFromTop(18));
        auto switches = area.removeFromBottom(21);
        mute.setBounds(switches.removeFromLeft(switches.getWidth() / 3));
        solo.setBounds(switches.removeFromLeft(switches.getWidth() / 2));
        cue.setBounds(switches);
    }
    volume.setBounds(area);
}

void MixerPanel::Strip::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff444b54), bounds.getX(), bounds.getY(),
                                          juce::Colour(0xff24292f), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colour(0xff59616a));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
}

MixerPanel::MixerPanel() {
    addAndMakeVisible(caption_);
    addAndMakeVisible(addReturnButton_);
    addAndMakeVisible(viewport_);
    addAndMakeVisible(outputViewport_);
    viewport_.setViewedComponent(&rack_, false);
    viewport_.setScrollBarsShown(true, true);
    outputViewport_.setViewedComponent(&outputRack_, false);
    outputViewport_.setScrollBarsShown(true, true);
    addReturnButton_.onClick = [this] { addReturn(); if (returnsChanged) returnsChanged(); };
}

void MixerPanel::setTracks(const ostfstudio::OstfpPreview& project) {
    strips_.clear();
    buses_.clear();
    returns_.clear();
    routing_ = {};
    routing_.returnCount = 0;
    const auto& tracks = project.trackLanes;
    channels_.assign(tracks.size(), {});
    trackOrder_.resize(tracks.size());
    std::iota(trackOrder_.begin(), trackOrder_.end(), std::size_t{0});
    std::stable_sort(trackOrder_.begin(), trackOrder_.end(), [&](auto a, auto b) { return project.mixerGroups[a] < project.mixerGroups[b]; });
    caption_.setText(project.chordVoicings > 0
        ? "Global Timeline: " + juce::String(project.chordVoicings) + " chord voicings / Cue outputs 3-4"
        : juce::String("Global Timeline tracks left / Cue outputs 3-4"), juce::dontSendNotification);

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto strip = std::make_unique<Strip>(juce::String::fromUTF8(tracks[i].label), groupName(project.mixerGroups[i]), true);
        auto* controls = strip.get();
        strip->volume.onValueChange = [this, i, controls] { setVolumeDb(i, controls->volume.getValue()); };
        strip->pan.onValueChange = [this, i, controls] { setPan(i, static_cast<float>(controls->pan.getValue() / 100.0)); };
        strip->mute.onClick = [this, i, controls] { setMuted(i, controls->mute.getToggleState()); };
        strip->solo.onClick = [this, i, controls] { setSoloed(i, controls->solo.getToggleState()); };
        strip->cue.onClick = [this, i, controls] {
            channels_[i].cue = controls->cue.getToggleState();
            if (routingChanged) routingChanged();
        };
        strip->output.onChange = [this, i, controls] {
            setOutputBus(i, controls->output.getSelectedId() - 2);
        };
        strip->returnTarget.onChange = [this, i, controls] {
            const auto id = controls->returnTarget.getSelectedId();
            if (id > 0) controls->sendLevel.setValue(channels_[i].returnSends[static_cast<std::size_t>(id - 1)] * 100.0,
                                                     juce::dontSendNotification);
        };
        strip->sendLevel.onValueChange = [this, i, controls] {
            const auto id = controls->returnTarget.getSelectedId();
            if (id <= 0) return;
            setSendLevel(i, static_cast<std::size_t>(id - 1), static_cast<float>(controls->sendLevel.getValue() / 100.0));
        };
        rack_.addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }

    for (std::size_t i = 0; i < routing_.busDb.size(); ++i) {
        auto bus = std::make_unique<Strip>(i == 0 ? "Bus A" : "Bus B", "BUS", false);
        bus->mute.onClick = [this, i, button = &bus->mute] { setOutputMuted(i + 1, button->getToggleState()); };
        bus->status.setText("To Master", juce::dontSendNotification);
        bus->volume.onValueChange = [this, i, control = &bus->volume] {
            routing_.busDb[i] = control->getValue();
            if (routingChanged) routingChanged();
        };
        outputRack_.addAndMakeVisible(*bus);
        buses_.push_back(std::move(bus));
    }
    master_ = std::make_unique<Strip>("Master", "OUTPUT", false);
    cue_ = std::make_unique<Strip>("Cue", "MONITOR", false);
    master_->mute.onClick = [this] { setOutputMuted(0, master_->mute.getToggleState()); };
    master_->status.setText("Out 1-2", juce::dontSendNotification);
    cue_->status.setText("Out 3-4", juce::dontSendNotification);
    master_->volume.onValueChange = [this] {
        routing_.masterDb = master_->volume.getValue();
        if (routingChanged) routingChanged();
    };
    cue_->volume.onValueChange = [this] {
        routing_.cueDb = cue_->volume.getValue();
        if (routingChanged) routingChanged();
    };
    outputRack_.addAndMakeVisible(*master_);
    outputRack_.addAndMakeVisible(*cue_);
    addReturn();
    refreshAudibility();
    viewport_.setViewPosition(0, 0);
    outputViewport_.setViewPosition(0, 0);
}

void MixerPanel::addReturn() {
    if (routing_.returnCount >= ostfstudio::maxMixerReturns) return;
    const auto index = routing_.returnCount++;
    auto strip = std::make_unique<Strip>("Return " + juce::String(static_cast<int>(index + 1)), "RETURN", false);
    strip->mute.onClick = [this, index, button = &strip->mute] { setOutputMuted(index + 3, button->getToggleState()); };
    strip->status.setText("To Master", juce::dontSendNotification);
    strip->volume.onValueChange = [this, index, control = &strip->volume] {
        routing_.returnDb[index] = control->getValue();
        if (routingChanged) routingChanged();
    };
    outputRack_.addAndMakeVisible(*strip);
    returns_.push_back(std::move(strip));
    addReturnButton_.setEnabled(routing_.returnCount < ostfstudio::maxMixerReturns);
    updateReturnOptions();
    resized();
}

void MixerPanel::updateReturnOptions() {
    for (auto& strip : strips_) {
        const auto chosen = std::max(1, strip->returnTarget.getSelectedId());
        strip->returnTarget.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < routing_.returnCount; ++i)
            strip->returnTarget.addItem("Send: Return " + juce::String(static_cast<int>(i + 1)), static_cast<int>(i + 1));
        strip->returnTarget.setSelectedId(chosen, juce::dontSendNotification);
    }
}

void MixerPanel::setVolumeDb(std::size_t index, double db) {
    channels_[index].volumeDb = db;
    strips_[index]->volume.setValue(db, juce::dontSendNotification);
    if (channelChanged) channelChanged(index);
}

void MixerPanel::setPan(std::size_t index, float pan) {
    channels_[index].pan = std::clamp(pan, -1.0f, 1.0f);
    strips_[index]->pan.setValue(channels_[index].pan * 100.0, juce::dontSendNotification);
    if (channelChanged) channelChanged(index);
}

void MixerPanel::setOutputBus(std::size_t index, int bus) {
    channels_[index].outputBus = std::clamp(bus, -1, 1);
    strips_[index]->output.setSelectedId(channels_[index].outputBus + 2, juce::dontSendNotification);
    if (routingChanged) routingChanged();
}

void MixerPanel::setSendLevel(std::size_t index, std::size_t target, float level) {
    if (target >= routing_.returnCount) return;
    channels_[index].returnSends[target] = std::clamp(level, 0.0f, 1.0f);
    if (strips_[index]->returnTarget.getSelectedId() == static_cast<int>(target + 1))
        strips_[index]->sendLevel.setValue(channels_[index].returnSends[target] * 100.0, juce::dontSendNotification);
    if (routingChanged) routingChanged();
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
    return {channels_, {}, routing_};
}

void MixerPanel::restore(const ostfstudio::SessionMix& mix) {
    if (mix.tracks.size() != channels_.size()) return;
    while (routing_.returnCount < std::min(mix.routing.returnCount, ostfstudio::maxMixerReturns)) addReturn();
    routing_.masterDb = mix.routing.masterDb;
    routing_.cueDb = mix.routing.cueDb;
    routing_.busDb = mix.routing.busDb;
    routing_.returnDb = mix.routing.returnDb;
    master_->volume.setValue(routing_.masterDb, juce::dontSendNotification);
    cue_->volume.setValue(routing_.cueDb, juce::dontSendNotification);
    for (std::size_t i = 0; i < buses_.size(); ++i) buses_[i]->volume.setValue(routing_.busDb[i], juce::dontSendNotification);
    for (std::size_t i = 0; i < returns_.size(); ++i) returns_[i]->volume.setValue(routing_.returnDb[i], juce::dontSendNotification);
    channels_ = mix.tracks;
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        const auto& track = channels_[i];
        auto& strip = *strips_[i];
        strip.volume.setValue(track.volumeDb, juce::dontSendNotification);
        strip.pan.setValue(track.pan * 100.0, juce::dontSendNotification);
        strip.mute.setToggleState(track.muted, juce::dontSendNotification);
        strip.solo.setToggleState(track.soloed, juce::dontSendNotification);
        strip.cue.setToggleState(track.cue, juce::dontSendNotification);
        strip.output.setSelectedId(track.outputBus + 2, juce::dontSendNotification);
        strip.sendLevel.setValue(track.returnSends[0] * 100.0, juce::dontSendNotification);
        if (channelChanged) channelChanged(i);
    }
    refreshAudibility();
    routing_.outputMuted = mix.routing.outputMuted;
    master_->mute.setToggleState(routing_.outputMuted[0], juce::dontSendNotification);
    for (std::size_t i = 0; i < buses_.size(); ++i)
        buses_[i]->mute.setToggleState(routing_.outputMuted[1 + i], juce::dontSendNotification);
    for (std::size_t i = 0; i < returns_.size(); ++i)
        returns_[i]->mute.setToggleState(routing_.outputMuted[3 + i], juce::dontSendNotification);
    if (routingChanged) routingChanged();
}

void MixerPanel::resized() {
    auto area = getLocalBounds().reduced(6, 0);
    auto toolbar = area.removeFromTop(24);
    addReturnButton_.setBounds(toolbar.removeFromRight(94).reduced(1));
    caption_.setBounds(toolbar);
    const auto outputCount = static_cast<int>(buses_.size() + returns_.size()) + 2;
    const auto groupWidth = juce::jmin(stripWidth * outputCount, area.getWidth() * 3 / 5);
    outputViewport_.setBounds(area.removeFromRight(groupWidth));
    area.removeFromRight(12);
    viewport_.setBounds(area);
    const auto height = std::max(160, area.getHeight() - viewport_.getScrollBarThickness());
    rack_.setSize(stripWidth * static_cast<int>(strips_.size()), height);
    outputRack_.setSize(stripWidth * outputCount, height);
    auto rackBounds = rack_.getLocalBounds();
    const auto place = [&](Strip& strip) { strip.setBounds(rackBounds.removeFromLeft(stripWidth).reduced(2, 1)); };
    for (const auto index : trackOrder_) place(*strips_[index]);
    rackBounds = outputRack_.getLocalBounds();
    for (auto& bus : buses_) place(*bus);
    for (auto& strip : returns_) place(*strip);
    if (master_) place(*master_);
    if (cue_) place(*cue_);
}

void MixerPanel::refreshAudibility() {
    for (std::size_t i = 0; i < strips_.size(); ++i) {
        const auto label = channels_[i].muted ? "Muted" : isAudible(i) ? "Active" : "Excluded";
        strips_[i]->status.setText(label, juce::dontSendNotification);
    }
}

double MixerPanel::outputVolume(std::size_t index) const {
    if (index == 0) return routing_.masterDb;
    if (index < 3) return routing_.busDb[index - 1];
    return routing_.returnDb[index - 3];
}

void MixerPanel::setOutputVolume(std::size_t index, double db) {
    if (index == 0) master_->volume.setValue(db, juce::sendNotificationSync);
    else if (index < 3) buses_[index - 1]->volume.setValue(db, juce::sendNotificationSync);
    else if (index - 3 < returns_.size()) returns_[index - 3]->volume.setValue(db, juce::sendNotificationSync);
}

void MixerPanel::setOutputMuted(std::size_t index, bool muted) {
    routing_.outputMuted[index] = muted;
    if (index == 0) master_->mute.setToggleState(muted, juce::dontSendNotification);
    else if (index < 3) buses_[index - 1]->mute.setToggleState(muted, juce::dontSendNotification);
    else if (index - 3 < returns_.size()) returns_[index - 3]->mute.setToggleState(muted, juce::dontSendNotification);
    if (routingChanged) routingChanged();
}
