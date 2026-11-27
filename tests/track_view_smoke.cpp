#include "ui/MainComponent.h"
#include "ui/GlobalTimelineEdit.h"
#include "ui/PluginRoles.h"
#include "plugins/scale/ScoreExchange.h"
#include "ui/SongMidi.h"
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
            // The demo also has FM, drum and wavetable instruments on independent tracks.
            if (matchesVst3Instrument(mix.instruments[i], instrument->getPluginDescription()) != (module != nullptr)) return 213;
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
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    while (!juce::Component::getCurrentlyModalComponent() && juce::Time::getMillisecondCounterHiRes() < deadline) {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep(10);
    }
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
    studio.refreshMixerPlugins();
    const auto memberButton = juce::String::fromUTF8(studio.project_->trackLanes[5].label) + " instrument editor";
    auto* sharedEditorButton = findControl<juce::TextButton>(studio.mixer_, memberButton);
    if (!sharedEditorButton || !sharedEditorButton->getTooltip().contains("#2 Ch 5") || !sharedEditorButton->isEnabled()) return 225;
    // Plug-in count and missing slots must not move the source fader or open a missing insert.
    {
        MixerPanel rack;
        rack.setSize(1500,620);
        rack.setTracks(*studio.project_);
        const auto track = juce::String::fromUTF8(studio.project_->trackLanes[0].label);
        auto* fx = findControl<juce::TextButton>(rack,track + " effect editor");
        auto* arrow = findControl<juce::TextButton>(rack,track + " effect selection");
        auto* pan = findControl<juce::Slider>(rack,track + " mixer pan");
        if (!fx || !arrow || !pan || fx->isEnabled()) return 228;
        const auto bounds = pan->getBounds();
        int opened = -1;
        rack.openPlugin = [&](bool output,size_t index,int slot) { if (!output && index == 0) opened = slot; };
        rack.setStripPlugins(false,0,2,{{"Absent",0,false},{"First",1,true},{"Second",2,true}});
        if (!fx->isEnabled() || !arrow->isEnabled() || fx->getTooltip() != "First") return 229;
        fx->onClick();
        if (opened != 1 || pan->getBounds() != bounds) return 230;
        rack.setStripPlugins(false,0,2,{{"Second",0,true}});
        fx->onClick();
        if (opened != 0 || fx->getTooltip() != "Second") return 231;
    }
    sharedEditorButton->onClick();
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
        studio.preferredHeaderWidth_ = 487;
        studio.setSize(1500,1000);
        studio.openSongFile(song, [this](bool ok) {
            opened = ok && !studio.loadingOverlay_.isVisible();
            startTimer(50);
        });
        sawLoading = studio.loadingOverlay_.isVisible() && studio.loadingOverlay_.getName() == song.getFileName();
        const auto preview = juce::SystemStats::getEnvironmentVariable("OSTF_LOADING_PNG", {});
        if (preview.isNotEmpty()) {
            juce::FileOutputStream out{juce::File(preview)};
            if (out.openedOk() && out.setPosition(0) && out.truncate().wasOk())
                juce::PNGImageFormat().writeImageToStream(studio.createComponentSnapshot(
                    studio.getLocalBounds().withSizeKeepingCentre(600, 210)), out);
        }
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
        for (const auto track : {lead,backing}) {
            for (const auto& lane : project.partLyricLanes.at(track)) {
                if (lane.kind != ostfstudio::TimelineLane::displayLyrics) continue;
                for (const auto& cell : lane.regions) {
                    const auto start = std::llround((cell.exactBars->start-1)*project.ticksPerBar);
                    const auto end = std::llround((cell.exactBars->end-1)*project.ticksPerBar);
                    for (auto tick : {start,end-1})
                        if (juce::String::fromUTF8(project.displayLyricAt(track,tick)) != juce::String::fromUTF8(cell.label)) return 24;
                }
            }
        }
        if (!studio.mixer_.channel(*project.chordVoicingTrack).muted
            || studio.trackControls_[*project.chordVoicingTrack]->size.height() != 100) return 26;
        const auto& backingStrip = *studio.trackControls_[backing];
        if (backingStrip.cue.getY() != backingStrip.mute.getY()
            || backingStrip.cue.getBottom() != backingStrip.arm.getBottom()) return 27;
        if (!backingStrip.size.expanded() || backingStrip.size.height() != 100 || studio.trackControls_[lead]->size.height() != 100
            || !backingStrip.volume.isVisible() || !backingStrip.pan.isVisible()
            || project.trackLanes[backing].regions.empty()) return 21;
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
        if (std::none_of(studio.vocalLane_.phrases.begin(), studio.vocalLane_.phrases.end(), [](const auto& phrase) {
                return phrase.text == juce::String::fromUTF8("雨の音が") && phrase.start > 0; }))
            return 14;
        {
            const auto before = project.sourceScore;
            auto score = juce::JSON::parse(before.toString());
            const auto chord = score["harmony"]["chords"][0];
            auto scale = juce::JSON::parse(R"({"id":"scale-return-smoke","start":0,"duration":1,"tonic":{"step":"C","alter":0},"intervals":[0,2,4,5,7,9,11],"targets":[{"type":"chord","id":"first"}]})");
            scale.getDynamicObject()->setProperty("start",chord["start"]);
            scale.getDynamicObject()->setProperty("duration",chord["duration"]);
            scale["targets"][0].getDynamicObject()->setProperty("id",chord["id"]);
            auto scales = score["harmony"]["scales"];
            scales.getArray()->add(scale);
            auto track = score["tracks"][0];
            auto direction = juce::JSON::parse(R"({"id":"direction-return-smoke","start":0,"duration":960,"type":"improvisation","parameters":{"pitchSource":{"type":"explicitScale","scale":{"tonic":{"step":"D","alter":0},"intervals":[0,2,3,5,7,9,10],"label":"D Dorian"}},"adherence":"preferred"}})");
            auto directions = track.getProperty("directions",juce::Array<juce::var>{});
            directions.getArray()->add(direction);
            track.getDynamicObject()->setProperty("directions",directions);
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
            const auto file = dir.getChildFile("studio-scales-"+juce::SHA256(before).toHexString()+"-"+juce::Uuid().toString()+".ostf");
            juce::ScopeGuard cleanup([&]{ file.deleteFile(); });
            if (!file.replaceWithText(juce::JSON::toString(score),false,false,nullptr)) return 33;
            auto drop = [&]{studio.filesDropped({file.getFullPathName()},studio.rowOverlay_.getX()+studio.trackHeaderWidth+3,studio.rowOverlay_.getY()+4);};
            drop();
            const auto applied = project.sourceScore;
            const auto loaded = juce::JSON::parse(applied.toString());
            if (loaded["harmony"]["scales"].size()!=scales.size() || loaded["tracks"].size()!=score["tracks"].size()
                || juce::JSON::toString(loaded["tracks"][0]["directions"])!=juce::JSON::toString(directions)) return 34;
            drop();
            if (project.sourceScore!=applied) return 35;
            studio.undoPianoEdit(false);
            if (project.sourceScore!=before) return 36;
            const auto generation=studio.projectGeneration_;
            const auto revision=juce::SHA256(before).toHexString();
            const auto proposal=juce::JSON::toString(score);
            auto assign=[&](std::size_t owner) { return studio.applyScaleEdit(owner,revision.toRawUTF8(),
                proposal.toRawUTF8(),int(proposal.getNumBytesAsUTF8())); };
            if (assign(generation+1) || project.sourceScore!=before || !assign(generation)) return 131;
            const auto committed=project.sourceScore;
            if (assign(generation) || project.sourceScore!=committed) return 132;
            const auto syncScore=juce::JSON::parse(committed.toString());
            if (syncScore["harmony"]["scales"].size()!=scales.size()
                || juce::JSON::toString(syncScore["tracks"][0]["directions"])!=juce::JSON::toString(directions)) return 133;
            studio.undoPianoEdit(false);
            if (project.sourceScore!=before) return 134;
            const auto copy=ostfstudio::scale::clipboardReturnPrefix+juce::SHA256(before).toHexString()+"\r\n"+juce::JSON::toString(score);
            studio.pasteScaleChanges("unrelated clipboard text");
            if (project.sourceScore!=before) return 95;
            studio.pasteScaleChanges(copy);
            const auto newScore=project.sourceScore;
            const auto copied=juce::JSON::parse(newScore.toString());
            if (copied["harmony"]["scales"].size()!=scales.size()
                || juce::JSON::toString(copied["tracks"][0]["directions"])!=juce::JSON::toString(directions)) return 96;
            studio.pasteScaleChanges(copy);
            if (project.sourceScore!=newScore) return 97;
            studio.undoPianoEdit(false);
            if (project.sourceScore!=before) return 98;
        }
        {
            const auto before = project.sourceScore;
            const auto routed = project.trackIds[lead];
            const auto previous = studio.midiRoutes_[routed];
            auto& route = studio.midiRoutes_[routed];
            route.enabled = true; route.port = "test-midi"; route.channel = 2;
            studio.mixerButton_.onClick();
            studio.setRecordArm(lead,true);
            if (!studio.mixer_.isVisible()) return 45;
            const auto now = juce::Time::getMillisecondCounterHiRes()*.001;
            {
                const juce::SpinLock::ScopedLockType lock(studio.surfaceLock_);
                const auto on = juce::MidiMessage::noteOn(2,69,juce::uint8(88));
                studio.rememberMidiInput("wrong-port",on,now-.9);
                studio.rememberMidiInput("test-midi",juce::MidiMessage::noteOn(1,60,juce::uint8(77)),now-.8);
                if (studio.midiPerformance_.hasNotes()) return 40;
                studio.rememberMidiInput("test-midi",on,now-.5);
                studio.rememberMidiInput("test-midi",juce::MidiMessage::noteOff(2,69),now-.25);
            }
            studio.showPadInput();
            auto* capture = findControl<juce::TextButton>(studio,studio.trackControls_[lead]->arm.getTitle().upToLastOccurrenceOf(" record arm",false,false)+" capture MIDI");
            if (!capture || !capture->isEnabled()) return 41;
            const auto count = project.pitchedNotes[lead].size();
            capture->onClick();
            if (capture->isEnabled() || project.pitchedNotes[lead].size()!=count+1) return 42;
            if (std::none_of(project.pitchedNotes[lead].begin(),project.pitchedNotes[lead].end(),[&](const auto& n) {
                return n.pitch==69 && n.startTick==studio.cursorTick_ && n.velocity==88 && n.endTick>n.startTick;
            })) return 43;
            studio.undoPianoEdit(false);
            if (project.sourceScore!=before) return 44;
            studio.midiRoutes_[routed]=previous;
            studio.setRecordArm(lead,false);
            if (!studio.mixer_.isVisible()) return 45;
            studio.mixerButton_.onClick();
        }
        const auto pre = project.vocalNotes.find("vocal-part-3-line-1-note-1");
        if (pre == project.vocalNotes.end() || pre->second.pronunciation != juce::String::fromUTF8("イフ")) return 32;
        studio.vocalLane_.entrance.onClick();
        if (studio.cursorTick_ != project.pitchedNotes[lead].front().startTick || studio.cursorTick_/project.ticksPerBar != 4) return 5;
        const auto* slots=studio.trackControls_[lead]->sends.getViewedComponent();
        if (!slots || slots->getNumChildComponents()!=int(ostfstudio::maxMixerReturns)) return 28;
        for(int i=0;i<slots->getNumChildComponents();++i)
            if(slots->getChildComponent(i)->isEnabled() != (size_t(i)<studio.mixer_.returnCount())) return 28;
        const auto previewDir=juce::SystemStats::getEnvironmentVariable("OSTF_TRACK_HEADER_PREVIEWS",{});
        if(previewDir.isNotEmpty()) {
            const auto audio=std::find(project.trackKinds.begin(),project.trackKinds.end(),ostfstudio::TrackKind::audio);
            if(audio==project.trackKinds.end()) return 38;
            const size_t track=size_t(audio-project.trackKinds.begin());
            const auto originalScroll=studio.verticalScroll_.getCurrentRangeStart();
            const auto originalWidth=studio.preferredHeaderWidth_;
            for(int width:{400,760}) {
                studio.preferredHeaderWidth_=width; studio.resized();
                studio.verticalScroll_.setCurrentRangeStart(studio.verticalScroll_.getCurrentRangeStart()
                    +studio.trackRows_[track].getY()-studio.rowOverlay_.getY()-4,juce::sendNotificationSync);
                studio.resized();
                auto area=studio.trackRows_[track].withWidth(studio.trackHeaderWidth).getIntersection(studio.rowOverlay_.getBounds());
                juce::FileOutputStream out(juce::File(previewDir).getChildFile("track-sends-"+juce::String(width)+".png"));
                if(area.isEmpty() || !out.openedOk() || !out.setPosition(0) || out.truncate().failed()
                    || !juce::PNGImageFormat().writeImageToStream(studio.createComponentSnapshot(area),out)) return 38;
            }
            studio.preferredHeaderWidth_=originalWidth;
            studio.verticalScroll_.setCurrentRangeStart(originalScroll,juce::sendNotificationSync); studio.resized();
        }
        while (studio.mixer_.returnCount() < ostfstudio::maxMixerReturns) studio.mixer_.addReturn();
        studio.refreshTrackSends();
        studio.resized();
        const auto& sends = studio.trackControls_[lead]->sends;
        if (sends.getViewedComponent()->getWidth() > sends.getWidth() || sends.getViewedComponent()->getHeight() > sends.getHeight()
            || sends.getViewedComponent()->getNumChildComponents() != int(ostfstudio::maxMixerReturns)) return 28;
        const auto previousWidth=studio.preferredHeaderWidth_;
        for(int width : {400,487,760}) {
            studio.preferredHeaderWidth_=width;
            studio.resized();
            const auto& strip=*studio.trackControls_[lead];
            const auto* row=strip.sends.getViewedComponent();
            if(row->getWidth()>strip.sends.getWidth() || strip.sends.getY()<strip.input.getBottom()
                || strip.pan.getBounds().intersects(strip.input.getBounds())) return 37;
            for(int k=0;k<row->getNumChildComponents();++k) {
                const auto* knob=row->getChildComponent(k);
                if(!knob->isEnabled() || !row->getLocalBounds().contains(knob->getBounds())
                    || (k && knob->getBounds().intersects(row->getChildComponent(k-1)->getBounds()))) return 37;
            }
        }
        studio.preferredHeaderWidth_=previousWidth;
        studio.resized();
        const auto guideImage = juce::SystemStats::getEnvironmentVariable("OSTF_PROMPT_LCD_PNG", {});
        if (guideImage.isNotEmpty()) {
            juce::FileOutputStream out{juce::File(guideImage)};
            if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed()
                || !juce::PNGImageFormat().writeImageToStream(studio.promptGuide_.createComponentSnapshot(
                    studio.promptGuide_.getLocalBounds()), out)) return 30;
        }
        const auto headerImage = juce::SystemStats::getEnvironmentVariable("OSTF_LYRIC_HEADER_PNG",{});
        if (headerImage.isNotEmpty()) {
            const auto previousScroll = studio.verticalScroll_.getCurrentRangeStart();
            studio.verticalScroll_.setCurrentRangeStart(previousScroll + studio.trackGroupBounds(lead).getY()
                - studio.rowOverlay_.getY() - studio.timelineRowHeight(),juce::sendNotificationSync);
            studio.resized();
            auto crop = studio.trackGroupBounds(lead).withWidth(studio.trackHeaderWidth + 380);
            crop = crop.getIntersection(studio.rowOverlay_.getBounds());
            juce::FileOutputStream stream{juce::File(headerImage)};
            if (crop.isEmpty() || !stream.openedOk() || !stream.setPosition(0) || stream.truncate().failed()
                || !juce::PNGImageFormat().writeImageToStream(studio.createComponentSnapshot(crop),stream)) return 25;
            studio.verticalScroll_.setCurrentRangeStart(previousScroll,juce::sendNotificationSync);
            studio.resized();
        }
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
        PreviewTransport::Inserts inserts{};
        if (juce::SystemStats::getEnvironmentVariable("OSTF_VOCAL_INSERTS",{})=="1") {
            for (size_t i=0;i<project.trackLanes.size();++i)
                playback.setChannelMix(i,studio.mixer_.channel(i),i==size_t(lead),0);
            playback.setOutputMix(studio.mixer_.routing());
            const auto copyChain = [&](const auto& chain,size_t index) {
                for (size_t i=0;i<chain.slots.size();++i) inserts[index][i]=chain.slots[i].effect.get();
            };
            copyChain(studio.trackControls_[lead]->effects,PreviewTransport::outputCount+size_t(lead));
            for (size_t i=0;i<studio.outputControls_.size();++i) copyChain(studio.outputControls_[i].effects,i);
        }
        if (playback.preparePlayback(project,routes,inserts,rate,block).failed()) return 9;
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
        return meters.tracks[lead][0] > .001f && meters.tracks[backing][0] > .001f
            && meters.vu[backing][0] > .001f && meters.master.vu[0] > .001f && meters.master.vu[1] > .001f
            && std::any_of(meters.master.scope.begin(), meters.master.scope.end(), [](auto point) {
                return point[0] != 0 || point[1] != 0;
            }) ? 0 : 13;
    }
    void timerCallback() override {
        if (dropped) {
            auto& chain = studio.effectChain(true,0);
            if (chain.loading && ++polls < 600) return;
            stopTimer();
            if (chain.slots.empty() || !chain.slots.back().effect || chain.slots.back().effect->getName()!="EQ"
                || !studio.mixer_.isVisible()) { finish(47); return; }
            checkMissingSong();
            return;
        }
        if (opened && !studio.canEditTracks() && ++polls < 600) return;
        stopTimer();
        const int result = !sawLoading || !opened ? 1 : !studio.canEditTracks() ? 2 : verify();
        if (result) {
            std::cerr << "Vocal track check " << result << '\n';
            finish(result);
            return;
        }
        studio.mixerButton_.onClick();
        auto& browser = studio.ensurePluginBrowser();
        const auto& types = browser.types();
        const auto eq = std::find_if(types.begin(),types.end(),[](const auto& p) {
            return p.manufacturerName=="OSTF Studio" && p.name=="EQ";
        });
        auto* outputButton = findControl<juce::TextButton>(studio.mixer_, "Master hardware output");
        if (eq==types.end() || !outputButton) { finish(46); return; }
        auto& chain = studio.effectChain(true,0);
        const auto initial = chain.slots.size();
        const auto point = studio.getLocalPoint(outputButton,outputButton->getLocalBounds().getCentre());
        juce::DragAndDropTarget::SourceDetails drag("ostf-vst3:"+eq->createIdentifierString(),&browser,point);
        studio.itemDragMove(drag);
        studio.itemDropped(drag);
        if (chain.slots.size()!=initial+1) { finish(46); return; }
        dropped=true; polls=0; startTimer(50);
    }
    void checkMissingSong() {
        const auto* original = studio.project_.get();
        const auto missing = juce::File::createTempFile(".ostfp");
        studio.openSongFile(missing, [this, original](bool ok) {
            finish(!ok && studio.project_.get() == original && !studio.loadingOverlay_.isVisible() ? 0 : 29);
        });
    }
    MainComponent studio;
    juce::File output;
    std::function<void(int)> finish;
    bool opened = false, sawLoading = false, dropped = false;
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
#if JUCE_MAC
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            studio.setBounds(studio.getBounds().constrainedWithin(display->userArea));
#endif
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
#if ! JUCE_MAC
            editor->setTopLeftPosition(-9500, -9500);
