#include "ui/MainComponent.h"
#include "ui/GlobalTimelineEdit.h"
#include "ui/PluginRoles.h"
#include "ui/MediaImport.h"
#include <BinaryData.h>
#include <iostream>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
template <typename T>
T* descendant(juce::Component& parent) {
    for (auto* child : parent.getChildren()) {
        if (auto* match = dynamic_cast<T*>(child)) return match;
        if (auto* match = descendant<T>(*child)) return match;
    }
    return nullptr;
}

juce::MouseEvent pointer(juce::Component& target, int y) {
    const auto time = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), {10.0f, static_cast<float>(y)},
            juce::ModifierKeys::leftButtonModifier, 1.0f, 0, 0, 0, 0, &target, &target, time, {10, 2}, time, 1, y != 2};
}

template <typename T>
T* findControl(juce::Component& root, const juce::String& title) {
    for (auto* child : root.getChildren()) {
        if (auto* control = dynamic_cast<T*>(child); control && control->getTitle() == title) return control;
        if (auto* found = findControl<T>(*child, title)) return found;
    }
    return nullptr;
}
}

int checkSharedPartEditing(std::unique_ptr<juce::AudioPluginInstance> instrument, std::unique_ptr<juce::AudioPluginInstance> second) {
    {
        juce::MemoryInputStream input(BinaryData::neonplatforms_ostfp, BinaryData::neonplatforms_ostfpSize, false);
        std::unique_ptr<ostfstudio::OstfpPreview> song;
        if (ostfstudio::readOstfpPreview(input, song).failed() || !song->savedMix) return 211;
        const auto& mix = *song->savedMix;
        std::vector<PreviewTransport::TrackSetup> routes(song->trackLanes.size());
        const int guitar = song->trackIds.indexOf("hook"), bass = song->trackIds.indexOf("bass");
        if (guitar < 0 || bass < 0) return 212;
        PreviewTransport playback;
        for (size_t i = 0; i < song->sourceTrackCount(); ++i) {
            const auto& r = mix.midiRoutes[i];
            auto* module = int(i) == guitar ? instrument.get() : int(i) == bass ? second.get() : nullptr;
            if (mix.instruments[i].isObject() != (module != nullptr)) return 213;
            routes[i] = {r.destination, module, r.channel};
            playback.setChannelMix(i, mix.tracks[i], true, 0);
        }
        playback.setOutputMix(mix.routing);
        if (playback.preparePlayback(*song, routes, {}, 44100, 512).failed()) return 214;
        playback.seekToTick(16LL * 4 * song->ppq);
        juce::AudioBuffer<float> block(2,512);
        for (int i = 0; i < 24; ++i) playback.renderBlock(block.getArrayOfWritePointers(), 2, 512);
        const auto meters = playback.takeMeters();
        if (meters.tracks[guitar][0] < .001f || meters.tracks[bass][0] < .001f) return 215;
        for (const auto owner : {guitar, bass}) {
            juce::MemoryBlock state, component;
            routes[owner].instrument->getStateInformation(state);
            auto wrapper = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
            const auto* child = wrapper ? wrapper->getChildByName("IComponent") : nullptr;
            if (!child || !component.fromBase64Encoding(child->getAllSubText())) return 216;
            auto xml = juce::AudioProcessor::getXmlFromBinary(component.getData(), int(component.getSize()));
            std::array<int,16> programs{};
            for (size_t i = 0; i < song->sourceTrackCount(); ++i) {
                if (routes[i].outputTrack.value_or(i) != size_t(owner)) continue;
                if (song->programChanges[i].empty()) return 217;
                programs[routes[i].midiChannel - 1] = song->programChanges[i].front().program;
            }
            for (int ch = 0; ch < 16; ++ch)
                if (!xml || xml->getIntAttribute("p" + juce::String(ch), -1) != programs[ch]) return 218;
        }
    }
    {
        juce::MemoryInputStream input(BinaryData::bluehourwaltzsharedgm_ostfp, BinaryData::bluehourwaltzsharedgm_ostfpSize, false);
        std::unique_ptr<ostfstudio::OstfpPreview> shared;
        if (ostfstudio::readOstfpPreview(input, shared).failed() || !shared->savedMix) return 60;
        const auto& mix = *shared->savedMix;
        const auto owner = shared->trackIds.indexOf("keys");
        if (owner < 0 || !matchesVst3Instrument(mix.instruments[owner], instrument->getPluginDescription())) return 61;
        std::vector<PreviewTransport::TrackSetup> routes(shared->trackLanes.size());
        for (size_t i = 0; i < shared->sourceTrackCount(); ++i) {
            const auto& route = mix.midiRoutes[i];
            if (i != size_t(owner) && (route.destination != size_t(owner) || mix.instruments[i].isObject())) return 62;
            routes[i] = {route.destination, i == size_t(owner) ? instrument.get() : nullptr, route.channel};
        }
        PreviewTransport playback;
        playback.setChannelMix(size_t(owner), {}, true, 0);
        playback.setOutputMix({});
        if (playback.preparePlayback(*shared, routes, {}, 44100, 512).failed()) return 63;
        playback.seekToTick(60 * 3 * shared->ppq);
        juce::AudioBuffer<float> block(2, 512);
        float peak = 0;
        for (int i = 0; i < 12; ++i) {
            playback.renderBlock(block.getArrayOfWritePointers(), 2, 512);
            peak = std::max(peak, block.getMagnitude(0, 512));
        }
        if (peak < .001f) return 64;
        juce::MemoryBlock state, component;
        instrument->getStateInformation(state);
        auto wrapper = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
        const auto* child = wrapper ? wrapper->getChildByName("IComponent") : nullptr;
        if (!child || !component.fromBase64Encoding(child->getAllSubText())) return 65;
        auto xml = juce::AudioProcessor::getXmlFromBinary(component.getData(), int(component.getSize()));
        for (size_t i = 0; i < shared->sourceTrackCount(); ++i) {
            const auto& events = shared->programChanges[i];
            if (events.empty() || !xml || xml->getIntAttribute("p" + juce::String(routes[i].midiChannel - 1), -1) != events.front().program)
                return 66;
        }
        playback.stop();
        const auto pitched = shared->pitchedNotes;
        const auto drums = shared->drumNotes;
        for (size_t part = 0; part < shared->sourceTrackCount(); ++part) {
            for (auto& notes : shared->pitchedNotes) notes.clear();
            for (auto& notes : shared->drumNotes) notes.clear();
            shared->pitchedNotes[part] = pitched[part];
            shared->drumNotes[part] = drums[part];
            const auto& notes = drums[part].empty() ? pitched[part] : drums[part];
            if (notes.empty() || playback.preparePlayback(*shared, routes, {}, 44100, 512).failed()) return 76;
            playback.seekToTick(notes.front().startTick);
            float partPeak = 0;
            for (int i = 0; i < 12; ++i) {
                playback.renderBlock(block.getArrayOfWritePointers(), 2, 512);
                partPeak = std::max(partPeak, block.getMagnitude(0, 512));
            }
            playback.stop();
            if (partPeak < .0001f) return 77;
        }
        // Render two notes starting on the same sample through the restored GM owner.
        // Each isolated waveform must be present in the simultaneous render.
        for (auto& notes : shared->pitchedNotes) notes.clear();
        for (auto& notes : shared->drumNotes) notes.clear();
        const int bass = shared->trackIds.indexOf("bass");
        if (bass < 0) return 207;
        auto renderPair = [&](bool keysOn, bool bassOn, juce::AudioBuffer<float>& samples) {
            shared->pitchedNotes[owner].clear();
            shared->pitchedNotes[bass].clear();
            if (keysOn) shared->pitchedNotes[owner].push_back({0, shared->ppq, 72, 90});
            if (bassOn) shared->pitchedNotes[bass].push_back({0, shared->ppq, 40, 90});
            if (playback.preparePlayback(*shared, routes, {}, 44100, 512).failed()) return false;
            for (int offset = 0; offset < samples.getNumSamples(); offset += 512) {
                playback.renderBlock(block.getArrayOfWritePointers(), 2, 512);
                for (int ch = 0; ch < 2; ++ch) samples.copyFrom(ch, offset, block, ch, 0, 512);
            }
            playback.stop();
            return true;
        };
        juce::AudioBuffer<float> keysOnly(2,4096), bassOnly(2,4096), together(2,4096);
        if (!renderPair(true,false,keysOnly) || !renderPair(false,true,bassOnly) || !renderPair(true,true,together)) return 208;
        if (keysOnly.getMagnitude(0,4096) < .001f || bassOnly.getMagnitude(0,4096) < .001f) return 209;
        for (int ch = 0; ch < 2; ++ch) {
            together.addFrom(ch, 0, keysOnly, ch, 0, 4096, -1.f);
            together.addFrom(ch, 0, bassOnly, ch, 0, 4096, -1.f);
        }
        if (together.getMagnitude(0,4096) > .0001f) return 210;
    }
    MainComponent studio;
    studio.newProject();
    studio.editTracks(ostfstudio::TrackKind::pitched);
    if (!studio.project_ || studio.project_->sourceTrackCount() != 1) return 70;
    auto* original = instrument.get();
    studio.trackControls_[0]->instance = std::move(instrument);
    studio.trackControls_[0]->playableInstrument = true;
    studio.trackControls_[0]->midiChannel = 16;
    studio.addSharedPart(0);
    if (studio.project_->sourceTrackCount() != 2) return 71;
    studio.addSharedPart(1);
    if (studio.project_->sourceTrackCount() != 3 || studio.trackControls_[0]->instance.get() != original) return 72;
    for (size_t i = 1; i <= 2; ++i) {
        const auto& part = *studio.trackControls_[i];
        if (part.instance || part.outputTrack != 0 || part.midiChannel != int(i)
            || studio.project_->sourceMidiChannels[i] != int(i) || !part.plugin.getButtonText().contains("Ch ")) return 73;
    }
    studio.detachSharedPart(1);
    if (studio.trackControls_[1]->outputTrack || studio.trackControls_[2]->outputTrack != 0
        || studio.trackControls_[0]->instance.get() != original || studio.trackControls_[1]->plugin.getButtonText().contains("Ch ")) return 75;
    if (!studio.isEmptyMidiTrack(1) || studio.isEmptyMidiTrack(2)) return 187;
    studio.routeMidiPart(1, 0, 1);
    if (studio.project_->sourceTrackCount() != 3 || studio.trackControls_[1]->outputTrack != 0 || studio.isEmptyMidiTrack(1)) return 188;
    studio.detachSharedPart(1);
    studio.trackControls_[0]->midiChannel = 9;
    studio.addSharedPart(2);
    if (studio.project_->sourceTrackCount() != 4 || studio.project_->trackKinds[3] != ostfstudio::TrackKind::drum
        || studio.trackControls_[3]->midiChannel != 10 || studio.trackControls_[3]->outputTrack != 0) return 74;
    studio.trackControls_[2]->plugin.onClick();
    const auto* sharedWindow = studio.trackControls_[0]->editor.get();
    studio.trackControls_[3]->plugin.onClick();
    if (!sharedWindow || sharedWindow->getAssociatedComponent() != &studio || studio.trackControls_[0]->editor.get() != sharedWindow || studio.trackControls_[2]->editor || studio.trackControls_[3]->editor) return 197;
    studio.loadTrackPlugin(2, original->getPluginDescription());
    if (studio.trackControls_[2]->loading || studio.trackControls_[2]->instance || studio.trackControls_[2]->outputTrack != 0) return 202;
    studio.trackControls_[0]->plugin.onClick();
    if (studio.trackControls_[0]->editor.get() != sharedWindow || studio.trackControls_[0]->instance.get() != original) return 203;
    juce::AudioProcessorParameter* partProgram = nullptr;
    for (auto* param : original->getParameters())
        if (param->getName(64) == "MIDI Program Ch 2") partProgram = param;
    if (!partProgram) return 193;
    partProgram->beginChangeGesture();
    partProgram->setValueNotifyingHost(44.f / 127.f);
    partProgram->endChangeGesture();
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    auto* prompt = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    if (!prompt) return 194;
    auto* tracks = prompt->getComboBoxComponent("track");
    if (!tracks || tracks->getNumItems() != 1 || !tracks->getItemText(0).startsWith("#3 ")) return 195;
    prompt->exitModalState(0);
    if (!studio.project_->programChanges[2].empty()) return 196;
    using Group = ostfstudio::MixerTrackGroup;
    studio.editTracks(ostfstudio::TrackKind::pitched, {}, {}, Group::melody);
    studio.editTracks(ostfstudio::TrackKind::pitched, {}, {}, Group::vocal);
    studio.editTracks(ostfstudio::TrackKind::pitched, {}, {}, Group::vocal);
    const auto writtenBit = 1u << unsigned(ostfstudio::TimelineLane::displayLyrics);
    const auto readingBit = 1u << unsigned(ostfstudio::TimelineLane::pronunciation);
    studio.timelineLaneMask_ |= writtenBit | readingBit;
    auto order = studio.displayedTrackOrder();
    auto before = [&](size_t a, size_t b) { return std::find(order.begin(),order.end(),a)<std::find(order.begin(),order.end(),b); };
    if (!before(4,5) || !before(5,6) || studio.adjacentMovableTrack(4,1) || studio.adjacentMovableTrack(5,-1)) return 198;
    studio.moveTrackRow(6,-1);
    studio.moveTrackRow(1,-1);
    order=studio.displayedTrackOrder();
    if (!before(4,6) || !before(6,5) || !before(1,0) || studio.trackControls_[2]->outputTrack != 0) return 199;
    if (!studio.trackOrderHandle(4).isEmpty()) return 204;
    studio.setSize(1600, 1400);
    studio.trackEditorOpen_ = false;
    studio.resized();
    auto drag = [&](size_t source, size_t target, bool after) {
        const auto from = studio.trackOrderHandle(source).getCentre().toFloat();
        auto row = studio.trackGroupBounds(target);
        const juce::Point<float> to(from.x, float(after ? row.getBottom() - 2 : row.getY() + 2));
        const auto now = juce::Time::getCurrentTime();
        auto event = [&](juce::Point<float> p) {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                juce::ModifierKeys::leftButtonModifier, 1.f, 0, 0, 0, 0, &studio, &studio, now, from, now, 1, p != from);
        };
        studio.mouseDown(event(from));
        studio.mouseDrag(event(to));
        studio.mouseUp(event(to));
    };
    drag(5, 6, false);
    drag(1, 3, true);
    order = studio.displayedTrackOrder();
    if (!before(5,6) || !before(3,1) || studio.trackControls_[2]->outputTrack != 0) return 205;
    const int lyricsHeight = studio.timelineRowHeight() * 2;
    for (const auto part : {4, 5, 6})
        if (studio.trackGroupBounds(part).getY() + lyricsHeight != studio.trackRows_[part].getY()) return 226;
    if (studio.trackGroupBounds(5).getBottom() >= studio.trackGroupBounds(6).getY()
        || studio.trackGroupBounds(4).getBottom() >= studio.trackGroupBounds(5).getY()) return 227;
    for (const auto hidden : {writtenBit, readingBit, writtenBit | readingBit}) {
        studio.timelineLaneMask_ &= ~hidden;
        studio.resized();
        const int height = hidden == (writtenBit | readingBit) ? 0 : lyricsHeight / 2;
        if (studio.trackRows_[5].getY() - studio.trackGroupBounds(5).getY() != height
            || studio.trackRows_[6].getY() - studio.trackGroupBounds(6).getY() != height) return 228;
        studio.timelineLaneMask_ |= hidden;
    }
    studio.resized();
    drag(5,4,false);
    drag(0,6,true);
    if (order != studio.displayedTrackOrder()) return 206;
    studio.trackControls_[1]->instance = std::move(second);
    studio.trackControls_[1]->playableInstrument = true;
    studio.routeMidiPart(5, 1, 5);
    if (studio.trackControls_[5]->outputTrack != 1) return 222;
    studio.trackControls_[2]->plugin.onClick();
    studio.trackControls_[5]->plugin.onClick();
    if (!studio.trackControls_[0]->editor) return 219;
    if (!studio.trackControls_[1]->editor) return 224;
    if (studio.trackControls_[5]->editor) return 225;
    if (!studio.trackControls_[1]->editor->getName().contains("#2")
        || !studio.trackControls_[5]->plugin.getButtonText().contains("GM #2")) return 223;
    const auto firstBounds = studio.trackControls_[0]->editor->getScreenBounds();
    const auto secondBounds = studio.trackControls_[1]->editor->getScreenBounds();
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(firstBounds);
    if (display && display->userArea.getWidth() >= firstBounds.getWidth() + secondBounds.getWidth() + 8
        && firstBounds.intersects(secondBounds)) return 221;
    studio.detachSharedPart(5);
    if (studio.trackControls_[5]->outputTrack || studio.hasSharedParts(1) || !studio.hasSharedParts(0)
        || studio.trackControls_[2]->outputTrack != 0 || studio.project_->sourceTrackCount() != 7) return 220;
    return 0;
}