#endif
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
#elif JUCE_MAC
        if (!editor->getPeer() || editor->getAssociatedComponent() != &studio
            || !editor->isShowing() || editor->isCurrentlyBlockedByAnotherModalComponent()) return false;
        if (editor->getPeer()->isFocused()) return true;
        const bool active = juce::Process::isForegroundProcess();
        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i) {
            auto* peer = juce::ComponentPeer::getPeer(i);
            if (peer->isFocused()) {
                std::cerr << "Unexpected key window: " << peer->getComponent().getName() << '\n';
                return false;
            }
        }
        if (active) std::cerr << "Studio is active with no key window\n";
        return !active;
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
        if (file.getFileNameWithoutExtension() == "OSTF Scale Editor") {
            stopTimer();
            juce::OwnedArray<juce::PluginDescription> types;
            studio.pluginFormats_.getFormat(0)->findAllTypesForFile(types,file.getFullPathName());
            studio.editTracks(ostfstudio::TrackKind::pitched);
            if (types.size()!=1 || studio.trackControls_.empty()) {finished(20); return;}
            auto& track=*studio.trackControls_.front();
            juce::String error;
            track.instance=studio.pluginFormats_.createPluginInstance(*types[0],48000,256,error);
            if (!track.instance) {finished(21); return;}
            studio.openTrackPluginEditor(0);
            auto* window=track.editor.get();
            if (!window || !window->isResizable()) {finished(22); return;}
            for (int width : {825,1760}) {
                window->setBoundsConstrained(window->getBounds().withSizeKeepingCentre(width,700));
                auto* content=window->getContentComponent();
                if (!content || std::abs(content->getWidth()-width)>32) {finished(23); return;}
            }
            finished(0); return;
        }
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
                const auto menu = menus->getMenuForIndex(6, {});
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
        overlap.pitchedNotes[0].push_back({0,160,61});
        const auto melodyFit = ChordAnalysis(overlap, 0).review({{0,480}});
        const auto keysFit = ChordAnalysis(overlap, 2).review({{0,480}});
        const auto silentFit = ChordAnalysis(overlap, 3).review({{0,480}});
        if (melodyFit.size() != 1 || keysFit.size() != 1 || silentFit.size() != 1
            || melodyFit[0].candidates != keysFit[0].candidates || melodyFit[0].candidates[0] != "C"
            || melodyFit[0].trackFit[1] != 75 || keysFit[0].trackFit[1] != 100
            || silentFit[0].trackFit[1] != -1) return 241;
        overlap.pitchedNotes[0].pop_back();
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
        if (main.systemUsage_.getBounds().isEmpty() || main.historyButton_.getY() != main.undoButton_.getY()
            || main.statusLabel_.getRight() > main.systemUsage_.getX()) return 244;
       #if JUCE_WINDOWS || JUCE_MAC
        if (!main.systemUsage_.getTooltip().contains("GiB")) return 245;
       #endif
        if (main.pianoGrid_.getHeight() < 10 || main.chordReview_.getHeight() < 36
            || decision->getHeight() < 16 || main.chordReview_.getBottom() > main.pianoGrid_.getY()) return 239;
        main.setSize(1650, 1400);
        if (!choose("Keep", true)) return 223;
        main.selectTrack(0);
        if (main.selectedTrack_ != 0 || !main.pendingReview_.empty()) return 203;
        main.selectTrack(tracks);
        if (main.chordReview_.isVisible() || main.chordReviewHeight() != 0 || main.project_->sourceScore != imported
            || !main.undoButton_.isEnabled() || !main.historyButton_.isEnabled()) return 224;
        auto history = main.editHistoryMenu();
        bool restoredPending = false;
        size_t state = 0;
        for (juce::PopupMenu::MenuItemIterator it(history); it.next();) {
            if (it.getItem().action && state++ == main.noteHistoryCursor_ - 1) {
                it.getItem().action();
                restoredPending = main.chordReview_.isVisible() && main.pendingReview_.size() == pendingCount;
                break;
            }
        }
        if (!restoredPending || !main.redoButton_.isEnabled()) return 242;
        main.redoButton_.onClick();
        if (main.chordReview_.isVisible() || !main.pendingReview_.empty() || main.project_->sourceScore != imported) return 243;
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
    {
        juce::MemoryInputStream input(BinaryData::neonplatforms_ostfp,BinaryData::neonplatforms_ostfpSize,false);
        std::unique_ptr<ostfstudio::OstfpPreview> song;
        if (ostfstudio::readOstfpPreview(input,song).failed()) return 336;
        song->savedMix.reset();
        main.openImportedSong(std::move(song));
        main.setVisible(true);
        using ostfstudio::TimelineLane;
        if (!main.splitTool_.getToggleState()) main.splitTool_.onClick();
        for (auto lane : {TimelineLane::section,TimelineLane::chord,TimelineLane::scale,TimelineLane::tempo,TimelineLane::key,TimelineLane::meter}) {
            const int cutX=main.rowOverlay_.getX()+main.trackHeaderWidth+24;
            int y=main.rowOverlay_.getY();
            while (y<main.rowOverlay_.getBottom() && main.globalLaneAt({cutX,y})!=lane) ++y;
            if(y==main.rowOverlay_.getBottom()) return 337;
            auto* hit=main.getComponentAt(cutX,y);
            if(hit!=&main) {std::cerr<<"Global click target "<<(hit?typeid(*hit).name():"outside")<<" at "<<cutX<<","<<y<<" lane "<<int(lane)<<"\n"; return 338;}
            const auto original=main.project_->sourceScore;
            main.mouseDown(drag({cutX,y},false));
            if(original==main.project_->sourceScore) {std::cerr<<"Scissors: "<<main.statusLabel_.getText()<<"\n"; return 339;}
            main.undoPianoEdit(false);
        }
        main.setWorkspaceMode(MainComponent::WorkspaceMode::live);
        const auto rows=main.live_.sections.size();
        main.liveSectionAction(true);
        if(main.live_.sections.size()!=rows+1 || main.live_.selected!=int(rows)) return 340;
        main.undoPianoEdit(false);
        if(main.live_.sections.size()!=rows) return 341;
        const auto beforeSplit=main.pianoDivider_.getY();
        main.pianoDivider_.resizeAt(beforeSplit+50);
        if(main.pianoDivider_.getY()-beforeSplit<30) return 342;
        main.pianoDivider_.resizeAt(beforeSplit);
        const bool muted=main.mixer_.channel(0).muted;
        main.liveMatrix_.trackAction(0,4,nullptr);
        if(main.mixer_.channel(0).muted==muted) return 343;
        main.liveMatrix_.trackAction(0,4,nullptr);
    }
    {
        MainComponent stems;
        stems.setSize(1650, 1400);
        stems.newProject();
        for (int track=0; track<3; ++track) stems.editTracks(ostfstudio::TrackKind::audio);
        const int origin=stems.rowOverlay_.getX()+stems.trackHeaderWidth+40;
        for (int track=0; track<3; ++track)
            stems.filesDropped({file.getFullPathName()},origin+(track==2 ? 120 : 0),stems.trackRows_[track].getCentreY());
        if (!stems.clipOn(0) || !stems.clipOn(1) || !stems.clipOn(2)
            || stems.clipOn(0)->startTick==stems.clipOn(2)->startTick) return 370;
        stems.selectTrack(1);
        auto pitched=stems.clipOn(1)->timing;
        pitched.pitches.push_back({.1,.3,2});
        stems.audioEditor_.onEdit(pitched);
        stems.selectTrack(0);
        for (auto* child:stems.audioEditor_.getChildren())
            if (auto* button=dynamic_cast<juce::Button*>(child); button && button->getButtonText()==TRANS("Link aligned stems"))
                button->setToggleState(true,juce::dontSendNotification);
        if (!stems.audioEditor_.linkWarp()) return 371;
        auto warp=stems.clipOn(0)->timing;
        warp.cycleTicks=stems.clipOn(0)->durationTicks;
        warp.preservePitch=true;
        warp.markers.push_back({.65,.5});
        stems.audioEditor_.onEdit(warp);
        if (stems.clipOn(0)->timing!=warp || stems.clipOn(1)->timing.markers!=warp.markers
            || stems.clipOn(1)->timing.pitches!=pitched.pitches || stems.clipOn(2)->timing.cycleTicks!=0) return 372;
        stems.undoPianoEdit(false);
        if (stems.clipOn(0)->timing.cycleTicks!=0 || stems.clipOn(1)->timing!=pitched) return 373;
    }
    {
        MainComponent view;
        view.setSize(1650, 1000);
        std::unique_ptr<ostfstudio::OstfpPreview> song;
        const auto demo=file.getParentDirectory().getParentDirectory().getChildFile("blue-hour-waltz.ostfp");
        juce::FileInputStream input(demo);
        if (!input.openedOk() || ostfstudio::readOstfpPreview(input,song).failed()) return 374;
        song->savedMix.reset();
        view.openImportedSong(std::move(song));
        view.setVisible(true);
        const auto area=view.rowOverlay_.getBounds();
        const int x=area.getX()+view.trackHeaderWidth+20;
        int y=area.getY();
        while (y<area.getBottom() && view.globalLaneAt({x,y})!=ostfstudio::TimelineLane::scale) ++y;
        if (y==area.getBottom()) return 375;
        const auto png=juce::SystemStats::getEnvironmentVariable("OSTF_GLOBAL_SCALE_PNG",{});
        if (png.isNotEmpty()) {
            const auto crop=area.withWidth(std::min(area.getWidth(),view.trackHeaderWidth+520))
                .withBottom(std::min(area.getBottom(),y+view.timelineRowHeight()*3));
            juce::FileOutputStream output{juce::File(png)};
            if (!output.openedOk() || !output.setPosition(0) || output.truncate().failed()
                || !juce::PNGImageFormat{}.writeImageToStream(view.createComponentSnapshot(crop),output)) return 376;
        }
        view.selectedGlobal_=ostfstudio::TimelineLane::scale;
        view.selectedGlobalTick_=view.project_->ticksPerBar;
        view.globalCommand(1);
        auto* dialog=dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
        if (!dialog || !dialog->getComboBoxComponent("scale-mode")) return 377;
        dialog->getComboBoxComponent("scale-mode")->setSelectedId(2,juce::sendNotificationSync);
        const bool matches=dialog->getTextEditorContents("intervals")=="0 2 3 5 7 9 10"
            && dialog->getTextEditorContents("value")=="Dorian";
        dialog->exitModalState(0);
        if (!matches) return 378;
    }
    return 0;
}

int checkLiveSession(const juce::File& fixture) {
    std::unique_ptr<ostfstudio::OstfpPreview> score;
    juce::FileInputStream input(fixture);
    if (!input.openedOk() || ostfstudio::readOstfPreview(input,score).failed()) return 1;
    MainComponent main;
    main.setSize(1536,970);
    main.openImportedSong(std::move(score));
    main.setWorkspaceMode(MainComponent::WorkspaceMode::live);
    main.setVisible(true);
    if (main.live_.sections.size()<3 || !main.liveMatrix_.isVisible()) return 2;
    const auto tick=main.cursorTick_;
    auto& matrix=main.liveMatrix_;
    matrix.launchRow(1);
    matrix.launchRow(0);
    matrix.launchRow(1);
    if (main.cursorTick_!=tick || main.transportActive_ || main.stagedCues_.size()!=3 || !matrix.isQueued(0)
        || main.stagedCues_[0].serial==main.stagedCues_[2].serial || main.stagedCues_[0].takes.empty()) return 3;
    const auto& first=main.live_.sections[1];
    const auto chord=main.project_->contextAt(ostfstudio::TimelineLane::chord,first.ticks.getStart());
    if (chord.isEmpty() || !main.liveGuide_.getDescription().contains(first.name+" / "+chord+" / ")) return 4;
    matrix.toggleCellLoop(0,1);
    if (!matrix.cellLoops(0,1) || matrix.cellLoops(0,0)) return 5;
    main.performer_.onClick();
    if (!main.liveGuide_.isVisible() || matrix.isVisible() || main.liveGuide_.getHeight()<150) return 6;
    main.performer_.onClick();
    if (!matrix.isVisible() || main.liveGuide_.getHeight()<20) return 7;
    const auto path=juce::SystemStats::getEnvironmentVariable("OSTF_LIVE_MATRIX_PNG",{});
    if (path.isNotEmpty()) {
        juce::FileOutputStream out{juce::File(path)};
        if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed() ||
            !juce::PNGImageFormat().writeImageToStream(main.createComponentSnapshot(main.getLocalBounds()),out)) return 8;
    }
    return 0;
}