class VocalTrackProbe final : public juce::Timer {
public:
    VocalTrackProbe(const juce::File& song, const juce::File& wav, std::function<void(int)> done)
        : output(wav), finish(std::move(done)) {
        studio.setSize(1500,1000);
        opened = studio.openDocument(song);
        startTimer(50);
    }
private:
    int verify() {
        auto& project = *studio.project_;
        const int lead = project.trackIds.indexOf("voice-track");
        const int backing = project.trackIds.indexOf("backing");
        if (lead < 0 || backing < 0 || project.primaryMelodyTrack != size_t(lead)
            || studio.selectedTrack_ != size_t(lead) || !studio.vocalLane_.isVisible()) return 3;
        if (!project.chordVoicingTrack || studio.displayedTrackOrder().front() != *project.chordVoicingTrack
            || studio.displayedTrackOrder()[1] != size_t(lead)
            || studio.trackRows_[*project.chordVoicingTrack].getBottom() >= studio.trackGroupBounds(lead).getY()
            || studio.trackRows_[lead].getBottom() >= studio.trackGroupBounds(backing).getY()) return 16;
        const auto pianoBounds = studio.pianoGrid_.getBounds();
        studio.mixerButton_.onClick();
        if (!studio.mixer_.isVisible() || studio.pianoGrid_.isVisible() || studio.pianoButton_.getToggleState()) return 17;
        studio.mixerButton_.onClick();
        if (studio.mixer_.isVisible() || !studio.pianoGrid_.isVisible() || !studio.pianoButton_.getToggleState()
            || studio.pianoGrid_.getBounds() != pianoBounds || studio.selectedTrack_ != size_t(lead)) return 18;
        studio.pianoButton_.onClick();
        studio.mixerButton_.onClick();
        if (!studio.mixer_.isVisible()) return 19;
        studio.mixerButton_.onClick();
        if (studio.mixer_.isVisible() || studio.pianoGrid_.isVisible() || studio.pianoButton_.getToggleState()) return 20;
        studio.pianoButton_.onClick();
        for (const auto i : {lead,backing}) {
            const auto& strip = *studio.trackControls_[i];
            if (!strip.playableInstrument || !strip.instance || strip.instance->getName() != "OSTF Formant Singer") return 4;
        }
        if (studio.trackControls_[lead]->instance.get() == studio.trackControls_[backing]->instance.get()
            || !studio.trackControls_[backing]->plugin.isVisible()) return 12;
        if (studio.vocalLane_.phrases.empty() || studio.vocalLane_.phrases.front().text != juce::String::fromUTF8("雨の音が"))
            return 14;
        studio.vocalLane_.entrance.onClick();
        if (studio.cursorTick_ != project.pitchedNotes[lead].front().startTick || studio.cursorTick_/project.ticksPerBar != 4) return 5;
        const auto& note = project.pitchedNotes[lead].front();
        auto kind = studio.performanceLaneNames_.indexOf("Vibrato depth");
        if (kind < 0) return 6;
        studio.performanceLane_.setSelectedId(kind+1,juce::sendNotificationSync);
        if (!studio.controllerEditor_.isVisible()) return 7;
        const auto id = note.id;
        studio.editPerformancePoint(id,note.startTick,.82);
        if (project.vocalNotes.at(id).technique != "vibrato" || std::abs(project.vocalNotes.at(id).intensity-.82)>.0001) return 8;
        studio.undoPianoEdit(false);
        std::vector<PreviewTransport::TrackSetup> routes(project.trackLanes.size());
        routes[lead].instrument = studio.trackControls_[lead]->instance.get();
        routes[lead].midiChannel = studio.trackControls_[lead]->midiChannel;
        routes[backing].instrument = studio.trackControls_[backing]->instance.get();
        routes[backing].midiChannel = studio.trackControls_[backing]->midiChannel;
        PreviewTransport playback;
        playback.setChannelMix(lead,{},true,0);
        playback.setOutputMix({});
        constexpr int rate = 48000, block = 512;
        if (playback.preparePlayback(project,routes,{},rate,block).failed()) return 9;
        const auto start = project.pitchedNotes[lead].front().startTick;
        playback.seekToTick(start);
        std::unique_ptr<juce::AudioFormatWriter> writer;
        if (output != juce::File{}) {
            if (output.exists()) return 10;
            std::unique_ptr<juce::OutputStream> out = output.createOutputStream();
            if (!out) return 10;
            writer = juce::WavAudioFormat{}.createWriterFor(out,juce::AudioFormatWriterOptions{}
                .withSampleRate(rate).withNumChannels(2).withBitsPerSample(16));
            if (!writer) return 10;
        }
        double energy = 0;
        juce::AudioBuffer<float> audio(2,block);
        const auto frames = juce::roundToInt(rate*(project.clock.secondsAt(start+4*project.ticksPerBar)-project.clock.secondsAt(start)));
        for (int pos=0;pos<frames;pos+=block) {
            const int count = std::min(block,frames-pos);
            playback.renderBlock(audio.getArrayOfWritePointers(),2,count);
            energy += count*std::pow(audio.getRMSLevel(0,0,count),2);
            if (writer && !writer->writeFromAudioSampleBuffer(audio,0,count)) return 10;
        }
        if (energy <= frames*.000001 || project.pitchedNotes[backing].empty()) return 11;
        playback.takeMeters();
        playback.setChannelMix(backing,{},true,0);
        playback.seekToTick(project.pitchedNotes[backing].front().startTick);
        for (int i=0; i<20; ++i) playback.renderBlock(audio.getArrayOfWritePointers(),2,block);
        const auto meters = playback.takeMeters();
        return meters.tracks[lead][0] > .001f && meters.tracks[backing][0] > .001f ? 0 : 13;
    }
    void timerCallback() override {
        if (opened && !studio.canEditTracks() && ++polls < 600) return;
        stopTimer();
        const int result = !opened ? 1 : !studio.canEditTracks() ? 2 : verify();
        if (result) std::cerr << "Vocal track check " << result << '\n';
        finish(result);
    }
    MainComponent studio;
    juce::File output;
    std::function<void(int)> finish;
    bool opened = false;
    int polls = 0;
};
std::unique_ptr<juce::Timer> startVocalTrackCheck(const juce::File& song,const juce::File& wav,std::function<void(int)> done) {
    return std::make_unique<VocalTrackProbe>(song,wav,std::move(done));
}