int checkTrackView(const juce::File& fixture) {
    const auto wave = fixture.getParentDirectory().getParentDirectory().getParentDirectory()
        .getChildFile("examples/demo-songs/assets/glass-lantern-chimes.wav");
    if (const auto result = checkSamplePlacement(wave)) return result;
    using namespace ostfstudio;
    using A = SurfaceAction;
    {
        juce::FileInputStream stream(fixture);
        std::unique_ptr<OstfpPreview> song, added;
        if (readOstfpPreview(stream,song).failed() || song->editTracks(TrackKind::pitched,{},added).failed()) return 322;
        const auto index = added->sourceTrackCount()-1;
        const auto bar = added->ticksPerBar;
        const juce::Range<juce::int64> section{bar, 3*bar};
        juce::MemoryBlock previous;
        juce::StringArray ids;
        if (added->editNotes(index,{{NoteEdit::insert,{bar,bar+1,60,85,true,{}}}},previous,ids,section).failed()
            || added->editNotes(index,{{NoteEdit::insert,{2*bar,2*bar+1,64,85,true,{}}}},previous,ids,section).failed()) return 323;
        const auto clips = juce::JSON::parse(added->sourceScore.toString())["tracks"][int(index)]["clips"];
        if (clips.size()!=1 || juce::int64(clips[0]["start"])!=bar || juce::int64(clips[0]["duration"])!=2*bar
            || clips[0]["notes"].size()!=2) return 324;
        const auto originalNotes = added->pitchedNotes[index];
        auto check = [](const juce::Result& result) {
            if (result.failed()) std::cerr << "MIDI takes: " << result.getErrorMessage() << '\n';
            return result.wasOk();
        };
        if (!check(added->addMidiTake(index, previous)) || added->takes(index).count != 2
            || !added->pitchedNotes[index][0].enabled) return 351;
        if (!check(added->editNotes(index, {{NoteEdit::insert,{bar,bar+10,76,88,true,{}}}}, previous, ids,section,2))) return 352;
        const auto alternative = ids[0];
        auto audible = [index](const OstfpPreview& p, const juce::String& id) {
            for (const auto& n : p.pitchedNotes[index]) if (n.id == id) return n.enabled;
            return false;
        };
        if (added->takes(index).takeOf(alternative)!=2 || audible(*added, alternative)) return 353;
        if (!check(added->adoptMidiTakes({{index,{bar,2*bar},2}},previous))) return 354;
        if (audible(*added,originalNotes[0].id) || !audible(*added,originalNotes[1].id)
            || !audible(*added,alternative)) return 355;
        const auto exportedNotes = [&] {
            juce::MemoryOutputStream bytes;
            std::vector<int> pitches;
            if (!check(writeSongMidi(*added,SmfFormat::multitrack,{},bytes,index))) return pitches;
            juce::MemoryInputStream input(bytes.getData(),bytes.getDataSize(),false);
            juce::MidiFile midi;
            if (!midi.readFrom(input)) return pitches;
            for (int t=0;t<midi.getNumTracks();++t)
                for (const auto* e : *midi.getTrack(t))
                    if (e->message.isNoteOn()) pitches.push_back(e->message.getNoteNumber());
            return pitches;
        };
        if (exportedNotes()!=std::vector<int>{76,64}) return 365;
        const auto adopted = added->sourceScore;
        if (!check(added->restoreNoteScore(previous)) || !audible(*added,originalNotes[0].id)
            || audible(*added,alternative) || !check(added->restoreNoteScore(adopted))) return 356;
        SessionMix mix;
        mix.tracks.resize(added->sourceTrackCount());
        for (auto kind : added->trackKinds) mix.audioTracks.push_back(kind==TrackKind::audio);
        for (bool package : {false,true}) {
            juce::MemoryOutputStream output;
            if (!check(package ? writeOstfpPreview(*added,mix,output) : writeOstfPreview(*added,output))) return 357;
            juce::MemoryInputStream input(output.getData(),output.getDataSize(),false);
            std::unique_ptr<OstfpPreview> restored;
            if (!check(package ? readOstfpPreview(input,restored) : readOstfPreview(input,restored))
                || restored->takes(index).count!=2 || restored->takes(index).takeOf(alternative)!=2
                || restored->takes(index).adoptedAt(bar)!=2 || restored->takes(index).adoptedAt(2*bar)!=1
                || audible(*restored,originalNotes[0].id) || !audible(*restored,alternative)) return 358;
        }
        std::unique_ptr<OstfpPreview> cloned;
        if (!check(added->editTracks(TrackKind::pitched,{},cloned,index))) return 359;
        const auto copy = cloned->sourceTrackCount()-1;
        if (cloned->takes(copy).count!=2 || cloned->takes(copy).owners.size()!=1
            || cloned->takes(copy).owners.begin()->first==alternative || cloned->takes(copy).adoptedAt(bar)!=2) return 360;
        const auto end=added->playbackEndTick();
        if (!check(added->appendSection(section,"Take copy",true,previous))
            || added->takes(index).adoptedAt(end)!=2 || added->takes(index).adoptedAt(end+bar)!=1
            || added->takes(index).owners.size()!=2) return 361;
    }
    {
        juce::MemoryInputStream input(BinaryData::neonplatforms_ostfp,BinaryData::neonplatforms_ostfpSize,false);
        std::unique_ptr<OstfpPreview> song;
        if (readOstfpPreview(input,song).failed()) return 329;
        for (auto lane : {TimelineLane::section,TimelineLane::chord,TimelineLane::key,TimelineLane::scale,TimelineLane::meter,TimelineLane::tempo}) {
            const auto score=juce::JSON::parse(song->sourceScore.toString());
            const auto tick=song->ppq;
            juce::Array<juce::var> removed,added;
            juce::MemoryBlock before;
            auto r=globalBoundaryEdit(score,lane,tick,song->durationTicks,true,false,removed,added);
            if(r.wasOk()) r=song->changeGlobalEvents(lane,removed,added,before);
            if(r.failed()) {std::cerr<<globalSource(lane).events<<": "<<r.getErrorMessage()<<"\n"; return 330;}
            const auto after=juce::JSON::parse(song->sourceScore.toString());
            if(juce::int64(globalEventAt(after,lane,tick)[globalSource(lane).span?"start":"at"])!=tick ||
               song->restoreNoteScore(before).failed()) return 331;
        }
        const auto bar=song->ticksPerBar, end=song->playbackEndTick();
        const auto count = song->pitchedNotes;
        juce::MemoryBlock before;
        const auto result=song->appendSection({0,4*bar},"Intro copy",true,before);
        if(result.failed()) {std::cerr<<result.getErrorMessage()<<"\n"; return 332;}
        const auto section=globalEventAt(juce::JSON::parse(song->sourceScore.toString()),TimelineLane::section,end);
        if(section["name"]!="Intro copy" || juce::int64(section["duration"])!=4*bar) return 333;
        for(size_t i=0;i<song->sourceTrackCount();++i) for(const auto& note:count[i]) if(note.startTick<4*bar &&
            std::none_of(song->pitchedNotes[i].begin(),song->pitchedNotes[i].end(),[&](const auto& n){
                return n.startTick==note.startTick+end && n.pitch==note.pitch && n.id!=note.id;})) return 334;
        if(song->restoreNoteScore(before).failed() || song->playbackEndTick()!=end ||
           song->appendSection({0,4*bar},"Break",false,before).failed() || song->playbackEndTick()!=end+4*bar) return 335;
    }
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
    const juce::StringArray names {TRANS("File"), TRANS("View"), TRANS("Edit"), TRANS("Track"), TRANS("Mode"), TRANS("Settings"), TRANS("External connections"), TRANS("Help")};
    if (model.getMenuBarNames() != names) return 38;
    const auto edit = model.getMenuForIndex(2, {});
    juce::StringArray editActions;
    for (juce::PopupMenu::MenuItemIterator item(edit); item.next();) editActions.add(item.getItem().text);
    juce::StringArray expectedEdits{TRANS("Undo"),TRANS("Redo"),TRANS("Edit history")};
    if (juce::SystemClipboard::getTextFromClipboard().startsWith(ostfstudio::scale::clipboardReturnPrefix))
        expectedEdits.add(TRANS("Paste scales from Scale Editor"));
    if (editActions!=expectedEdits) return 246;
    const auto settings = model.getMenuForIndex(5, {});
    juce::StringArray performanceOptions;
    for (juce::PopupMenu::MenuItemIterator item(settings, true); item.next();) performanceOptions.add(item.getItem().text);
    for (auto label : {"Performance", "History entries", "History memory", "Meter refresh rate"})
        if (!performanceOptions.contains(juce::translate(label))) return 247;
    const auto external = model.getMenuForIndex(6, {});
    juce::StringArray actions;
    for (juce::PopupMenu::MenuItemIterator it(external); it.next();) actions.add(it.getItem().text);
    if (!actions.contains(TRANS("Open Splice Sounds")) || !actions.contains("Melodyne...") || !actions.contains("Ableton Link")) return 121;
    auto* examples = findControl<juce::TextEditor>(main, TRANS("Example prompts"));
    auto* connection = main.findChildWithID("chatgpt-connection");
    if (!examples || !examples->isReadOnly() || !connection || !connection->isVisible()) return 122;
    const juce::StringArray modeNames{"AI Arrangement Studio", "AI Lyric Studio", "AI Vocal Studio"};
    const auto modes = model.getMenuForIndex(4, {});
    if (modes.getNumItems() != modeNames.size()) return 122;
    juce::StringArray displayedExamples;
    int previousMode = 0;
    for (juce::PopupMenu::MenuItemIterator it(modes); it.next();) {
        const auto& mode = it.getItem();
        if (!modeNames.contains(mode.text) || !mode.isEnabled) return 122;
        if (mode.isTicked) previousMode = mode.itemID;
        model.menuItemSelected(mode.itemID, 3);
        const auto selectedMenu = model.getMenuForIndex(4, {});
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
    const auto help = model.getMenuForIndex(7, {});
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
    if (!newEnabled || !saveScoreEnabled || !savePackageEnabled || songs.size() != 8 || !songs.contains("Blue Hour Waltz (Shared GM)")
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
    const auto trackMenu = model.getMenuForIndex(3, {});
    for (juce::PopupMenu::MenuItemIterator it(trackMenu); it.next();)
        if (it.getItem().text == TRANS("Add pitched track") && it.getItem().isEnabled)
            model.menuItemSelected(it.getItem().itemID, 2);
    if (tracks->getNumItems() != 1 || tracks->getSelectedId() != 1) return 15;
    const auto renameMenu = model.getMenuForIndex(3, {});
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
    auto* mixerArm = findControl<juce::TextButton>(main, tracks->getText() + " mixer record arm");
    if (!mixerArm) return 106;
    arm->onClick();
    if (!arm->getToggleState() || !mixerArm->getToggleState()) return 106;
    auto* mixIn = findControl<juce::TextButton>(main,tracks->getText()+" mixer input");
    auto* capture = findControl<juce::TextButton>(main,tracks->getText()+" capture MIDI");
    if (!mixIn || !mixIn->getButtonText().startsWith("IN") || !capture || capture->isEnabled()) return 260;
    {
        ostfstudio::MidiPerformanceBuffer recent;
        recent.push({1.,-1,1,60,1,85}); recent.push({1.25,-1,1,60,1,0});
        recent.push({1.5,-1,1,64,1,92}); recent.push({1.75,-1,1,64,1,0});
        std::vector<ostfstudio::MidiPerformanceBuffer::Edge> edges;
        auto end=recent.snapshot(edges);
        ostfstudio::TempoMap tempo(480); tempo.append(0,120);
        auto notes=recent.notes(edges,tempo,1920,8192,2.,-1);
        if(notes.size()!=2 || notes[0].startTick!=1920 || notes[0].endTick!=2160 || notes[1].startTick!=2400 || notes[1].velocity!=92) return 261;
        recent.push({3.,512,1,67,1,91}); recent.push({3.25,752,1,67,1,0});
        recent.consume(end); edges.clear(); recent.snapshot(edges);
        notes=recent.notes(edges,tempo,0,8192,4.,-1);
        if(notes.size()!=1 || notes[0].startTick!=512 || notes[0].endTick!=752) return 262;
    }
    mixerArm->onClick();
    if (arm->getToggleState() || mixerArm->getToggleState()) return 107;
    if (!click("Add chorus vocal track") || !click("Add drum / percussion track")
        || !click("Add audio track") || tracks->getNumItems() != 5) return 34;
    auto* audioEditor = dynamic_cast<AudioClipEditor*>(main.findChildWithID("audio-clip-editor"));
    if (!audioEditor || !audioEditor->isVisible() || audioEditor->getWidth() < 200) return 51;
    if (!audioEditor->recordRuler.chooseCycle || !audioEditor->recordRuler.cycleBars) return 67;
    audioEditor->recordRuler.chooseCycle(.12, .39);
    if (audioEditor->recordRuler.cycleBars().isEmpty()) return 68;
    audioEditor->channels.setSelectedId(2, juce::sendNotificationSync);
    const auto audioTrack = tracks->getSelectedId();
    auto* mixGain = findControl<juce::Slider>(main, tracks->getText() + " mixer input gain");
    auto* trackGain = findControl<juce::Slider>(main, tracks->getText() + " input gain");
    if (!mixGain || !trackGain) {
        std::cerr << "Missing gain controls for " << tracks->getText() << " (mixer " << (mixGain != nullptr)
                  << ", track " << (trackGain != nullptr) << ")\n";
        return 264;
    }
    mixGain->setValue(6.5,juce::sendNotificationSync);
    if (std::abs(trackGain->getValue() - 6.5) > 0.000001) {
        std::cerr << "Track gain after mixer change: " << trackGain->getValue() << '\n';
        return 264;
    }
    trackGain->setValue(0,juce::sendNotificationSync);
    if (std::abs(mixGain->getValue()) > 0.000001) {
        std::cerr << "Mixer gain after track change: " << mixGain->getValue() << '\n';
        return 264;
    }
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
    auto* search = dynamic_cast<juce::TextEditor*>(pane->findChildWithID("plugin-search"));
    auto* tree = descendant<juce::TreeView>(*pane);
    if (!search || !search->isVisible() || !tree || !tree->getRootItem()) return 362;
    // Exercise multiple manufacturers even on a clean machine with no installed VST3s.
    pane->catalog_.clear();
    for (auto manufacturer : {"Fixture Audio", "Fixture Instruments"}) {
        juce::PluginDescription instrument;
        instrument.pluginFormatName = "VST3";
        instrument.manufacturerName = manufacturer;
        instrument.name = instrument.manufacturerName + " Piano";
        instrument.isInstrument = true;
        pane->catalog_.add(instrument);
    }
    pane->refreshTree();
    const auto makers = tree->getRootItem()->getNumSubItems();
    auto* vendor = dynamic_cast<juce::ComboBox*>(pane->findChildWithID("plugin-manufacturer"));
    if (!vendor || vendor->getNumItems()<2) return 365;
    vendor->setSelectedId(2,juce::sendNotificationSync);
    if (tree->getRootItem()->getNumSubItems()!=1 || tree->getRootItem()->getSubItem(0)->getUniqueName()!=vendor->getText()) return 366;
    search->setText("no-match-9b218b8a",false); search->onTextChange();
    if (tree->getRootItem()->getNumSubItems()!=0) return 363;
    search->setText({},false); search->onTextChange();
    if (tree->getRootItem()->getNumSubItems()!=1) return 367;
    vendor->setSelectedId(1,juce::sendNotificationSync);
    if (tree->getRootItem()->getNumSubItems()!=makers) return 364;
    const auto* imported = audioEditor->sourceClip();
    if (!imported || imported->localFile != sample || imported->durationTicks <= 0) return 54;
    auto* loopButton = dynamic_cast<juce::Button*>(audioEditor->findChildWithID("audio-loop"));
    auto* warpButton = dynamic_cast<juce::Button*>(audioEditor->findChildWithID("audio-warp"));
    if (!loopButton || !warpButton || !loopButton->isEnabled() || !warpButton->isEnabled()) return 55;
    loopButton->setToggleState(true, juce::sendNotificationSync);
    if (!audioEditor->sourceClip()->timing.loop) return 56;
    warpButton->setToggleState(true, juce::sendNotificationSync);
    imported = audioEditor->sourceClip();
    if (!imported->timing.loop || !imported->timing.cycleTicks) return 368;
    if (!imported->timing.preservePitch) return 369;
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
        float(audioGrid.getBottom() + (audioEditor->getHeight() - audioGrid.getBottom()) / 4));
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
    auto trackActions = model.getMenuForIndex(3, {});
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
        auto* listen = findControl<juce::TextButton>(*mixer, TRANS("Monitor Cue on the selected Master output"));
        auto* cueStatus = findControl<juce::Label>(*mixer, TRANS("Cue output"));
        if (!listen || !cueStatus || listen->getToggleState()) return 254;
        mixer->setHardwareOutputLabels({},{},false);
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
        auto* masterMute = findControl<juce::TextButton>(*mixer, TRANS("Master") + " mute");
        auto* masterMeter = findControl<LevelMeter>(*mixer,"Master stereo peak meter");
        if (!masterMute || !masterMeter || !masterMeter->isVisible() || masterMeter->getWidth()<34
            || masterMeter->getHeight()<65) return 258;
        const auto masterArea = mixer->getLocalArea(masterMute->getParentComponent(),masterMute->getParentComponent()->getLocalBounds());
        const auto trackArea = mixer->getLocalArea(output->getParentComponent(),output->getParentComponent()->getLocalBounds());
        auto* masterOut=findControl<juce::TextButton>(*mixer,"Master hardware output");
        auto* cueOut=findControl<juce::TextButton>(*mixer,"Cue hardware output");
        if (!masterOut || !cueOut || !masterOut->isVisible() || !cueOut->isVisible()) {
            std::cerr << "Missing Master or Cue hardware output selector\n";
            return 264;
        }
        auto* outputViewport = cueStatus->findParentComponentOfClass<juce::Viewport>();
        auto* sourceViewport = output->findParentComponentOfClass<juce::Viewport>();
        if (!sourceViewport || !outputViewport || masterArea.getWidth() <= trackArea.getWidth()
            || mixer->getLocalArea(sourceViewport, sourceViewport->getLocalBounds()).getRight() > outputViewport->getX()
            || mixer->getLocalArea(outputViewport, outputViewport->getLocalBounds()).getRight() > masterArea.getX()) return 258;
        const auto masterPoint = mixer->getLocalPoint(masterOut,masterOut->getLocalBounds().getCentre());
        if (mixer->dropTargetAt(masterPoint,true)!=0) {
            std::cerr << "Master drop point " << masterPoint.toString() << " in mixer " << mixer->getLocalBounds().toString() << '\n';
            return 265;
        }
        const auto viewPosition = outputViewport->getViewPosition();
        outputViewport->setViewPositionProportionately(1.,0.);
        const auto cuePoint = mixer->getLocalPoint(cueOut,cueOut->getLocalBounds().getCentre());
        if (mixer->dropTargetAt(cuePoint,true)) {
            std::cerr << "Cue drop point " << cuePoint.toString() << ", viewport " << outputViewport->getBounds().toString() << '\n';
            return 266;
        }
        outputViewport->setViewPosition(viewPosition);
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
        if (addBus->isEnabled() || !addReturn->isEnabled()) return 254;
        const auto firstTrack = tracks->getItemText(0);
        auto* pan = findControl<juce::Slider>(main, firstTrack + " pan");
        if (!pan || pan->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag) return 259;
        const size_t selected = size_t(tracks->getItemId(0) - 1);
        auto sendTitle = [&](size_t r) {
            return firstTrack + " / " + TRANS("Send: ") + mixer->outputName(ostfstudio::returnOutput(r));
        };
        for (auto r = returns; r < ostfstudio::maxMixerReturns; ++r) {
            const auto placeholder = firstTrack + " / " + TRANS("Send: ") + TRANS("Return ") + juce::String(int(r + 1));
            auto* inactive = findControl<juce::Slider>(*mixer, placeholder);
            if (!inactive || inactive->isEnabled() || !inactive->isVisible()) return 259;
            if (!addReturn->isEnabled()) return 259;
            addReturn->onClick();
            auto* knob = findControl<juce::Slider>(*mixer, sendTitle(r));
            auto* header = findControl<juce::Slider>(*pan->getParentComponent(), sendTitle(r));
            if (!knob || !knob->isEnabled() || !header || header == knob || mixer->returnCount() != r + 1) return 259;
            header->setValue(21, juce::sendNotificationSync);
            if (knob->getValue() != 21) return 259;
            knob->setValue(34, juce::sendNotificationSync);
            if (header->getValue() != 34 || std::abs(mixer->channel(selected).returnSends[r] - .34f) > .001f) return 259;
        }
        if (addReturn->isEnabled() || !click("Piano Roll")) return 254;
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
    const auto reserved = juce::GlyphArrangement::getStringWidth(heading, TRANS("Degree"));
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
        meter.clear();
        meter.update({juce::Decibels::decibelsToGain(-12.f),0.f},2.);
        meter.update({},3.);
        const auto heldImage = meter.createComponentSnapshot(meter.getLocalBounds());
        if (heldImage.getPixelAt(vertical?4:97,vertical?25:4) != juce::Colour(0xfff6ecc5)) {
            std::cerr << "Missing held peak on " << (vertical ? "vertical" : "horizontal") << " meter\n";
            return 263;
        }
        meter.update({}, 8.0);
        const auto silent = meter.createComponentSnapshot(meter.getLocalBounds());
        if (silent.getPixelAt(vertical ? 4 : 60, vertical ? 60 : 4) != juce::Colour(0xff152026)) return 12;
    }
    tracks->setSelectedItemIndex(0,juce::sendNotificationSync);
    if (!click("Live session")) return 284;
    auto* matrix = descendant<LiveSectionMatrix>(main);
    auto* session = descendant<LiveSections>(main);
    auto* sectionGrid = descendant<PianoNoteGrid>(main);
    auto* selector = descendant<LiveTrackSelector>(main);
    if (!matrix || !matrix->isVisible() || !session || session->sections.empty() || !sectionGrid || !selector) return 285;
    const auto last = int(session->sections.size()) - 1;
    session->list.selectRow(last);
    session->changeRepeats(last, 1);
    if (!click("Arrangement") || !click("Live session") || session->selected != last || session->sections.back().repeats != 2) return 287;
    int guitarItem = -1;
    for (int i=0; i<tracks->getNumItems(); ++i) if (tracks->getItemText(i)=="Lead Guitar") guitarItem=i;
    if (guitarItem<0) return 291;
    const int other = (guitarItem+1)%tracks->getNumItems();
    juce::PropertySet livePrefs;
    selector->openDocument("smoke-live",livePrefs);
    selector->toggleTrack(other);
    if (selector->includesTrack(other) || !selector->includesTrack(guitarItem)) return 303;
    selector->openDocument("different-song",livePrefs);
    if (!selector->includesTrack(other)) return 306;
    selector->openDocument("smoke-live",livePrefs);
    if (selector->includesTrack(other)) return 307;
    selector->toggleTrack(other);
    const auto previousSize = main.getLocalBounds();
    main.setSize(1450,1000);
    const auto firstName = session->sections[0].name;
    auto* firstSlot = findControl<juce::TextButton>(*matrix, firstName + " / Lead Guitar");
    auto* moreRepeats = findControl<juce::TextButton>(*matrix, TRANS("Loop count") + " + / " + firstName);
    if (!matrix->isVisible() || session->isVisible() || !firstSlot || !moreRepeats) return 314;
    const auto beforeRepeats = session->sections[0].repeats;
    moreRepeats->onClick();
    if (session->selected != last || session->sections[0].repeats != beforeRepeats+1) return 315;
    firstSlot->onClick();
    if (tracks->getSelectedItemIndex()!=guitarItem || session->selected!=0
        || descendant<PianoNoteGrid>(main)!=sectionGrid || !sectionGrid->editable
        || !main.findChildWithID("piano-ruler")->isVisible()) return 316;
    const auto tick = session->range().getStart();
    const auto beforeInsert = sectionGrid->notes.size();
    sectionGrid->commit({{NoteEdit::insert,{tick,tick+1,51,90,true,{}}}});
    if (sectionGrid->notes.size()!=beforeInsert+1) return 317;
    const auto editId = sectionGrid->selection[0];
    const auto hasInserted = [&]{return std::any_of(sectionGrid->notes.begin(),sectionGrid->notes.end(),[&](const auto& n){return n.id==editId;});};
    if (editId.isEmpty() || !click("Arrangement") || !hasInserted()) return 318;
    sectionGrid->undoRedo(false);
    if (hasInserted() || !click("Live session") || !matrix->isVisible() || hasInserted()) return 319;
    const auto boundary = session->range().getEnd();
    sectionGrid->commit({{NoteEdit::insert,{boundary-1,boundary+32,54,90,true,{}}}});
    const auto crossing = sectionGrid->selection[0];
    if (crossing.isEmpty() || !std::any_of(sectionGrid->notes.begin(),sectionGrid->notes.end(),[&](const auto& n){
        return n.id==crossing && n.endTick==boundary+32; })) return 326;
    session->list.selectRow(1);
    if (sectionGrid->acceptsOnset(boundary-1)) return 327;
    sectionGrid->commit({{NoteEdit::erase,{boundary-1,boundary+32,54,90,true,crossing}}});
    if (!std::any_of(sectionGrid->notes.begin(),sectionGrid->notes.end(),[&](const auto& n){return n.id==crossing;})) return 328;
    session->list.selectRow(0);
    sectionGrid->undoRedo(false);
    if (const auto path = juce::SystemStats::getEnvironmentVariable("OSTF_LIVE_MATRIX_PNG",{}); path.isNotEmpty()) {
        juce::FileOutputStream out{juce::File(path)};
        if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed() ||
            !juce::PNGImageFormat().writeImageToStream(main.createComponentSnapshot(main.getLocalBounds()),out)) return 320;
    }
    auto* lowerMixer = dynamic_cast<juce::TextButton*>(main.findChildWithID("live-mixer"));
    auto* lowerPiano = dynamic_cast<juce::TextButton*>(main.findChildWithID("live-piano"));
    if (!lowerMixer || !lowerPiano) return 321;
    lowerMixer->onClick();
    if (!matrix->isVisible() || sectionGrid->isVisible()) return 322;
    lowerPiano->onClick();
    session->list.selectRow(last);
    main.setSize(previousSize.getWidth(), previousSize.getHeight());
    const auto area=session->range();
    const auto countBefore=sectionGrid->notes.size();
    sectionGrid->commit({{NoteEdit::insert,{area.getStart(),area.getStart()+1,47,93,true,{}}}});
    if(sectionGrid->notes.size()!=countBefore+1) return 292;
    sectionGrid->commit({{NoteEdit::insert,{area.getEnd(),area.getEnd()+1,47,93,true,{}}}});
    if(sectionGrid->notes.size()!=countBefore+1) return 293;
    matrix->editCell(size_t(other),last);
    if(tracks->getSelectedItemIndex()!=other) return 294;
    if (!click("Print lead sheet")) return 288;
    auto* sheet=descendant<ScoreView>(main);
    if (!sheet || !sheet->isVisible() || !sheet->showsLeadSheet() || !click("Print score") || sheet->showsLeadSheet()) return 289;
    if (!click("Arrangement")) return 290;
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