class PluginFocusProbe final : public juce::Timer {
public:
    PluginFocusProbe(const juce::File& bundle, std::function<void(int)> done)
        : file(bundle), finished(std::move(done)) {
        studio.setBounds(-10000, -10000, 1440, 900);
        studio.addToDesktop(juce::ComponentPeer::windowHasTitleBar);
        studio.setVisible(true);
        studio.newProject();
        startTimer(150);
    }
private:
    int openFromChain() {
        juce::OwnedArray<juce::PluginDescription> types;
        studio.pluginFormats_.getFormat(0)->findAllTypesForFile(types, file.getFullPathName());
        if (types.size() != 1) return 1;
        auto& chain = studio.effectChain(true, 0);
        juce::String error;
        auto& slot = chain.slots.emplace_back();
        slot.effect = studio.pluginFormats_.createPluginInstance(*types[0], 48000, 256, error);
        if (!slot.effect) return 2;
        studio.showEffects(true, 0);
        auto* box = dynamic_cast<juce::CallOutBox*>(juce::Component::getCurrentlyModalComponent());
        if (!box || !box->getChildComponent(0)) return 3;
        for (auto* child : box->getChildComponent(0)->getChildren()) {
            auto* edit = dynamic_cast<juce::TextButton*>(child);
            if (!edit || edit->getButtonText() != TRANS("Edit") || !edit->isShowing()) continue;
            edit->onClick();
            editor = slot.editor.get();
            if (!editor || box->isCurrentlyModal() || editor->isCurrentlyBlockedByAnotherModalComponent()
                || editor->getAssociatedComponent() != &studio || !editor->isShowing()) return 4;
            editor->setTopLeftPosition(-9500, -9500);
            return 0;
        }
        return 5;
    }

    bool editorFocused() const {
#if JUCE_WINDOWS
        const auto handle = static_cast<HWND>(editor->getPeer()->getNativeHandle());
        const auto owner = GetWindow(handle, GW_OWNER);
        const auto host = studio.getPeer()->getNativeHandle();
        const auto focusedRoot = GetAncestor(GetFocus(), GA_ROOT);
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        // Windows can leave an unattended test in the background. Studio must respect that too.
        const bool studioInactive = focusedRoot == nullptr && GetActiveWindow() == nullptr
            && foregroundProcess != GetCurrentProcessId();
        const bool correctFocus = focusedRoot == handle || studioInactive;
        const bool correctStack = owner == host && (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0;
        if (!correctFocus || !correctStack)
            std::cerr << "Focused root " << focusedRoot << ", editor " << handle << ", owner " << owner
                      << ", workspace " << host << ", foreground PID " << foregroundProcess
                      << ", Studio PID " << GetCurrentProcessId() << '\n';
        return correctFocus && correctStack;
#else
        return editor->getPeer()->isFocused();
#endif
    }
    int advance() {
        if (tick == 0) {
            const int result = openFromChain();
            // Leave the editor alone across Studio's 15-second prompt guide refresh.
            startTimer(16000);
            return result;
        }
        if (!editor || !editorFocused()) {
            std::cerr << "Plug-in focus lost at check " << tick << '\n';
            return 6;
        }
        if (tick == 1) {
            if (studio.surfaceWindow_ && studio.surfaceWindow_->isOnDesktop()) return 12;
            if (studio.deviceSettings_ && studio.deviceSettings_->isOnDesktop()) return 13;
            startTimer(150);
        }
        if (tick == 3) {
            PreviewTransport::Inserts inserts{};
            inserts[0][0] = studio.effectChain(true, 0).slots.front().effect.get();
            if (studio.transport_.preparePlayback(*studio.project_, {}, inserts, 48000, 256).failed()) return 7;
            studio.transportActive_ = true;
            studio.startTimerHz(24);
        }
        if (studio.transportActive_) {
            juce::AudioBuffer<float> block(2, 256);
            studio.transport_.renderBlock(block.getArrayOfWritePointers(), 2, 256);
        }
        if (tick == 7) {
            if (studio.cursorTick_ <= 0) return 8;
            studio.stopTransport();
            studio.getPeer()->toFront(true);
            studio.grabKeyboardFocus();
#if JUCE_WINDOWS
            auto hwnd = static_cast<HWND>(studio.getPeer()->getNativeHandle());
            const auto child = editor->getPeer()->getNativeHandle();
            while (hwnd && hwnd != child) hwnd = GetWindow(hwnd, GW_HWNDPREV);
            if (!hwnd) return 9;
#endif
            editor->closeButtonPressed();
            if (editor->isVisible()) return 10;
            studio.openEffectEditor(true, 0, 0);
            if (editor != studio.effectChain(true, 0).slots.front().editor.get() || !editor->isShowing()) return 11;
        }
        return 0;
    }
    void timerCallback() override {
        const int result = advance();
        if (result != 0 || ++tick == 9) { stopTimer(); finished(result); }
    }
    juce::File file;
    std::function<void(int)> finished;
    MainComponent studio;
    juce::Component::SafePointer<juce::DocumentWindow> editor;
    int tick = 0;
};

std::unique_ptr<juce::Timer> startPluginFocusCheck(const juce::File& bundle, std::function<void(int)> done) {
    return std::make_unique<PluginFocusProbe>(bundle, std::move(done));
}

// Optional integration check using the user's installed VST3 and its real native editor.
// Exercise the same menu item twice, closing and reopening the independent window.
std::unique_ptr<juce::Timer> startSpliceWindowCheck(std::function<void(int)> finished) {
    class Probe final : public juce::Timer {
    public:
        explicit Probe(std::function<void(int)> done) : finish(std::move(done)) {
            studio.setBounds(-12000, -12000, 1440, 900);
            studio.addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
            studio.setVisible(true);
            for (auto* child : studio.getChildren())
                if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) menus = bar->getModel();
            if (menus) {
                const auto menu = menus->getMenuForIndex(5, {});
                for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                    if (it.getItem().text == TRANS("Open Splice Sounds")) command = it.getItem().itemID;
                if (auto* pane = descendant<PluginBrowser>(studio)) {
                    browserBounds = pane->getBounds();
                    browserVisible = pane->isVisible();
                }
                if (command) menus->menuItemSelected(command, 5);
            }
            startTimer(100);
        }
    private:
        int inspect() {
            if (!command) return 1;
            PluginBrowser* pane = nullptr;
            for (auto* child : studio.getChildren())
                if (auto* p = dynamic_cast<PluginBrowser*>(child)) pane = p;
            if (!pane || pane->isVisible() != browserVisible || (browserVisible && pane->getBounds() != browserBounds)) return 2;
            juce::DocumentWindow* window = nullptr;
            auto& desktop = juce::Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i) {
                auto* component = desktop.getComponent(i);
                if (component->getName() == "Splice Sounds") window = dynamic_cast<juce::DocumentWindow*>(component);
            }
            auto* editor = window ? descendant<juce::AudioProcessorEditor>(*window) : nullptr;
            if (!editor) {
                const auto types = pane->types();
                const bool installed = std::any_of(types.begin(), types.end(), [](const auto& t) {
                    return t.name.equalsIgnoreCase("Splice Sounds") && t.pluginFormatName == "VST3";
                });
                return installed ? -1 : 77;
            }
            if (!editor->processor.getName().equalsIgnoreCase("Splice Sounds")
                || !editor->isShowing() || editor->getPeer() == studio.getPeer()
                || editor->getWidth() <= 0 || editor->getHeight() <= 0) return 3;
#if JUCE_WINDOWS
            auto* native = descendant<juce::HWNDComponent>(*editor);
            if (!native) return -1;
            auto host = static_cast<HWND>(native->getHWND());
            if (!host || !IsChild(static_cast<HWND>(window->getPeer()->getNativeHandle()), host)
                || !GetWindow(host, GW_CHILD)) return -1;
#endif
            if (!firstEditor) {
                firstEditor = editor;
                window->closeButtonPressed();
                if (editor->isShowing()) return 5;
                menus->menuItemSelected(command, 5);
                return -1;
            }
            return editor == firstEditor ? 0 : 6;
        }
        void timerCallback() override {
            int result = inspect();
            if (result < 0 && juce::Time::getMillisecondCounterHiRes() < deadline) return;
            stopTimer();
            finish(result < 0 ? 7 : result);
        }
        bool browserVisible = false;
        juce::Rectangle<int> browserBounds;
        MainComponent studio;
        std::function<void(int)> finish;
        juce::MenuBarModel* menus = nullptr;
        juce::Component::SafePointer<juce::AudioProcessorEditor> firstEditor;
        int command = 0;
        double deadline = juce::Time::getMillisecondCounterHiRes() + 30000;
    };
    return std::make_unique<Probe>(std::move(finished));
}

int checkSamplePlacement(const juce::File& file) {
    MainComponent main;
    main.setSize(1650, 1400);
    main.newProject();
    main.editTracks(ostfstudio::TrackKind::audio);
    main.editTracks(ostfstudio::TrackKind::audio);
    const auto x = main.rowOverlay_.getX() + main.trackHeaderWidth + 40;
    main.filesDropped({file.getFullPathName()}, x, main.trackRows_[0].getCentreY());
    auto* clip = main.clipOn(0);
    if (!clip || clip->localFile != file || clip->startTick != main.arrangementTickAt(x)) return 151;
    const auto start = clip->startTick;
    const auto length = clip->durationTicks;
    const auto begin = juce::Point<int>(x + 14, main.trackRows_[0].getCentreY());
    auto drag = [&](juce::Point<int> position, bool moved) {
        const auto time = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
            juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &main, &main, time, begin.toFloat(), time, 1, moved);
    };
    main.splitTool_.onClick();
    main.mouseDown(drag(begin,false));
    if (!main.splitTool_.getToggleState() || main.clipsOn(0).size()!=2 || !main.clipAt(0,main.arrangementTickAt(begin.x))) return 216;
    main.joinTool_.onClick();
    main.mouseDown(drag({x+2,begin.y},false));
    if (main.splitTool_.getToggleState() || !main.joinTool_.getToggleState() || main.clipsOn(0).size()!=1
        || main.clipsOn(0).front().end!=start+length) return 217;
    main.joinTool_.onClick();
    main.mouseDown(drag(begin, false));
    if (!main.movingAudio_ || main.resizingAudio_ != 0) return 152;
    const auto end = juce::Point<int>(begin.x + 120, main.trackRows_[1].getCentreY());
    main.mouseDrag(drag(end, true));
    main.mouseUp(drag(end, true));
    clip = main.clipOn(1);
    if (main.clipOn(0) || !clip || clip->durationTicks != length || clip->startTick <= start
        || clip->startFrame != 0 || clip->timing.leadingTicks != 0 || clip->localFile != file) return 153;
    using Op = MainComponent::ClipOp;
    auto hit = main.clipAt(1, clip->startTick);
    if (!hit || length < 4) return 170;
    const auto original = *hit;
    const auto cut = original.start + length / 2;
    clip->timing = {true, length, {{0.3, 0.6}}};
    main.clipCommand(Op::split, hit, 1, cut);
    auto right = main.clipAt(1, cut);
    if (main.clipsOn(1).size() != 2 || !right || right->id == original.id || right->end != original.end
        || std::abs(main.clipOn(1)->timing.phaseOffset - double(cut - original.start) / length) > 1e-7) return 171;
    main.clipCommand(Op::copy, right, 0, 0);
    main.clipCommand(Op::paste, {}, 0, 0);
    auto pasted = main.clipAt(0, 0);
    if (!pasted || pasted->id == right->id || pasted->end != right->end - right->start) return 172;
    main.clipCommand(Op::duplicate, pasted, 0, 0);
    if (main.clipsOn(0).size() != 2 || !main.clipAt(0, pasted->end)) return 173;
    main.clipCommand(Op::paste, {}, 0, 1);
    if (main.clipsOn(0).size() != 2) return 174;
    main.clipCommand(Op::join, main.clipAt(1, original.start), 1, 0);
    auto joined = main.clipAt(1, original.start);
    if (!joined || joined->id != original.id || joined->end != original.end || main.clipsOn(1).size() != 1
        || !main.clipOn(1)->timing.loop || main.clipOn(1)->localFile != file) return 175;
    main.undoPianoEdit(false);
    if (main.clipsOn(1).size() != 2 || main.clipsOn(0).size() != 2) return 176;
    main.undoPianoEdit(true);
    if (main.clipsOn(1).size() != 1) return 177;
    // A separator file carries playable triads and named regions in the same standard MIDI track.
    juce::MidiMessageSequence notes;
    notes.addEvent(juce::MidiMessage::textMetaEvent(3, "OSTF Stem Separator Chords"));
    notes.addEvent(juce::MidiMessage::textMetaEvent(6, "Dm"), 0);
    notes.addEvent(juce::MidiMessage::controllerEvent(4, 32, 8), 0);
    notes.addEvent(juce::MidiMessage::programChange(4, 33), 0);
    for (int pitch : {62,65,69}) {
        notes.addEvent(juce::MidiMessage::noteOn(4, pitch, juce::uint8(90)), 0);
        notes.addEvent(juce::MidiMessage::noteOff(4,pitch), 480);
    }
    juce::TemporaryFile temporary(".mid");
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(480); midi.addTrack(notes);
    { auto out = temporary.getFile().createOutputStream(); if (!out || !midi.writeTo(*out)) return 154; }
    const auto before = main.project_->sourceTrackCount();
    main.filesDropped({temporary.getFile().getFullPathName()}, x, main.trackRows_[0].getCentreY());
    if (main.project_->sourceTrackCount() != before + 1 || main.project_->pitchedNotes[before].size() != 3) return 155;
    const auto midiClips = main.clipsOn(before);
    if (midiClips.size() != 1) return 178;
    const auto first = midiClips.front();
    if (main.trackControls_[before]->midiChannel != 4 || main.project_->programChanges[before].size() != 1
        || main.project_->programChanges[before][0].program != 33) return 189;
    main.clipCommand(Op::duplicate, first, before, 0);
    auto duplicate = main.clipAt(before, first.end);
    if (!duplicate || duplicate->id == first.id || main.project_->pitchedNotes[before].size() != 6) return 179;
    const auto midiCut = first.start + (first.end - first.start) / 2;
    main.clipCommand(Op::split, first, before, midiCut);
    if (main.clipsOn(before).size() != 3 || !main.clipAt(before, midiCut)) return 180;
    main.clipCommand(Op::join, main.clipAt(before, first.start), before, 0);
    if (main.clipsOn(before).size() != 2 || main.clipAt(before, first.start)->end != first.end) return 181;
    main.clipCommand(Op::copy, duplicate, before, 0);
    main.clipCommand(Op::paste, {}, before, duplicate->end);
    auto last = main.clipAt(before, duplicate->end);
    if (!last || last->id == duplicate->id) return 182;
    main.clipCommand(Op::move, last, before, last->end);
    if (main.clipAt(before, last->start) || !main.clipAt(before, last->end) || main.clipAt(before, last->end)->id != last->id) return 183;
    main.verticalScroll_.setCurrentRangeStart(0, juce::sendNotificationSync);
    main.timelineLaneMask_ |= 1u << static_cast<unsigned>(ostfstudio::TimelineLane::chord);
    main.refreshTimelineLanes();
    main.resized();
    bool placedChord = false;
    const auto expectedTick = main.arrangementTickAt(x);
    for (int y = main.rowOverlay_.getY(); y < main.rowOverlay_.getBottom(); ++y) {
        if (main.globalLaneAt({x,y}) != ostfstudio::TimelineLane::chord) continue;
        main.filesDropped({temporary.getFile().getFullPathName()}, x, y);
        const auto score = juce::JSON::parse(main.project_->sourceScore.toString());
        for (const auto& region : *score["harmony"]["chords"].getArray())
            if (region["label"] == "Dm" && juce::int64(region["start"]) == expectedTick) placedChord = true;
        if (!placedChord) {
            std::fprintf(stderr, "Chord drop at %lld: %s; %s\n", static_cast<long long>(expectedTick),
                         main.statusLabel_.getText().toRawUTF8(), juce::JSON::toString(score["harmony"]["chords"], true).toRawUTF8());
            return 156;
        }
        break;
    }
    if (!placedChord) return 232;
    {
        using namespace ostfstudio;
        const auto score=juce::JSON::parse(R"({"harmony":{"chords":[{"id":"a","start":0,"duration":480,"label":"C"},{"id":"b","start":480,"duration":960,"label":"G"}]},"timeline":{"tempoChanges":[{"at":0,"bpm":100},{"at":480,"bpm":130}]}})");
        for (bool later:{false,true}) {
            juce::Array<juce::var> remove,insert;
            if(globalBoundaryEdit(score,TimelineLane::chord,120,1440,false,later,remove,insert).failed() || remove.size()!=2 || insert.size()!=1
                || int(insert[0]["duration"])!=1440 || insert[0]["label"].toString()!=(later?"G":"C")) return 208;
            remove.clear(); insert.clear();
            if(globalBoundaryEdit(score,TimelineLane::tempo,120,1440,false,later,remove,insert).failed() || int(insert[0]["at"])!=0
                || int(insert[0]["bpm"])!=(later?130:100)) return 209;
        }
        juce::Array<juce::var> remove,insert;
        if(globalBoundaryEdit(score,TimelineLane::chord,120,1440,true,false,remove,insert).failed() || insert.size()!=2
            || int(insert[0]["duration"])!=120 || int(insert[1]["start"])!=120 || int(insert[1]["duration"])!=360
            || insert[0]["id"]==insert[1]["id"]) return 210;
        const std::vector<PitchedNote> notes{{0,480,62,90},{0,480,65,90},{0,480,69,90}};
        OstfpPreview single;
        single.trackLanes.resize(1);
        single.trackKinds = {TrackKind::pitched};
        single.pitchedNotes = {notes};
        const ChordAnalysis singlePart(single);
        if (singlePart.rank({0,480})[0] != "Dm" || !singlePart.rank({480,960}).isEmpty()) return 211;
        OstfpPreview overlap;
        auto withTracks = score.clone();
        withTracks.getDynamicObject()->setProperty("tracks", juce::JSON::parse(
            R"([{"clips":[{"id":"disabled-clip","enabled":false}]},{"clips":[]},{"clips":[]}])"));
        const auto json=juce::JSON::toString(withTracks);
        overlap.sourceScore=juce::MemoryBlock(json.toRawUTF8(),json.getNumBytesAsUTF8());
        overlap.durationTicks=1440; overlap.ticksPerBar=1440;
        // The melody sustains C while the bass and keys supply C major, then F major.
        overlap.trackLanes.resize(6);
        overlap.trackKinds = {TrackKind::pitched, TrackKind::pitched, TrackKind::pitched,
                              TrackKind::drum, TrackKind::audio, TrackKind::pitched};
        overlap.chordVoicingTrack = 5;
        overlap.pitchedNotes = {{{0,1440,60}}, {{0,480,48},{480,1440,41}},
                                {{480,1440,69},{0,480,64},{0,480,67}}, {}, {}, {}};
        const auto clean = ChordAnalysis(overlap).review({{0,1440}});
        PitchedNote noise{0,1920,37,100};
        for (const auto i : {3,4,5}) overlap.pitchedNotes[i].push_back(noise);
        noise.fixedTrigger = true;
        overlap.pitchedNotes[0].push_back(noise);
        noise.fixedTrigger = false;
        noise.enabled = false;
        overlap.pitchedNotes[0].push_back(noise);
        noise.enabled = true;
        noise.clipId = "disabled-clip";
        overlap.pitchedNotes[0].push_back(noise);
        const ChordAnalysis ensemble(overlap);
        const auto decisions=ensemble.review({{0,1440}});
        if(decisions.size()!=2 || decisions[0].range!=ReviewRange(0,480) || decisions[1].range!=ReviewRange(480,1440)
            || decisions[0].candidates[0]!="C" || decisions[1].candidates[0]!="F") return 218;
        if (clean.size() != 2 || clean[0].candidates != decisions[0].candidates ||
            clean[1].candidates != decisions[1].candidates || !ensemble.rank({1440,1920}).isEmpty()) return 220;
        const auto touched = ensemble.review({{40,70}});
        if (touched.size() != 1 || touched.front().range != decisions.front().range) return 222;
        const auto cropped = ensemble.review({{420,510}}, {}, true);
        if (cropped.size() != 2 || cropped[0].candidates != decisions[0].candidates ||
            cropped[1].candidates != decisions[1].candidates) return 221;
        ChordReviewLane lane;
        lane.timeline=[] {return std::pair{juce::Rectangle<int>(40,0,144,42),juce::Range<double>(0,1440)};};
        lane.setBounds(0,0,184,44);lane.display(decisions,*main.project_);
        if(lane.intervalBounds(0).getX()!=40 || lane.intervalBounds(0).getWidth()!=48 ||
            lane.intervalBounds(1).getX()!=88 || lane.intervalBounds(1).getWidth()!=96) return 219;
        lane.setVisible(true);
        auto* second = lane.findChildWithID("chord-decision-1");
        if (!second || !second->isVisible() || !lane.intervalBounds(1).contains(second->getBounds())
            || lane.getComponentAt(second->getBounds().getCentre()) != second) return 232;
        int decisionsMade = 0;
        lane.decide = [&](bool all, ChordReviewAction action) {
            if (!all && action == ChordReviewAction::keep && lane.selected() == 1) ++decisionsMade;
        };
        auto menu = lane.decisionMenu(1);
        int actions = 0;
        for (juce::PopupMenu::MenuItemIterator item(menu); item.next();) {
            if (item.getItem().subMenu) return 233;
            if (item.getItem().action) ++actions;
            if (item.getItem().text == TRANS("Keep")) item.getItem().action();
        }
        if (actions != 3 || decisionsMade != 1) return 233;
        remove.clear(); insert.clear();
        ChordDecision middle{{120,360},"C",{"Dm"},{},1};
        if(chordDecisionEdits(score,{middle},remove,insert).failed() || remove.size()!=1 || insert.size()!=3
            || insert[0]["label"]!="Dm" || insert[1]["id"]!="a" || int(insert[2]["start"])!=360) return 212;
        remove.clear(); insert.clear();
        auto defaultScale=juce::JSON::parse(R"({"song":{"scale":{"id":"sm","label":"Major","tonic":{"step":"C","alter":0},"intervals":[0,2,4,5,7,9,11]}}})");
        if(globalBoundaryEdit(defaultScale,TimelineLane::scale,240,1440,true,false,remove,insert).failed() ||
            !remove.isEmpty() || insert.size()!=2 || int(insert[1]["duration"])!=1200 || insert[0]["id"]==insert[1]["id"]) return 215;
        auto moved=notes.front(); moved.id="moved"; auto shifted=moved; shifted.startTick+=480; shifted.endTick+=480;
        if(changedNoteRanges({moved},{{NoteEdit::change,shifted}}).size()!=2) return 213;
    }
    std::unique_ptr<ostfstudio::OstfpPreview> source;
    juce::String details;
    if (ostfstudio::prepareMediaImport(temporary.getFile(), source, details).failed()) return 190;
    ostfstudio::SessionMix materialMix;
    materialMix.tracks.resize(source->sourceTrackCount());
    for (const auto kind : source->trackKinds) materialMix.audioTracks.push_back(kind == ostfstudio::TrackKind::audio);
    for (bool packed : {false, true}) {
        juce::TemporaryFile material(packed ? ".ostfp" : ".ostf");
        {
            auto out = material.getFile().createOutputStream();
            if (!out || (packed ? ostfstudio::writeOstfpPreview(*source, materialMix, *out)
                                : ostfstudio::writeOstfPreview(*source, *out)).failed()) return 191;
        }
        const auto tracks = main.project_->sourceTrackCount();
        main.revealRow(main.trackRows_[0]);
        const auto dropTick = main.arrangementTickAt(x);
        main.filesDropped({material.getFile().getFullPathName()}, x, main.trackRows_[0].getCentreY());
        if (main.project_->sourceTrackCount() != tracks + 1 || main.project_->pitchedNotes[tracks].size() != 3
            || main.project_->programChanges[tracks].size() != 1 || main.project_->programChanges[tracks][0].program != 33) return 192;
        if (main.pendingReview_.empty() || !main.chordReview_.isVisible() || main.pendingReview_.front().range.getStart() != dropTick
            || main.pendingReview_.front().sourceChord["label"] != "Dm") return 201;
        main.selectTrack(0);
        if (main.selectedTrack_ != tracks) return 202;
        auto button = [&](const char* id) {
            return dynamic_cast<juce::TextButton*>(main.chordReview_.findChildWithID(id));
        };
        auto* decision = button("chord-confirm-all");
        auto choose = [&](const char* label, bool all = false) {
            if (all) {
                auto* batch = button(juce::String(label) == "Confirm" ? "chord-confirm-all"
                                   : juce::String(label) == "Keep" ? "chord-keep-all" : "chord-cancel-all");
                if (!batch || !batch->isVisible() || !batch->isEnabled()) return false;
                batch->onClick();
                return true;
            }
            auto menu = main.chordReview_.decisionMenu(main.chordReview_.selected());
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                if (it.getItem().text == juce::translate(label) && it.getItem().action) {
                    it.getItem().action(); return true;
                }
            return false;
        };
        if (!decision || !decision->isEnabled()) return 223;
        const auto imported = main.project_->sourceScore;
        const auto pendingCount = main.pendingReview_.size();
        main.leadButton_.onClick();
        if (!main.scoreView_ || !main.scoreView_->isVisible() || !main.scoreView_->showsLeadSheet()
            || main.scoreView_->getBounds().isEmpty() || main.chordReview_.isVisible()) return 234;
        main.scoreButton_.onClick();
        if (!main.scoreView_->isVisible() || main.scoreView_->showsLeadSheet()) return 235;
        main.mixerButton_.onClick();
        if (!main.mixer_.isVisible() || main.scoreView_->isVisible() || main.chordReview_.isVisible()
            || main.chordReviewHeight() != 0) return 236;
        main.pianoButton_.onClick();
        if (!main.chordReview_.isVisible() || main.pendingReview_.size() != pendingCount ||
            main.project_->sourceScore != imported || main.selectedTrack_ != tracks) return 237;
        for (auto id : {"chord-confirm-all", "chord-keep-all", "chord-cancel-all"})
            if (!button(id) || button(id)->getRight() > main.chordReview_.timeline().first.getX()) return 238;
        main.setSize(680, 550);
        if (main.pianoGrid_.getHeight() < 10 || main.chordReview_.getHeight() < 36
            || decision->getHeight() < 16 || main.chordReview_.getBottom() > main.pianoGrid_.getY()) return 239;
        main.setSize(1650, 1400);
        if (!choose("Keep", true)) return 223;
        main.selectTrack(0);
        if (main.selectedTrack_ != 0 || !main.pendingReview_.empty()) return 203;
        main.selectTrack(tracks);
        if (!main.chordReview_.isVisible() || main.project_->sourceScore != imported || decision->isEnabled()) return 224;
        auto note = main.project_->pitchedNotes[tracks].front();
        note.pitch += 1;
        main.applyNoteGesture({{ostfstudio::NoteEdit::change,note}});
        if (main.pendingReview_.empty() || main.pendingReview_.at(size_t(main.chordReview_.selected())).candidates.size()!=3) return 204;
        main.undoPianoEdit(false);
        if (!main.pendingReview_.empty()) return 205;
        main.undoPianoEdit(true);
        if (main.pendingReview_.empty()) return 206;
        if (!decision->isEnabled()) return 225;
        if (packed) {
            const auto edited = main.project_->sourceScore;
            if (!choose("Cancel chord", true) || !main.pendingReview_.empty()) return 226;
            const auto notesOf = [tracks](const juce::MemoryBlock& score) {
                return juce::JSON::toString(juce::JSON::parse(score.toString())["tracks"][int(tracks)]["clips"]);
            };
            if (notesOf(main.project_->sourceScore) != notesOf(imported)) return 226;
            main.undoPianoEdit(false);
            if (main.project_->sourceScore != edited || main.pendingReview_.empty()) return 227;
            if (!choose("Keep", true) || main.project_->sourceScore != edited) return 228;
            // One note moves across clips, a second is deleted, and a copy is inserted elsewhere.
            // Cancelling the batch must restore the original note IDs, attributes and clip inventory.
            const auto originalNotes = main.project_->pitchedNotes[tracks];
            auto shifted = originalNotes[0];
            shifted.startTick += main.project_->ticksPerBar;
            shifted.endTick += main.project_->ticksPerBar;
            auto copied = originalNotes[2];
            copied.startTick = shifted.startTick;
            copied.endTick = shifted.endTick;
            main.applyNoteGesture({{ostfstudio::NoteEdit::change, shifted},
                                   {ostfstudio::NoteEdit::erase, originalNotes[1]},
                                   {ostfstudio::NoteEdit::duplicate, copied}});
            if (main.pendingReview_.size() < 2 || !choose("Cancel chord", true) ||
                notesOf(main.project_->sourceScore) != notesOf(edited) || !main.pendingReview_.empty()) return 229;
            auto repitched = originalNotes[0];
            ++repitched.pitch;
            main.applyNoteGesture({{ostfstudio::NoteEdit::change, repitched}, {ostfstudio::NoteEdit::duplicate, copied}});
            if (main.pendingReview_.size() < 2 || !choose("Cancel chord") || main.pendingReview_.empty()) return 230;
            const auto& remainingNotes = main.project_->pitchedNotes[tracks];
            const auto restored = std::find_if(remainingNotes.begin(), remainingNotes.end(),
                [&](const auto& n) { return n.id == repitched.id; });
            if (remainingNotes.size() != originalNotes.size() + 1 || restored == remainingNotes.end() ||
                restored->pitch != originalNotes[0].pitch || !choose("Keep", true)) return 231;
            main.selectTrack(0);
            if (main.selectedTrack_ != 0) return 228;
            continue;
        }
        auto& firstDecision=main.pendingReview_.at(size_t(main.chordReview_.selected()));
        firstDecision.choice = firstDecision.candidates[0] == firstDecision.current ? 2 : 1;
        const auto expected=firstDecision.candidates[firstDecision.choice-1];
        const auto at=firstDecision.range.getStart();
        const auto remaining=main.pendingReview_.size()-1;
        if (!choose("Confirm")) return 225;
        const auto updated=juce::JSON::parse(main.project_->sourceScore.toString());
        if(main.pendingReview_.size()!=remaining || ostfstudio::globalEventAt(updated,ostfstudio::TimelineLane::chord,at)["label"]!=expected) return 214;
        if (!main.pendingReview_.empty()) {
            main.selectTrack(0);
            if(main.selectedTrack_ != tracks) return 217;
            if (!choose("Confirm", true)) return 225;
        }
        if (!main.pendingReview_.empty()) return 207;
    }
    return 0;
}

int checkTrackView(const juce::File& fixture) {
    const auto wave = fixture.getParentDirectory().getParentDirectory().getParentDirectory()
        .getChildFile("examples/demo-songs/assets/glass-lantern-chimes.wav");
    if (const auto result = checkSamplePlacement(wave)) return result;
    using namespace ostfstudio;
    using A = SurfaceAction;
    if (mackieAction(juce::MidiMessage::pitchWheel(8,16383)).index != 7
        || mackieAction(juce::MidiMessage::controllerEvent(1,16,65)).value != -1
        || mackieAction(juce::MidiMessage::pitchWheel(9,8192)).kind != A::none) return 95;
    auto note = [](int n) { return mackieAction(juce::MidiMessage::noteOn(1,n,juce::uint8(127))); };
    if (note(0x5e).kind != A::play || note(0x5f).kind != A::record || note(0x2f).kind != A::bankRight
        || note(0x13).kind != A::mute || note(0x13).index != 3
        || mackieAction(juce::MidiMessage::noteOff(1,0x5e)).kind != A::none) return 93;
    MidiInputRoute route{"selected-port", "Selected keyboard", 2};
    if (!route.accepts("selected-port", 2) || route.accepts("selected-port", 1) || route.accepts("other-port", 2)) return 101;
    route.enabled = false;
    if (route.accepts("selected-port", 2)) return 102;
    route = {};
    if (!route.accepts("other-port", 16) || route.accepts("other-port", 0) || route.accepts("other-port", 17)) return 103;
    PianoNoteGrid grid;
    PianoInspector colours(grid);
    colours.showLegend = true;
    colours.setSize(492, 285);
    for (int i = 0; i < static_cast<int>(pianoHarmony::Theme::count); ++i) {
        colours.theme = static_cast<pianoHarmony::Theme>(i);
        const auto legend = colours.createComponentSnapshot(colours.getLocalBounds());
        for (int pc = 0; pc < 12; ++pc)
            if (legend.getPixelAt(8 + 40 * pc, PianoInspector::controlsHeight + 20) != pianoHarmony::degreeColour(pc + 60, 0)) return 67;
        if (legend.getPixelAt(7, PianoInspector::controlsHeight + 67) != pianoHarmony::toneColour(pianoHarmony::Tone::root)) return 68;
    }
    double scrolled = 0;
    grid.setSize(200, 90);
    grid.viewportPan.scroll = [&](double fraction) { scrolled += fraction; };
    const auto when = juce::Time::getCurrentTime();
    const auto panEvent = [&](float x, int modifiers) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {x, 30}, modifiers,
            1.f, 0, 0, 0, 0, &grid, &grid, when, {100, 30}, when, 1, x != 100);
    };
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown(panEvent(100, left | juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier));
    grid.mouseDrag(panEvent(60, left));
    grid.mouseUp(panEvent(60, 0));
    if (std::abs(scrolled - .2) > .0001 || !grid.selection.isEmpty()) return 134;
    MainComponent main;
    main.setVisible(true);
    main.setSize(1450, 1200);
    auto* tracks = findControl<juce::ComboBox>(main, TRANS("Arrangement track"));
    juce::MenuBarComponent* menus = nullptr;
    for (auto* child : main.getChildren())
        if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) menus = bar;
    if (!main.getName().contains(TRANS("Unsaved project"))
        || !tracks || tracks->getNumItems() != 0 || !menus || !menus->getModel()) return 13;
    auto& model = *menus->getModel();
    const juce::StringArray names {TRANS("File"), TRANS("View"), TRANS("Track"), TRANS("Mode"), TRANS("Settings"), TRANS("External connections"), TRANS("Help")};
    if (model.getMenuBarNames() != names) return 38;
    const auto external = model.getMenuForIndex(5, {});
    juce::StringArray actions;
    for (juce::PopupMenu::MenuItemIterator it(external); it.next();) actions.add(it.getItem().text);
    if (!actions.contains(TRANS("Open Splice Sounds")) || !actions.contains("Melodyne...") || !actions.contains("Ableton Link")) return 121;
    auto* examples = findControl<juce::TextEditor>(main, TRANS("Example prompts"));
    auto* connection = main.findChildWithID("chatgpt-connection");
    if (!examples || !examples->isReadOnly() || !connection || !connection->isVisible()) return 122;
    const juce::StringArray modeNames{"AI Arrangement Studio", "AI Lyric Studio", "AI Vocal Studio"};
    const auto modes = model.getMenuForIndex(3, {});
    if (modes.getNumItems() != modeNames.size()) return 122;
    juce::StringArray displayedExamples;
    int previousMode = 0;
    for (juce::PopupMenu::MenuItemIterator it(modes); it.next();) {
        const auto& mode = it.getItem();
        if (!modeNames.contains(mode.text) || !mode.isEnabled) return 122;
        if (mode.isTicked) previousMode = mode.itemID;
        model.menuItemSelected(mode.itemID, 3);
        const auto selectedMenu = model.getMenuForIndex(3, {});
        for (juce::PopupMenu::MenuItemIterator selected(selectedMenu); selected.next();)
            if (selected.getItem().isTicked != (selected.getItem().itemID == mode.itemID)) return 122;
        const auto example = examples->getText();
        if (example.isEmpty() || displayedExamples.contains(example)) return 122;
        displayedExamples.add(example);
        auto* next = findControl<juce::TextButton>(main, TRANS("Next example"));
        auto* previous = findControl<juce::TextButton>(main, TRANS("Previous example"));
        if (!next || !previous) return 162;
        next->onClick();
        if (examples->getText().isEmpty() || examples->getText() == example) return 162;
        previous->onClick();
        if (examples->getText() != example) return 162;
    }
    if (!previousMode) return 122;
    model.menuItemSelected(previousMode, 3);
    if (!findControl<EditorClipLane>(main, TRANS("Clips"))) return 123;
    EditorClipLane exampleLane;
    exampleLane.clips = {{{1, 2}, "A"}, {{3, 5}, "B"}};
    if (exampleLane.at(1.5) != 1. || exampleLane.at(2.) || exampleLane.at(3.) != 3. || exampleLane.at(5.)) return 124;
    const auto help = model.getMenuForIndex(6, {});
    bool hasShortcuts = false;
    for (juce::PopupMenu::MenuItemIterator it(help); it.next();)
        if (it.getItem().text == TRANS("Shortcuts...")) hasShortcuts = it.getItem().isEnabled;
    if (!hasShortcuts) return 39;
    const auto fileMenu = model.getMenuForIndex(0, {});
    bool newEnabled = false, saveScoreEnabled = false, savePackageEnabled = false;
    juce::StringArray songs;
    for (juce::PopupMenu::MenuItemIterator it(fileMenu, true); it.next();) {
        const auto& item = it.getItem();
        if (item.text == TRANS("New project")) newEnabled = item.isEnabled;
        if (item.text == TRANS("Save as OSTF...")) saveScoreEnabled = item.isEnabled;
        if (item.text == TRANS("Save as OSTFP...")) savePackageEnabled = item.isEnabled;
        if (item.text == TRANS("Demo songs") && item.subMenu)
            for (juce::PopupMenu::MenuItemIterator demo(*item.subMenu); demo.next();)
                songs.add(demo.getItem().text);
    }
    if (!newEnabled || !saveScoreEnabled || !savePackageEnabled || songs.size() != 7 || !songs.contains("Blue Hour Waltz (Shared GM)")
        || !songs.contains("Copper Mile (Rock)") || !songs.contains("First Light Vector (Trance)")) return 14;
    auto* group = findControl<juce::ComboBox>(main, TRANS("Track height group"));
    auto* height = findControl<juce::Slider>(main, TRANS("Group track height"));
    if (!group || !height) return 16;
    const auto choose = [&](const juce::String& title) {
        for (int i = 0; i < group->getNumItems(); ++i)
            if (group->getItemText(i) == title) {
                group->setSelectedItemIndex(i, juce::sendNotificationSync);
                return true;
            }
        return false;
    };
    if (!choose(TRANS("Pitched tracks"))) return 17;
    height->setValue(84, juce::sendNotificationSync);
    const auto trackMenu = model.getMenuForIndex(2, {});
    for (juce::PopupMenu::MenuItemIterator it(trackMenu); it.next();)
        if (it.getItem().text == TRANS("Add pitched track") && it.getItem().isEnabled)
            model.menuItemSelected(it.getItem().itemID, 2);
    if (tracks->getNumItems() != 1 || tracks->getSelectedId() != 1) return 15;
    const auto renameMenu = model.getMenuForIndex(2, {});
    int renameId = 0;
    for (juce::PopupMenu::MenuItemIterator it(renameMenu); it.next();)
        if (it.getItem().text == TRANS("Rename track...")) renameId = it.getItem().itemID;
    if (!renameId) return 126;
    model.menuItemSelected(renameId, 2);
    auto* inlineName = dynamic_cast<juce::TextEditor*>(main.findChildWithID("inline-track-name"));
    if (!inlineName || !inlineName->isVisible()) return 127;
    const auto originalName = tracks->getText();
    inlineName->setText("Keep original", false);
    inlineName->onEscapeKey();
    if (inlineName->isVisible() || tracks->getText() != originalName) return 128;
    model.menuItemSelected(renameId, 2);
    inlineName->setText("Inline track", false);
    inlineName->onReturnKey();
    if (inlineName->isVisible() || tracks->getText() != "Inline track") return 129;
    auto* addedGrip = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!addedGrip || addedGrip->height() != 84) return 18;
    auto* info = findControl<ViewButton>(main, TRANS("Song information"));
    if (!info || !info->isEnabled() || info->getTooltip().isEmpty() || !info->onClick) return 37;
    const auto click = [&](const char* label) {
        auto* button = findControl<ViewButton>(main, juce::translate(label));
        if (!button || !button->isEnabled() || button->getTooltip().isEmpty() || !button->onClick) return false;
        button->onClick();
        return true;
    };
    main.setSize(680, 570);
    juce::RectangleList<int> icons;
    icons.add(connection->getBounds());
    for (auto* child : main.getChildren()) {
        if (auto* button = dynamic_cast<ViewButton*>(child)) {
            if (button->getTooltip().isEmpty() || icons.intersectsRectangle(button->getBounds())
                || !main.getLocalBounds().contains(button->getBounds())) return 33;
            icons.add(button->getBounds());
        }
    }
    if (!click("Add melody track")) return 91;
    auto* melody = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!melody || melody->height() != 100) return 92;
    auto* arm = findControl<juce::TextButton>(main, tracks->getText() + " record arm");
    auto* input = findControl<juce::TextButton>(main, tracks->getText() + " MIDI input");
    if (!arm || !arm->isVisible() || !input || !input->isVisible()) return 105;
    arm->onClick();
    if (!arm->getToggleState()) return 106;
    arm->onClick();
    if (arm->getToggleState()) return 107;
    if (!click("Add chorus vocal track") || !click("Add drum / percussion track")
        || !click("Add audio track") || tracks->getNumItems() != 5) return 34;
    auto* audioEditor = dynamic_cast<AudioClipEditor*>(main.findChildWithID("audio-clip-editor"));
    if (!audioEditor || !audioEditor->isVisible() || audioEditor->getWidth() < 200) return 51;
    if (!audioEditor->recordRuler.chooseCycle || !audioEditor->recordRuler.cycleBars) return 67;
    audioEditor->recordRuler.chooseCycle(.12, .39);
    if (audioEditor->recordRuler.cycleBars().isEmpty()) return 68;
    audioEditor->channels.setSelectedId(2, juce::sendNotificationSync);
    const auto audioTrack = tracks->getSelectedId();
    tracks->setSelectedId(1, juce::sendNotificationSync);
    if (audioEditor->isVisible()) return 52;
    tracks->setSelectedId(audioTrack, juce::sendNotificationSync);
    if (audioEditor->channels.getSelectedId() != 2) return 69;
    if (!audioEditor->isVisible() || !main.isInterestedInFileDrag({"Splice sample.wav"})
        || !main.isInterestedInFileDrag({"notes.mid"})) return 53;
    main.setSize(1450, 1200);
    const auto sample = fixture.getParentDirectory().getParentDirectory().getParentDirectory()
        .getChildFile("examples/demo-songs/assets/glass-lantern-chimes.wav");
    const auto drop = audioEditor->getBounds().getCentre();
    auto* pane = descendant<PluginBrowser>(main);
    if (!click("Media library")) return 140;
    pane = descendant<PluginBrowser>(main);
    if (!pane) return 140;
    auto* assets = dynamic_cast<juce::Button*>(pane->findChildWithID("browser-samples"));
    auto* plugins = dynamic_cast<juce::Button*>(pane->findChildWithID("browser-plugins"));
    if (!assets || !plugins) return 141;
    if (!assets->getToggleState() || !pane->isVisible()) return 141;
    auto* files = descendant<juce::FileBrowserComponent>(*pane);
    if (!files || !files->isVisible() || !files->getDisplayComponent()) return 142;
    files->setRoot(sample.getParentDirectory());
    const juce::DragAndDropTarget::SourceDetails dragging("ostf-media:" + sample.getFullPathName(),files,drop);
    auto* dropTarget = dynamic_cast<juce::DragAndDropTarget*>(static_cast<juce::Component*>(&main));
    if (!dropTarget || !dropTarget->isInterestedInDragSource(dragging)) return 143;
    dropTarget->itemDropped(dragging);
    plugins->onClick();
    if (files->isVisible() || !plugins->getToggleState()) return 144;
    const auto* imported = audioEditor->sourceClip();
    if (!imported || imported->localFile != sample || imported->durationTicks <= 0) return 54;
    for (auto* child : audioEditor->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b && (b->getButtonText() == TRANS("Warp (re-pitch)") || b->getButtonText() == TRANS("Loop"))) {
            if (!b->isEnabled()) return 55;
            b->setToggleState(true, juce::dontSendNotification);
            b->onClick();
        }
    if (!imported->timing.loop || !imported->timing.cycleTicks) return 56;
    auto* clipLane = findControl<EditorClipLane>(main, TRANS("Clips"));
    const auto audioGrid = audioEditor->recordRuler.getBounds();
    if (!clipLane || clipLane->clips.empty()
        || main.getLocalArea(&audioEditor->recordRuler, audioEditor->recordRuler.getLocalBounds()).getX() != clipLane->getX()
        || audioGrid.getWidth() != clipLane->getWidth() || audioEditor->channels.getRight() > audioGrid.getX()) return 132;
    auto* audioGrip = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    auto* upperRuler = audioGrip ? dynamic_cast<ArrangementRuler*>(audioGrip->getParentComponent()->findChildWithID("arrangement-ruler")) : nullptr;
    if (!upperRuler) return 138;
    const auto upperArea = main.getLocalArea(upperRuler, upperRuler->getLocalBounds()).withTrimmedLeft(upperRuler->headerWidth);
    if (upperArea.getX() != clipLane->getX() || upperArea.getRight() != clipLane->getRight()) return 139;
    const auto bars = audioEditor->recordRuler.visibleBars();
    const auto shown = bars.getIntersectionWith(clipLane->clips.front().bars);
    if (shown.isEmpty()) return 133;
    const auto editorPoint = juce::Point<float>(
        float(audioGrid.getX() + ((shown.getStart() + shown.getLength() * .25) - bars.getStart()) * audioGrid.getWidth() / bars.getLength()),
        float(std::max(audioGrid.getBottom() + 2, audioEditor->getHeight() / 2)));
    const auto gesture = [](juce::Component* target, juce::Point<float> from, juce::Point<float> to) {
        const auto time = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), to,
            juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, target, target, time, from, time, 1, from != to);
    };
    audioEditor->mouseDoubleClick(gesture(audioEditor, editorPoint, editorPoint));
    if (imported->timing.markers.size() != 1) return 57;
    auto later = editorPoint.translated(65, 0);
    audioEditor->mouseDown(gesture(audioEditor, editorPoint, editorPoint));
    audioEditor->mouseDrag(gesture(audioEditor, editorPoint, later));
    audioEditor->mouseUp(gesture(audioEditor, editorPoint, later));
    if (imported->timing.markers[0].position <= imported->timing.markers[0].source) return 58;
    auto* grip = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!grip) return 59;
    const auto row = main.getLocalArea(grip, grip->getLocalBounds());
    const auto timeline = main.getLocalArea(&main, main.getLocalBounds());
    // Ask the hit-tested right edge (the amber grip) for its actual pixel, regardless of saved zoom or UI scale.
    const auto snapshot = main.createComponentSnapshot(timeline);
    int edge = -1;
    const int rowY = row.getY() - 6;
    for (int x = row.getX() + 150; x < row.getRight(); ++x)
        if (snapshot.getPixelAt(x, rowY).getARGB() == 0xffffc479) edge = x;
    if (edge < 0) return 60;
    const juce::Point<float> edgePoint{float(edge), float(rowY)};
    const auto extendTo = edgePoint.translated(50, 0);
    const auto length = imported->durationTicks;
    main.mouseDown(gesture(&main, edgePoint, edgePoint));
    main.mouseDrag(gesture(&main, edgePoint, extendTo));
    main.mouseUp(gesture(&main, edgePoint, extendTo));
    if (imported->durationTicks <= length) return 61;
    const auto selected = tracks->getText();
    auto trackActions = model.getMenuForIndex(2, {});
    for (juce::PopupMenu::MenuItemIterator it(trackActions); it.next();)
        if (it.getItem().text == TRANS("Duplicate track") && it.getItem().isEnabled)
            model.menuItemSelected(it.getItem().itemID, 2);
    if (tracks->getNumItems() != 6 || !tracks->getText().startsWith(selected)) return 35;
    auto enableClipFit = [&model] {
        juce::Array<int> toggled;
        for (const auto* caption : {"Chord fit", "Scale fit"}) {
            auto viewMenu = model.getMenuForIndex(1, {});
            for (juce::PopupMenu::MenuItemIterator it(viewMenu, true); it.next();)
                if (it.getItem().text == juce::translate(caption) && !it.getItem().isTicked) {
                    toggled.add(it.getItem().itemID);
                    model.menuItemSelected(it.getItem().itemID, 1);
                    break;
                }
        }
        return toggled;
    };
    const auto audioToggles = enableClipFit();
    auto* firstAudio = findControl<TrackRowSize>(main, selected + " " + TRANS("track height"));
    auto* copiedAudio = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!firstAudio || !copiedAudio || copiedAudio->getY() - firstAudio->getY() != copiedAudio->height()) return 108;
    for (int i = audioToggles.size(); --i >= 0;) model.menuItemSelected(audioToggles[i], 1);
    if (!click("Add bus track") || !click("Add return track")) return 36;
    const auto* busC = findControl<TrackRowSize>(main, "Bus C track height");
    const auto* return2 = findControl<TrackRowSize>(main, "Return 2 track height");
    if (!busC || !busC->isVisible() || !return2 || !return2->isVisible()) return 37;
    auto& desktop = juce::Desktop::getInstance();
    const auto scale = desktop.getGlobalScaleFactor();
    const bool atMaxScale = scale >= 1.5f;
    if (!click(atMaxScale ? "Zoom out interface" : "Zoom in interface") || scale == desktop.getGlobalScaleFactor()
        || !click(atMaxScale ? "Zoom in interface" : "Zoom out interface") || scale != desktop.getGlobalScaleFactor()) return 38;
    main.setSize(1450, 1200);
    {
        std::unique_ptr<OstfpPreview> source;
        juce::FileInputStream input(fixture);
        if (readOstfpPreview(input,source).failed()) return 96;
        auto it = std::find_if(source->pitchedNotes.begin(), source->pitchedNotes.end(), [](const auto& n){ return !n.empty(); });
        if (it == source->pitchedNotes.end()) return 97;
        const int index = int(it - source->pitchedNotes.begin());
        ImportResultRow importedRow("Drop test",*source,index);
        auto* dropRuler = busC->getParentComponent()->findChildWithID("arrangement-ruler");
        if (!dropRuler) return 98;
        const auto pos = main.getLocalPoint(dropRuler,juce::Point<int>(12,4));
        const auto count = tracks->getNumItems();
        // Resolve the receiving component from the arrangement position, just as JUCE
        // does when a row from a separate drag container is released over a child.
        struct Review : juce::Component, juce::DragAndDropContainer {} review;
        juce::Viewport viewport;
        juce::Component rows;
        rows.addAndMakeVisible(importedRow);
        viewport.setViewedComponent(&rows, false);
        review.addAndMakeVisible(viewport);
        if (!importedRow.getViewportIgnoreDragFlag()
            || juce::DragAndDropContainer::findParentDragContainerFor(&importedRow) != &review) return 99;
        juce::DragAndDropTarget::SourceDetails details("ostf-import-result", &importedRow, {});
        juce::DragAndDropTarget* receiver = nullptr;
        for (auto* hit = main.getComponentAt(pos); hit != nullptr; hit = hit->getParentComponent()) {
            auto* target = dynamic_cast<juce::DragAndDropTarget*>(hit);
            if (target && target->isInterestedInDragSource(details)) {
                receiver = target;
                details.localPosition = hit->getLocalPoint(&main, pos);
                break;
            }
        }
        if (!receiver) return 99;
        receiver->itemDragMove(details);
        receiver->itemDropped(details);
        if (tracks->getNumItems() != count+1 || tracks->getText()!=juce::String::fromUTF8(source->trackLanes[index].label)) return 100;
    }
    {
        if (!click("Mixer")) return 251;
        auto* mixer = descendant<MixerPanel>(main);
        if (!mixer || !mixer->isVisible()) return 251;
        auto* listen = findControl<juce::TextButton>(*mixer, TRANS("Listen to Cue on outputs 1-2"));
        auto* cueStatus = findControl<juce::Label>(*mixer, TRANS("Cue output"));
        if (!listen || !cueStatus || listen->getToggleState()) return 254;
        mixer->setActiveOutputCount(2);
        if (cueStatus->getText() != TRANS("Out 3-4 inactive")) return 254;
        listen->setToggleState(true, juce::sendNotificationSync);
        if (cueStatus->getText() != TRANS("Out 1-2")) return 254;
        mixer->setActiveOutputCount(4);
        if (cueStatus->getText() != TRANS("Out 1-2 + 3-4")) return 254;
        listen->setToggleState(false, juce::sendNotificationSync);
        if (cueStatus->getText() != TRANS("Out 3-4")) return 254;
        auto* addBus = findControl<juce::TextButton>(*mixer, TRANS("Add bus track"));
        auto* addReturn = findControl<juce::TextButton>(*mixer, TRANS("Add return track"));
        auto* output = findControl<juce::ComboBox>(*mixer, tracks->getItemText(0) + " output");
        if (!addBus || !addReturn || !output || addBus->getRight() > addReturn->getX()
            || main.getComponentAt(main.getLocalPoint(addBus, addBus->getLocalBounds().getCentre())) != addBus) return 251;
        const auto returns = mixer->returnCount();
        for (auto bus = mixer->routing().busCount; bus < ostfstudio::maxMixerBuses; ++bus) {
            if (!addBus->isEnabled()) return 252;
            addBus->onClick();
            if (mixer->routing().busCount != bus + 1 || mixer->returnCount() != returns
                || !findControl<juce::TextButton>(*mixer, MixerPanel::busName(bus) + " mute")) return 252;
            output->setSelectedId(int(bus) + 2, juce::sendNotificationSync);
            if (output->getText() != TRANS("Out: ") + MixerPanel::busName(bus)
                || mixer->channel(size_t(tracks->getItemId(0) - 1)).outputBus != int(bus)) return 253;
        }
        if (addBus->isEnabled() || !addReturn->isEnabled() || !click("Piano Roll")) return 254;
    }
    if (!main.openDocument(fixture)) return 1;
    {
        auto* divider = dynamic_cast<EditorDivider*>(main.findChildWithID("piano-divider"));
        auto menu = model.getMenuForIndex(1, {});
        int mixerId = 0, pianoId = 0;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
            if (it.getItem().text == TRANS("Mixer")) mixerId = it.getItem().itemID;
            if (it.getItem().text == TRANS("Piano Roll")) pianoId = it.getItem().itemID;
        }
        if (!divider || !mixerId || !pianoId) return 135;
        model.menuItemSelected(mixerId,1);
        if (!divider->isVisible() || !divider->resizeAt) return 136;
        const int y = divider->getY();
        divider->resizeAt(y + (y > main.getHeight()/2 ? -46 : 46));
        if (std::abs(divider->getY() - y) < 20) return 137;
        divider->resizeAt(y);
        model.menuItemSelected(pianoId,1);
    }
    auto* guitar = findControl<TrackRowSize>(main, "Lead Guitar track height");
    auto* bass = findControl<TrackRowSize>(main, "Bass track height");
    auto* master = findControl<TrackRowSize>(main, "Master track height");
    auto* scroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-vertical-scroll"));
    if (!guitar || !bass || !master || !scroll) return 2;
    auto reachable = [&](TrackRowSize& row) {
        return main.getComponentAt(main.getLocalPoint(&row, juce::Point<int>{10, 2})) == &row;
    };
    if (master->height() != TrackRowSize::minimum || bass->height() != 100) return 70;
    auto* tempo = dynamic_cast<TempoEditor*>(main.findChildWithID("tempo-editor"));
    auto* barRuler = guitar->getParentComponent()->findChildWithID("arrangement-ruler");
    auto* topRuler = dynamic_cast<ArrangementRuler*>(barRuler);
    if (!tempo || !topRuler || main.getName().contains(TRANS("Unsaved changes"))) return 75;
    const auto rulerArea = main.getLocalArea(barRuler, barRuler->getLocalBounds());
    for (int y = rulerArea.getBottom() + 2; y < main.getLocalPoint(guitar, juce::Point<int>{0, 0}).y && !tempo->isVisible(); y += 10) {
        const juce::Point<float> header{float(rulerArea.getX() + topRuler->headerWidth - 12), float(y)};
        main.mouseDown(gesture(&main, header, header));
    }
    if (!tempo->isVisible() || tracks->isVisible() || audioEditor->isVisible()) return 76;
    auto* promptPane = findControl<PromptGuide>(main, TRANS("Prompt guide"));
    auto* picker = findControl<juce::ComboBox>(main, TRANS("Analysis model"));
    auto* refresh = findControl<juce::TextButton>(main, TRANS("Refresh models"));
    auto* deck = dynamic_cast<TransportPanel*>(main.findChildWithID("transport"));
    if (!deck || !deck->click.getClickingTogglesState() || deck->countIn.getNumItems() != 9
        || !picker || !refresh || picker->getParentComponent() != &main || refresh->getParentComponent() != &main
        || connection->getY() < menus->getBottom() || connection->getBottom() > deck->getY()
        || connection->getX() < main.getWidth() / 2) return 140;
    const juce::Font heading(juce::FontOptions(15.f, juce::Font::bold));
    const auto reserved = juce::GlyphArrangement::getStringWidth(heading, TRANS("Degree") + " / " + TRANS("Progression"));
    if (!promptPane || !promptPane->isVisible() || promptPane->getWidth() < 100
        || promptPane->getRight() + reserved + 5 > topRuler->headerWidth
        || promptPane->getParentComponent() != topRuler->getParentComponent()) return 141;
    const auto columns = tempo->timelineColumns();
    if (tempo->getX() + columns.getStart() != rulerArea.getX() + topRuler->headerWidth
        || tempo->getX() + columns.getEnd() != rulerArea.getRight()) return 82;
    const juce::Point<float> rampStart{float(columns.getStart() + columns.getLength()/8), 100.f},
                            rampEnd{float(columns.getStart() + columns.getLength()/2), 75.f};
    tempo->mouseDown(gesture(tempo, rampStart, rampStart));
    tempo->mouseDrag(gesture(tempo, rampStart, rampEnd));
    tempo->mouseUp(gesture(tempo, rampStart, rampEnd));
    if (!main.getName().contains(TRANS("Unsaved changes"))) return 77;
    if (!main.keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0))
        || main.getName().contains(TRANS("Unsaved changes"))) return 78;
    if (!reachable(*guitar))
        scroll->setCurrentRangeStart(scroll->getCurrentRangeStart() + guitar->getBottom()
            - guitar->getParentComponent()->getHeight() + 4, juce::sendNotificationSync);
    const auto trackPoint = main.getLocalPoint(guitar, juce::Point<int>{12, -8}).toFloat();
    main.mouseDown(gesture(&main, trackPoint, trackPoint));
    if (tempo->isVisible() || !tracks->isVisible()) return 79;
    if (!reachable(*guitar)) return 3;
    auto& overlay = *guitar->getParentComponent();
    const std::array<juce::Button*, 4> headerButtons{
        findControl<juce::TextButton>(overlay, "Lead Guitar mute"),
        findControl<juce::TextButton>(overlay, "Lead Guitar solo"),
        findControl<juce::TextButton>(overlay, "Lead Guitar record arm"),
        findControl<ViewButton>(overlay, "Lead Guitar Cue")
    };
    for (const int rowHeight : {TrackRowSize::normal, TrackRowSize::minimum, guitar->height()}) {
        guitar->assignHeight(rowHeight);
        guitar->changed();
        int previousRight = 0;
        for (auto* button : headerButtons) {
            if (!button || !button->isVisible() || button->getX() < previousRight
                || main.getComponentAt(main.getLocalPoint(button, button->getLocalBounds().getCentre())) != button)
                return 250;
            previousRight = button->getRight();
        }
    }
    const int bassY = bass->getY();
    const int fitHeight = bassY - guitar->getY() - bass->height();
    if (fitHeight != 0 && fitHeight != 16 && fitHeight != 32) return 72;
    const auto bassPosition = [&](int h) { return bassY + h - 100; };
    guitar->mouseDown(pointer(*guitar, 2));
    guitar->mouseDrag(pointer(*guitar, 82));
    if (!guitar->expanded() || guitar->height() != 100 + 80
        || bass->getY() != bassPosition(180) || bass->height() != 100) return 4;
    // The guitar enters at bar 11, just past the default ten-bar viewport.
    auto* horizontal = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-horizontal-scroll"));
    if (!horizontal || std::abs(horizontal->getCurrentRange().getLength() - 10.0) > 0.1) return 71;
    horizontal->setCurrentRangeStart(11.0, juce::sendNotificationSync);
    const auto image = main.createComponentSnapshot(main.getLocalBounds());
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    bool hasNotePreview = false;
    for (int y = 0; y < image.getHeight() && !hasNotePreview; ++y)
        for (int x = 425; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y) == juce::Colour(0xffb3ebdc)) { hasNotePreview = true; break; }
    if (!hasNotePreview) return 5;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (!guitar->compact() || bass->getY() != bassPosition(TrackRowSize::minimum)) return 6;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (guitar->height() != 100 + 80) return 7;
    main.setSize(790, 570);
    if (!scroll->isVisible()) return 8;
    scroll->setCurrentRangeStart(scroll->getMaximumRangeLimit(), juce::sendNotificationSync);
    auto* lastReturn = findControl<TrackRowSize>(main, "Return 1 track height");
    auto* ruler = master->getParentComponent()->findChildWithID("arrangement-ruler");
    if (promptPane->isVisible()) return 141;
    if (!lastReturn || !reachable(*lastReturn) || reachable(*guitar) || !ruler || ruler->getY() != 0) return 9;
    // A short viewport can fit fewer than four output strips beneath the pinned ruler.
    if (master->getY() < ruler->getBottom())
        scroll->setCurrentRangeStart(scroll->getCurrentRangeStart() + master->getY() - ruler->getBottom(), juce::sendNotificationSync);
    if (!reachable(*master)) return 43;
    master->mouseDown(pointer(*master, 2));
    master->mouseDrag(pointer(*master, 55));
    if (!master->expanded() || !guitar->expanded()) return 10;

    auto* busA = findControl<TrackRowSize>(main, "Bus A track height");
    auto* busB = findControl<TrackRowSize>(main, "Bus B track height");
    auto* drums = findControl<TrackRowSize>(main, "Drums track height");
    if (!busA || !busB || !drums || !choose(TRANS("Output tracks"))) return 19;
    if (height->getTextFromValue(height->getValue()) != TRANS("Mixed")) return 20;
    height->setValue(94, juce::sendNotificationSync);
    if (master->height() != 94 || busA->height() != 94 || busB->height() != 94 || !guitar->expanded()) return 21;
    if (!choose(TRANS("Buses"))) return 22;
    height->setValue(38, juce::sendNotificationSync);
    if (master->height() != 94 || busA->height() != 38 || busB->height() != 38) return 23;
    if (!choose(TRANS("Drum tracks"))) return 24;
    height->setValue(79, juce::sendNotificationSync);
    if (drums->height() != 79 || bass->height() != 100) return 25;
    if (!choose(TRANS("Timeline lanes"))) return 26;
    scroll->setCurrentRangeStart(0, juce::sendNotificationSync);
    const int beforeLanes = bass->getY();
    height->setValue(62, juce::sendNotificationSync);
    if (bass->getY() <= beforeLanes || bass->height() != 100) return 27;
    if (ruler->getHeight() != 62 || group->getLookAndFeel().getPopupMenuFont().getHeight() < 15.f) return 62;
    auto* basics = dynamic_cast<SongBasicsPanel*>(main.findChildWithID("song-basics-strip"));
    if (!basics || !basics->isVisible() || !basics->advanced || !basics->commit ||
        basics->getHeight() != SongBasicsPanel::stripHeight || basics->getBottom() > scroll->getY()) return 63;
    basics->commit(SongBasicsPanel::title, "Edited title", 0);
    if (!main.getName().contains("Edited title") || !main.getName().contains(TRANS("Unsaved changes"))) return 65;
    basics->commit(SongBasicsPanel::key, {}, 10 + 8 * 3 + 1);
    auto* keys = dynamic_cast<juce::ComboBox*>(basics->findChildWithID("song-base-key"));
    if (!keys || keys->getText() != "Eb minor") return 66;
    if (!choose(TRANS("All tracks"))) return 28;
    height->setValue(35, juce::sendNotificationSync);
    if (!guitar->compact() || !drums->compact() || !master->compact() || !bass->compact()) return 29;

    // Fit badges must not add space between neighbouring pitched tracks.
    const auto toggled = enableClipFit();
    if (!choose(TRANS("Arrangement tracks"))) return 73;
    for (int h : {100, 155, 42}) {
        height->setValue(h, juce::sendNotificationSync);
        if (guitar->height() != h || bass->height() != h || bass->getY() - guitar->getY() != h) return 74;
    }
    for (int i = toggled.size(); --i >= 0;) model.menuItemSelected(toggled[i], 1);

    // Paint independent L/R peaks and confirm silence and overload colours in both orientations.
    for (bool vertical : {false, true}) {
        LevelMeter meter(vertical);
        meter.setSize(vertical ? 16 : 120, vertical ? 120 : 16);
        meter.update({1.1f, 0.f}, 1.0);
        const auto lit = meter.createComponentSnapshot(meter.getLocalBounds());
        if (lit.getPixelAt(vertical ? 4 : 118, vertical ? 1 : 4) != juce::Colour(0xffff5654)
            || lit.getPixelAt(vertical ? 12 : 60, vertical ? 60 : 12) != juce::Colour(0xff152026)) return 11;
        meter.update({}, 5.0);
        const auto silent = meter.createComponentSnapshot(meter.getLocalBounds());
        if (silent.getPixelAt(vertical ? 4 : 60, vertical ? 60 : 4) != juce::Colour(0xff152026)) return 12;
    }
    if (!main.keyPressed(juce::KeyPress(juce::KeyPress::F1Key))) return 40;
    auto* guide = dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());
    if (!guide || guide->getName() != TRANS("Shortcuts") || !guide->getContentComponent()) return 41;
    bool listsPlayback = false, listsRuler = false;
    for (auto* child : guide->getContentComponent()->getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            listsPlayback |= label->getText() == TRANS("Play / stop");
            listsRuler |= label->getText() == TRANS("Click the bar ruler");
        }
    guide->exitModalState(0);
    if (!listsPlayback || !listsRuler) return 42;
    // A dropped AIFF must become a self-contained WAV asset with the same samples.
    juce::TemporaryFile aiffFile(".aiff");
    std::unique_ptr<juce::OutputStream> stream = aiffFile.getFile().createOutputStream();
    juce::AiffAudioFormat aiff;
    auto writer = aiff.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(44100)
        .withNumChannels(1).withBitsPerSample(16));
    juce::AudioBuffer<float> source(1,128);
    source.clear();
    source.setSample(0,32,.5f);
    if (!writer || !writer->writeFromAudioSampleBuffer(source,0,128)) return 145;
    writer.reset();
    if (!click("Add audio track")) return 146;
    const auto position = audioEditor->getBounds().getCentre();
    main.filesDropped({aiffFile.getFile().getFullPathName()},position.x,position.y);
    const auto* decoded = audioEditor->sourceClip();
    if (!decoded || decoded->embedded.isEmpty() || !decoded->path.endsWith(".wav")) return 147;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::MemoryInputStream(decoded->embedded,false),true));
    if (!reader || reader->sampleRate != 44100 || reader->lengthInSamples != 128 || reader->numChannels != 1
        || !reader->read(&source,0,128,0,true,false) || std::abs(source.getSample(0,32)-.5f) > .0001f) return 148;
    return 0;
}
