#include "ui/PreviewTransport.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include "../plugins/instruments/SingingScore.h"

PreviewTransport::~PreviewTransport() { stop(); }

namespace {
// Reconstruct the state immediately before the first sample of the new block.
// Events on that sample are still delivered normally from the timeline.
juce::MidiBuffer midiAtSeek(const juce::MidiBuffer& timeline, int sample) {
    std::map<std::pair<int,int>,juce::MidiMessage> phones;
    std::array<int, 16> programs;
    programs.fill(-1);
    std::array<std::array<int,2>,16> selectedBank{}, pendingBank{};
    for (auto& bank : selectedBank) bank.fill(-1);
    for (auto& bank : pendingBank) bank.fill(-1);
    std::array<std::map<int, juce::MidiMessage>, 16> controls;
    std::array<std::array<juce::uint8, 128>, 16> sounding{};
    for (auto event : timeline) {
        if (event.samplePosition > sample) break;
        const auto msg = event.getMessage();
        if (msg.isSysEx()) {
            if (event.samplePosition < sample && ostfstudio::singing::isPhone(msg)) {
                const auto* d=msg.getSysExData(); phones[{d[4],d[5]}]=msg;
            }
            continue;
        }
        const auto channel = msg.getChannel() - 1;
        if (event.samplePosition < sample && msg.isProgramChange()) {
            programs[channel] = msg.getProgramChangeNumber();
            selectedBank[channel] = pendingBank[channel];
        }
        if (event.samplePosition < sample) {
            if (msg.isControllerOfType(0) || msg.isControllerOfType(32))
                pendingBank[channel][msg.getControllerNumber() == 32] = msg.getControllerValue();
            else if (msg.isController()) controls[channel][msg.getControllerNumber()] = msg;
            else if (msg.isPitchWheel()) controls[channel][128] = msg;
            else if (msg.isChannelPressure()) controls[channel][129] = msg;
            else if (msg.isAftertouch()) controls[channel][130 + msg.getNoteNumber()] = msg;
        }
        if (msg.isNoteOff() || msg.isNoteOn()) {
            auto& velocity = sounding[channel][msg.getNoteNumber()];
            velocity = msg.isNoteOn() && event.samplePosition < sample ? msg.getVelocity() : 0;
        }
    }
    juce::MidiBuffer result;
    for (int ch = 0; ch < 16; ++ch) {
        result.addEvent(juce::MidiMessage::allSoundOff(ch + 1), 0);
        result.addEvent(juce::MidiMessage::allNotesOff(ch + 1), 0);
        result.addEvent(juce::MidiMessage::controllerEvent(ch + 1, 121, 0), 0);
        result.addEvent(juce::MidiMessage::pitchWheel(ch + 1, 8192), 0);
        for (const auto& [key, message] : controls[ch]) result.addEvent(message, 0);
        const auto sendBank = [&](const std::array<int,2>& bank) {
            for (int side = 0; side < 2; ++side) if (bank[side] >= 0)
                result.addEvent(juce::MidiMessage::controllerEvent(ch + 1, side == 0 ? 0 : 32, bank[side]), 0);
        };
        if (programs[ch] >= 0) {
            sendBank(selectedBank[ch]);
            result.addEvent(juce::MidiMessage::programChange(ch + 1, programs[ch]), 0);
        }
        if (pendingBank[ch] != selectedBank[ch]) sendBank(pendingBank[ch]);
        for (const auto& [key, msg] : phones) if (key.first == ch && sounding[ch][key.second]) result.addEvent(msg,0);
        for (int pitch = 0; pitch < 128; ++pitch)
            if (const auto velocity = sounding[ch][pitch])
                result.addEvent(juce::MidiMessage::noteOn(ch + 1, pitch, velocity), 0);
    }
    return result;
}
}

bool PreviewTransport::supportsInstrument(const juce::AudioPluginInstance& instrument) {
    const auto outputs = instrument.getMainBusNumOutputChannels();
    const auto description = instrument.getPluginDescription();
    return description.isInstrument
        && (outputs == 1 || outputs == 2)
        && std::max(instrument.getTotalNumInputChannels(), instrument.getTotalNumOutputChannels()) <= 16;
}

bool PreviewTransport::supportsInsert(const juce::AudioPluginInstance& effect) {
    return !effect.getPluginDescription().isInstrument && effect.getTotalNumInputChannels() == 2
        && effect.getTotalNumOutputChannels() == 2;
}

juce::Result PreviewTransport::start(juce::AudioDeviceManager &devices, const ostfstudio::OstfpPreview &project,
                                     bool record, std::span<const TrackSetup> tracks, const Inserts &inserts,
                                     juce::int64 startTick, int semitones, double tempoRate,
                                     std::optional<size_t> inputTrack, const TakeOptions* options, bool monitorOnly) {
    stop();
    auto* device = devices.getCurrentAudioDevice();
    if (!device || !device->isOpen() || device->getActiveOutputChannels().isZero())
        return juce::Result::fail("Select an active audio output in Settings first.");
    const bool drumInput = record && inputTrack && *inputTrack < project.sourceTrackCount() &&
                           project.trackKinds[*inputTrack] == ostfstudio::TrackKind::drum;
    const int inputs = record && options ? options->inputChannels : 0;
    if (inputs && (options->firstInputBuffer < 0 || options->firstInputBuffer + inputs > device->getActiveInputChannels().countNumberOfSetBits()))
        return juce::Result::fail("Enable the required audio input channels in Settings.");
    if (record && !inputs && (!drumInput || (options && options->midiInput.port.isNotEmpty()))) {
        bool enabled = false;
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            enabled = enabled || (devices.isMidiInputDeviceEnabled(input.identifier) && (!options ||
                (options->midiInput.enabled && (options->midiInput.port.isEmpty() || options->midiInput.port == input.identifier))));
        if (!enabled) return juce::Result::fail("Enable a MIDI input in Settings before recording.");
    }
    const auto result = preparePlayback(project, tracks, inserts, device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), semitones, tempoRate, monitorOnly, options ? options->linkTempo : 0);
    if (result.failed()) return result;
    if (startTick > 0 && !monitorOnly) seekToTick(startTick);
    if (monitorOnly && inputTrack && *inputTrack < tracks.size()) {
        sample_ = juce::roundToInt(clock_.secondsAt(startTick) * sampleRate_);
        Timelines patches;
        makeMidiTimelines(project, tracks, 0, patches, true);
        const auto dest = tracks[*inputTrack].outputTrack.value_or(*inputTrack);
        if (dest < instruments_.size()) {
            auto chased = midiAtSeek(patches[dest], int(sample_.load()) + 1);
            for (auto event : chased) {
                const auto m = event.getMessage();
                if (m.isProgramChange() || m.isControllerOfType(0) || m.isControllerOfType(32))
                    instruments_[dest].seekEvents.addEvent(m, 0);
            }
        }
    }
    if (options && !options->cycle.isEmpty()) setLoop(options->cycle);
    if (!inputs && (record || inputTrack)) prepareRecording(project, tracks, inputTrack, record);
    if ((record || inputTrack) && options) {
        if (const auto armed = prepareTake(*options); armed.failed()) { stop(); return armed; }
    }
    launchClock_ = !monitorOnly && options ? options->link : nullptr;
    if (launchClock_) {
        linkBaseTempo_ = options->linkTempo;
        linkBpm_ = linkBaseTempo_;
        linkQuantum_ = options->linkQuantum;
        linkStartBeat_ = double(startTick) / project.ppq;
        playbackLink_ = inputs == 0 ? launchClock_ : nullptr;
        const auto countBeats = countRemaining_.load() / sampleRate_ * options->linkTempo / 60.0;
        if (options->announceLinkStart) {
            launchMicros_ = launchClock_->startAt(linkStartBeat_ - countBeats, linkQuantum_, true);
        } else {
            const auto launch = launchClock_->followStart(linkQuantum_);
            launchMicros_ = launch.micros;
            linkStartBeat_ = launch.gridBeat;
            seekToTick(startTick + juce::int64(std::llround(launch.elapsedBeats * project.ppq)));
        }
        outputLatencyMicros_ = device->getOutputLatencyInSamples() * 1.0e6 / sampleRate_;
    }
    devices_ = &devices;
    devices.addAudioCallback(this);
    if (!inputs && (record || monitorTrack_)) devices.addMidiInputDeviceCallback({}, this);
    return result;
}

void PreviewTransport::prepareRecording(const ostfstudio::OstfpPreview &project, std::span<const TrackSetup> tracks,
                                        std::optional<size_t> inputTrack, bool capture) {
    jassert(devices_ == nullptr);
    recording_ = capture;
    takeStart_ = sample_.load();
    const bool drumInput = inputTrack && *inputTrack < project.trackKinds.size() &&
                           project.trackKinds[*inputTrack] == ostfstudio::TrackKind::drum;
    inputPitches_.fill(!drumInput);
    if (drumInput)
        for (const auto &pad : project.slotsByTrack[*inputTrack])
            if (pad.pitch >= 0 && pad.pitch < 128)
                inputPitches_[pad.pitch] = true;
    monitorTrack_.reset();
    if (inputTrack && *inputTrack < tracks.size()) {
        monitorTrack_ = tracks[*inputTrack].outputTrack.value_or(*inputTrack);
        monitorChannel_ = tracks[*inputTrack].midiChannel;
    }
    liveMessages_.clear();
    liveMessages_.ensureSize(32768);
    queuedLiveMessages_ = 0;
}

juce::Result PreviewTransport::preparePlayback(const ostfstudio::OstfpPreview& project, std::span<const TrackSetup> tracks,
                                             const Inserts& inserts, double sampleRate, int blockSize, int semitones, double tempoRate, bool monitorOnly, double linkTempo) {
    stop();
    playbackLink_ = launchClock_ = nullptr;
    linkSubsample_ = 0;
    linkBpm_ = 0;
    monitorOnly_ = monitorOnly;
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || blockSize < 1)
        return juce::Result::fail("Invalid audio configuration");
    sampleRate_ = sampleRate;
    if (project.clock.empty()) return juce::Result::fail("The song needs an initial tempo.");
    if (!monitorOnly) {
        if (auto check = project.checkAudition(semitones, tempoRate); check.failed()) return check;
    }
    const auto scaled = project.clock.scaled(tempoRate);
    if (!scaled) return juce::Result::fail("Invalid tempo rate");
    clock_ = *scaled;
    if (linkTempo > 0) {
        clock_ = ostfstudio::TempoMap(project.ppq);
        if (!clock_.append(0, linkTempo)) return juce::Result::fail("Invalid Link tempo");
    }
    ppq_ = project.ppq;
    meters_ = project.meters;
    meterBeats_ = project.meterBeats;
    beatUnit_ = project.beatUnit;
    scoreTicks_ = project.playbackEndTick();
    const auto total = clock_.secondsAt(scoreTicks_) * sampleRate_;
    if (!std::isfinite(total) || total < 1.0 || total > sampleRate_ * 3600.0
        || total > std::numeric_limits<int>::max() - 1)
        return juce::Result::fail("Preview transport supports songs up to one hour.");
    loopSamples_ = {}; loopRestart_ = false;
    captured_.setSize(0, 0);
    mapInput_ = false;
    controlPort_.clear();
    midiInput_ = {};
    elapsed_ = countRemaining_ = 0;
    beatSamples_.clear();
    for (juce::int64 bar = 0; !monitorOnly && bar < scoreTicks_; bar += project.ticksPerBar) {
        const auto meter = ostfstudio::activeMeter(meters_, double(bar), {0, meterBeats_, beatUnit_});
        for (int beat = 0; beat < meter.numerator; ++beat) {
            const auto tick = bar + std::llround(beat * 4.0 * ppq_ / meter.denominator);
            if (tick < scoreTicks_) beatSamples_.emplace_back(juce::roundToInt(clock_.secondsAt(tick) * sampleRate_), beat == 0);
        }
    }
    lastSample_ = static_cast<juce::int64>(std::ceil(total));
    pendingNotes_ = false;
    int outputChannels = 0;
    for (auto& slot : instruments_) {
        slot.timeline.clear();
        slot.seekEvents.clear();
        slot.regions.clear();
    }
    ostfstudio::AudioRegionPlayback::SourceCache sources;
    if (!monitorOnly) for (const auto& audio : project.audio) {
        if (audio.track >= instruments_.size()) continue;
        auto& region = instruments_[audio.track].regions.emplace_back();
        if (auto r = region.load(audio, clock_, sampleRate_, int(lastSample_), &sources); r.failed()) return r;
        outputChannels = 2;
    }
    for (auto& slot : instruments_) slot.panEnvelope = {};
    for (int i = 0; i < project.trackIds.size(); ++i)
        if (const auto it = project.panAutomation.find(project.trackIds[i]); it != project.panAutomation.end())
            instruments_[size_t(i)].panEnvelope = it->second;
    Timelines timelines;
    if (!monitorOnly) {
        if (auto result = makeMidiTimelines(project, tracks, semitones, timelines); result.failed()) return result;
    }
    for (std::size_t i = 0; i < instruments_.size(); ++i) instruments_[i].timeline.swapWith(timelines[i]);
    playbackRoutes_.assign(tracks.begin(),tracks.end());
    playbackShift_ = semitones;
    for (const auto& setup : tracks) if (setup.external) outputChannels = std::max(outputChannels, 2);
    for (const auto& setup : tracks)
        if (setup.instrument && !setup.outputTrack && !setup.external)
            outputChannels = std::max({outputChannels, setup.instrument->getTotalNumInputChannels(),
                                       setup.instrument->getTotalNumOutputChannels()});
    for (const auto& chain : inserts) for (auto* effect : chain) if (effect) outputChannels = std::max(outputChannels, 2);
    pluginAudio_.setSize(0, 0);
    if (outputChannels > 0) {
        pluginAudio_.setSize(std::max(2, outputChannels), blockSize);
        for (auto& stem : stems_) stem.setSize(2, blockSize);
        pluginMidi_.ensureSize(1024 * 1024);
        patchMidi_.ensureSize(1024 * 1024);
        for (std::size_t track = 0; track < std::min(tracks.size(), instruments_.size()); ++track) {
            const auto& setup = tracks[track];
            auto& slot = instruments_[track];
            slot.external = setup.external;
            slot.externalChannels = {};
            slot.sentChannels = {};
            if (setup.external) {
                slot.externalChannels[size_t(setup.midiChannel - 1)] = true;
                for (auto event : slot.timeline) {
                    const auto channel = event.getMessage().getChannel();
                    if (channel > 0) slot.externalChannels[size_t(channel - 1)] = true;
                }
                continue;
            }
            if (setup.outputTrack) continue;
            if (auto* instance = setup.instrument) {
                instance->setPlayHead(this);
                instance->setRateAndBufferSizeDetails(sampleRate_, blockSize);
                instance->prepareToPlay(sampleRate_, blockSize);
            }
            instruments_[track].instance = setup.instrument;
        }
        inserts_ = inserts;
        for (size_t i = 0; i < instruments_.size(); ++i) if (instruments_[i].external) inserts_[outputCount + i] = {};
        for (const auto& chain : inserts_) for (auto* effect : chain) if (effect) {
            effect->setPlayHead(this);
            effect->setRateAndBufferSizeDetails(sampleRate_, blockSize);
            effect->prepareToPlay(sampleRate_, blockSize);
        }
    }
    pressed_.fill(-1);
    recorded_.clear();
    sample_ = 0;
    recording_ = false;
    running_ = true;
    return juce::Result::ok();
}

juce::Result PreviewTransport::makeMidiTimelines(const ostfstudio::OstfpPreview& project, std::span<const TrackSetup> tracks,
                                                int semitones, Timelines& timelines, bool patchesOnly) const {
    std::optional<ostfstudio::singing::Lyrics> lyrics;
    int noteCount = 0;
    std::array<bool, maxTracks> sharedHosts{};
    for (const auto& route : tracks)
        if (route.outputTrack && *route.outputTrack < sharedHosts.size())
            sharedHosts[*route.outputTrack] = true;
    for (std::size_t track = 0; track < std::min(project.pitchedNotes.size(), instruments_.size()); ++track) {
        const auto setup = track < tracks.size() ? tracks[track] : TrackSetup{};
        const auto outputTrack = setup.outputTrack.value_or(track);
        if (outputTrack >= instruments_.size()) continue;
        const bool sharedPart = sharedHosts[outputTrack];
        auto* midi = &timelines[outputTrack];
        const bool instrument = outputTrack < tracks.size() && (tracks[outputTrack].external || (tracks[outputTrack].instrument && tracks[outputTrack].instrument->acceptsMidi()));
        struct OrderedPatch { juce::int64 tick, order; size_t ordinal; juce::MidiMessage message; };
        std::vector<OrderedPatch> patches;
        if (track < project.performanceCurves.size()) for (const auto& curve : project.performanceCurves[track]) {
            const auto& k = curve.control;
            if (k.type.isEmpty() || k.type == "programChange" || !curve.enabled) continue;
            const bool bank = k.type == "controlChange" && (k.number == 0 || k.number == 32);
            if ((patchesOnly || !instrument) && !bank) continue;
            const int ch = sharedPart || !k.channel ? setup.midiChannel : k.channel;
            for (const auto& p : curve.points) {
                const int v = juce::roundToInt(p.value * k.maximum());
                auto msg = k.type == "pitchBend" ? juce::MidiMessage::pitchWheel(ch, v)
                         : k.type == "channelPressure" ? juce::MidiMessage::channelPressureChange(ch, v)
                         : k.type == "polyPressure" ? juce::MidiMessage::aftertouchChange(ch, k.number, v)
                         : juce::MidiMessage::controllerEvent(ch, k.number, v);
                if (bank) patches.push_back({p.tick, p.order, p.ordinal, msg});
                else midi->addEvent(msg, juce::roundToInt(clock_.secondsAt(p.tick) * sampleRate_));
            }
        }
        if (track < project.programChanges.size()) for (const auto& pc : project.programChanges[track])
            patches.push_back({pc.tick, pc.order, pc.ordinal, juce::MidiMessage::programChange(sharedPart ? setup.midiChannel : pc.channel.value_or(setup.midiChannel), pc.program)});
        std::stable_sort(patches.begin(), patches.end(), [](const auto& a, const auto& b) {
            return std::tie(a.tick, a.order, a.ordinal) < std::tie(b.tick, b.order, b.ordinal);
        });
        for (const auto& e : patches) midi->addEvent(e.message, juce::roundToInt(clock_.secondsAt(e.tick) * sampleRate_));
        if (!instrument || patchesOnly) continue;
        auto addNote = [&](juce::int64 start, juce::int64 end, int pitch, int velocity, int channel) {
            if (noteCount >= 20000) return false;
            const auto first = static_cast<int>(std::llround(clock_.secondsAt(start) * sampleRate_));
            const auto last = static_cast<int>(std::llround(clock_.secondsAt(end) * sampleRate_));
            if (last <= first) return true;
            ++noteCount;
            midi->addEvent(juce::MidiMessage::noteOn(channel, pitch, static_cast<juce::uint8>(velocity)), first);
            midi->addEvent(juce::MidiMessage::noteOff(channel, pitch), last);
            return true;
        };
        const int shift = track < project.trackKinds.size() && project.trackKinds[track] != ostfstudio::TrackKind::pitched ? 0 : semitones;
        if (tracks[outputTrack].instrument && tracks[outputTrack].instrument->getName() == "OSTF Formant Singer") {
            if (!lyrics) lyrics = ostfstudio::singing::lyricsFor(juce::JSON::parse(project.sourceScore.toString()));
            for (const auto& n : project.pitchedNotes[track]) if (n.enabled) {
                const int key=n.pitch+(n.fixedTrigger?0:shift);
                if (key<0 || key>127) continue;
                const auto entry=lyrics->find(n.id);
                if (entry == lyrics->end()) {
                    midi->addEvent(ostfstudio::singing::phoneMessage(setup.midiChannel,key,{}),juce::roundToInt(clock_.secondsAt(n.startTick)*sampleRate_));
                } else for (size_t p=0;p<entry->second.size();++p) {
                    auto tick = n.startTick+(n.endTick-n.startTick)*static_cast<juce::int64>(p)/static_cast<juce::int64>(entry->second.size());
                    midi->addEvent(ostfstudio::singing::phoneMessage(setup.midiChannel,key,entry->second[p]),juce::roundToInt(clock_.secondsAt(tick)*sampleRate_));
                }
            }
        }
        for (const auto& note : project.pitchedNotes[track])
            if (note.enabled && !addNote(note.startTick, note.endTick, note.pitch + (note.fixedTrigger ? 0 : shift), note.velocity.value_or(92), setup.midiChannel))
                return juce::Result::fail("Preview transport supports up to 20,000 notes.");
        if (track < project.drumNotes.size())
            for (const auto& note : project.drumNotes[track])
                if (note.enabled && !addNote(note.startTick, note.endTick, note.pitch, note.velocity.value_or(92),
                                             sharedPart || (outputTrack < tracks.size() && tracks[outputTrack].external) ? setup.midiChannel : 10))
                    return juce::Result::fail("Preview transport supports up to 20,000 notes.");
    }
    return juce::Result::ok();
}

void PreviewTransport::adoptPendingNotes() {
    if (!pendingNotes_) return;
    for (std::size_t i = 0; i < instruments_.size(); ++i) {
        instruments_[i].timeline.swapWith(pendingTimelines_[i]);
        instruments_[i].loopEvents.swapWith(pendingLoopEvents_[i]);
    }
    pendingNotes_ = false;
}

juce::Result PreviewTransport::updateLoopNotes(const ostfstudio::OstfpPreview& project) {
    if (loopSamples_.isEmpty() || !running_) return juce::Result::fail("Loop playback ended.");
    Timelines next, chase;
    if (auto result = makeMidiTimelines(project, playbackRoutes_, playbackShift_, next); result.failed()) return result;
    for (std::size_t i = 0; i < instruments_.size(); ++i)
        if (!next[i].isEmpty() || instruments_[i].instance) chase[i] = midiAtSeek(next[i],loopSamples_.getStart());
    const auto publish = [&] {
        for (std::size_t i = 0; i < instruments_.size(); ++i) {
            next[i].swapWith(pendingTimelines_[i]);
            chase[i].swapWith(pendingLoopEvents_[i]);
        }
        pendingNotes_ = true;
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); publish(); }
    else publish();
    return juce::Result::ok();
}

void PreviewTransport::setLoop(juce::Range<juce::int64> ticks) {
    // A range change also commits edits waiting for the old loop boundary.
    bool switched = false;
    const auto flush = [&] { switched = pendingNotes_; adoptPendingNotes(); };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); flush(); }
    else flush();
    if (switched) seekToTick(positionTick());
    juce::Range<int> samples;
    std::array<juce::MidiBuffer, maxTracks> chase;
    if (!ticks.isEmpty() && !recording_ && running_) {
        samples = {juce::roundToInt(clock_.secondsAt(std::max<juce::int64>(0,ticks.getStart())) * sampleRate_),
                   juce::roundToInt(clock_.secondsAt(std::min(scoreTicks_,ticks.getEnd())) * sampleRate_)};
        for (size_t i = 0; i < instruments_.size(); ++i) if (!instruments_[i].timeline.isEmpty() || instruments_[i].instance)
            chase[i] = midiAtSeek(instruments_[i].timeline, samples.getStart());
    }
    auto apply = [&] {
        loopSamples_ = samples; loopRestart_ = false;
        for (size_t i=0; i<chase.size(); ++i) instruments_[i].loopEvents.swapWith(chase[i]);
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); apply(); } else apply();
}

void PreviewTransport::seekToTick(juce::int64 tick) {
    if (!running_ || recording_) return;
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); adoptPendingNotes(); }
    else adoptPendingNotes();
    tick = std::clamp(tick, juce::int64(0), scoreTicks_);
    const auto sample = tick == scoreTicks_ ? lastSample_ : std::llround(clock_.secondsAt(tick) * sampleRate_);
    std::array<juce::MidiBuffer, maxTracks> events;
    for (std::size_t i = 0; i < instruments_.size(); ++i)
        if (!instruments_[i].timeline.isEmpty() || instruments_[i].instance)
            events[i] = midiAtSeek(instruments_[i].timeline, static_cast<int>(sample));

    // Timelines are immutable during playback. Only the handover and processor
    // reset need to exclude the device callback.
    const auto apply = [&] {
        for (std::size_t i = 0; i < instruments_.size(); ++i) {
            if (auto* instance = instruments_[i].instance) instance->reset();
            instruments_[i].seekEvents.swapWith(events[i]);
        }
        for (const auto& chain : inserts_)
            for (auto* effect : chain) if (effect) effect->reset();
        sample_ = sample;
        running_ = sample < lastSample_;
    };
    if (devices_) {
        const juce::ScopedLock lock(devices_->getAudioCallbackLock());
        apply();
    } else {
        apply();
    }
}

std::vector<ostfstudio::PitchedNote> PreviewTransport::stop(Cursor* stoppedAt) {
    running_ = false;
    if (devices_) {
        if (!captured_.getNumChannels() && (recording_ || monitorTrack_)) devices_->removeMidiInputDeviceCallback({}, this);
        devices_->removeAudioCallback(this);
        devices_ = nullptr;
    }
    for (auto& track : instruments_) if (track.external) track.external->clearPending();
    for (auto& track : instruments_) {
        if (auto port = std::exchange(track.external, {})) {
            juce::MidiBuffer panic;
            for (int ch = 0; ch < 16; ++ch) if (track.sentChannels[size_t(ch)]) {
                panic.addEvent(juce::MidiMessage::controllerEvent(ch + 1, 64, 0), 0);
                panic.addEvent(juce::MidiMessage::allNotesOff(ch + 1), 0);
                panic.addEvent(juce::MidiMessage::allSoundOff(ch + 1), 0);
            }
            if (!panic.isEmpty()) port->send(panic, 0, sampleRate_);
        }
        if (auto* instance = std::exchange(track.instance, nullptr)) {
            instance->reset();
            instance->releaseResources();
            instance->setPlayHead(nullptr);
        }
    }
    for (auto& chain : inserts_) for (auto*& slot : chain) if (auto* effect = std::exchange(slot, nullptr)) {
        effect->reset();
        effect->releaseResources();
        effect->setPlayHead(nullptr);
    }
    const auto finalCursor = cursor();
    if (stoppedAt) *stoppedAt = finalCursor;
    countRemaining_ = 0;
    const std::scoped_lock lock(recordingMutex_);
    if (recording_) {
        for (std::size_t key = 0; key < pressed_.size(); ++key)
            if (pressed_[key] >= 0) closeRecordedNote(key, elapsed_.load());
    }
    recording_ = false;
    playbackRoutes_.clear();
    monitorTrack_.reset();
    sample_ = 0;
    takeMeters();
    return std::exchange(recorded_, {});
}

PreviewTransport::Cursor PreviewTransport::cursor() const {
    const auto seconds = sampleRate_ > 0 ? sample_.load() / sampleRate_ : 0.0;
    return {std::min(scoreTicks_, static_cast<juce::int64>(clock_.tickAt(seconds) + 1e-9)), seconds};
}

juce::int64 PreviewTransport::positionTick() const { return cursor().tick; }

void PreviewTransport::setChannelMix(std::size_t track, const ostfstudio::ChannelMix& mix, bool audible, double cueDb) {
    if (track >= gains_.size()) return;
    auto& channel = gains_[track];
    channel.audible = audible;
    channel.level = audible ? ostfstudio::gainFromDb(mix.volumeDb) : 0.0f;
    channel.cue = mix.cue ? ostfstudio::gainFromDb(cueDb) : 0.0f;
    channel.pan = mix.pan;
    channel.bus = mix.outputBus >= 0 && mix.outputBus < static_cast<int>(ostfstudio::maxMixerBuses)
        ? static_cast<int>(ostfstudio::busOutput(static_cast<std::size_t>(mix.outputBus))) : 0;
    for (std::size_t i = 0; i < channel.sends.size(); ++i) channel.sends[i] = mix.returnSends[i];
}

void PreviewTransport::setOutputMix(const ostfstudio::MixerRouting& mix) {
    outputGains_[0] = mix.outputMuted[0] ? 0.0f : ostfstudio::gainFromDb(mix.masterDb);
    for (std::size_t i = 0; i < mix.busDb.size(); ++i) {
        const auto slot = ostfstudio::busOutput(i);
        outputGains_[slot] = i < mix.busCount && !mix.outputMuted[slot] ? ostfstudio::gainFromDb(mix.busDb[i]) : 0.0f;
    }
    for (std::size_t i = 0; i < mix.returnDb.size(); ++i)
        outputGains_[3 + i] = i < mix.returnCount && !mix.outputMuted[3 + i] ? ostfstudio::gainFromDb(mix.returnDb[i]) : 0.0f;
}

void PreviewTransport::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device->getCurrentSampleRate() != sampleRate_
        || (pluginAudio_.getNumChannels() > 0 && device->getCurrentBufferSizeSamples() > pluginAudio_.getNumSamples()))
        running_ = false;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> PreviewTransport::getPosition() const {
    PositionInfo position;
    const auto sample = sample_.load();
    position.setTimeInSamples(sample);
    const double seconds = sampleRate_ > 0 ? sample / sampleRate_ : 0.0;
    const double tick = clock_.tickAt(seconds);
    position.setTimeInSeconds(seconds);
    position.setPpqPosition(tick / ppq_);
    const auto linkedTempo = linkBpm_.load();
    position.setBpm(linkedTempo > 0 ? linkedTempo : clock_.bpmAt(static_cast<juce::int64>(tick + 1e-9)));
    const auto meter = ostfstudio::activeMeter(meters_, tick, {0, meterBeats_, beatUnit_});
    position.setTimeSignature(TimeSignature{meter.numerator, meter.denominator});
    const auto barPpq = meterBeats_ * 4.0 / beatUnit_;
    position.setPpqPositionOfLastBarStart(std::floor(tick / ppq_ / barPpq) * barPpq);
    position.setIsPlaying(running_ && !monitorOnly_);
    return position;
}

void PreviewTransport::processEffects(const Chain& effects, juce::AudioBuffer<float>& audio, int count, const juce::MidiBuffer* patches) {
    juce::AudioBuffer<float> stereo(audio.getArrayOfWritePointers(), 2, count);
    for (auto* effect : effects) {
        if (!effect) continue;
        pluginMidi_.clear();
        if (patches && effect->acceptsMidi()) pluginMidi_.addEvents(*patches, 0, -1, 0);
        effect->processBlock(stereo, pluginMidi_);
    }
}

void PreviewTransport::renderInstruments(float* const* output, int channels, int count, int first, int end) {
    for (auto& stem : stems_) stem.clear(0, count);
    const int audible = count;
    const auto timelineStep = double(end - first) / count;
    for (std::size_t track = 0; track < instruments_.size(); ++track) {
        auto& slot = instruments_[track];
        const auto& chain = inserts_[outputCount + track];
        if (!slot.external && !slot.instance && slot.regions.empty() && std::none_of(chain.begin(), chain.end(), [](auto* fx) { return fx != nullptr; })) continue;
        const auto channelCount = slot.instance ? std::max({2, slot.instance->getTotalNumInputChannels(),
                                                            slot.instance->getTotalNumOutputChannels()}) : 2;
        if (channelCount < 1 || channelCount > pluginAudio_.getNumChannels()) continue;
        juce::AudioBuffer<float> block(pluginAudio_.getArrayOfWritePointers(), channelCount, count);
        pluginMidi_.clear();
        if (loopRestart_) pluginMidi_.addEvents(slot.loopEvents, 0, -1, 0);
        pluginMidi_.addEvents(slot.seekEvents, 0, -1, 0);
        slot.seekEvents.clear();
        if (!monitorOnly_) {
            if (end - first == count) pluginMidi_.addEvents(slot.timeline, first, count, -first);
            else if (end > first) {
                for (auto it = slot.timeline.findNextSamplePosition(first); it != slot.timeline.cend(); ++it) {
                    const auto event = *it;
                    if (event.samplePosition >= end) break;
                    const int offset = std::min(count - 1, int((event.samplePosition - first) / timelineStep));
                    pluginMidi_.addEvent(event.data, event.numBytes, offset);
                }
            }
        }
        if (monitorTrack_ == track) {
            const juce::SpinLock::ScopedTryLockType lock(liveLock_);
            if (lock.isLocked()) {
                pluginMidi_.addEvents(liveMessages_, 0, -1, 0);
                liveMessages_.clear();
                queuedLiveMessages_ = 0;
            }
        }
        if (slot.external) {
            patchMidi_.clear();
            if (gains_[track].audible) {
                for (auto event : pluginMidi_) {
                    const int ch = event.getMessage().getChannel() - 1;
                    if (ch < 0 || !slot.externalChannels[size_t(ch)]) continue;
                    slot.sentChannels[size_t(ch)] = true;
                    patchMidi_.addEvent(event.getMessage(), event.samplePosition);
                }
            } else {
                for (int ch = 0; ch < 16; ++ch) if (std::exchange(slot.sentChannels[size_t(ch)], false)) {
                    patchMidi_.addEvent(juce::MidiMessage::controllerEvent(ch + 1, 64, 0), 0);
                    patchMidi_.addEvent(juce::MidiMessage::allSoundOff(ch + 1), 0);
                }
            }
            if (!patchMidi_.isEmpty()) slot.external->send(patchMidi_, blockTimeMs_, sampleRate_);
            continue;
        }
        patchMidi_.clear();
        if (std::any_of(chain.begin(), chain.end(), [](auto* fx) { return fx && fx->acceptsMidi(); })) {
            for (const auto event : pluginMidi_) {
                const auto msg = event.getMessage();
                if (msg.isProgramChange() || msg.isControllerOfType(0) || msg.isControllerOfType(32))
                    patchMidi_.addEvent(msg, event.samplePosition);
            }
        }
        block.clear();
        if (slot.instance) {
            juce::AudioBuffer<float> pluginBlock(block.getArrayOfWritePointers(),
                std::max(slot.instance->getTotalNumInputChannels(), slot.instance->getTotalNumOutputChannels()), count);
            slot.instance->processBlock(pluginBlock, pluginMidi_);
            if (slot.instance->getMainBusNumOutputChannels() == 1) block.copyFrom(1, 0, block, 0, 0, count);
        }
        for (const auto& region : slot.regions) region.mixInto(block, count, first, timelineStep, sampleRate_, clock_);
        processEffects(chain, block, count, &patchMidi_);
        const auto& mix = gains_[track];
        const auto basePan = mix.pan.load();
        const bool automated = !slot.panEnvelope.points.empty();
        if (automated) {
            for (int i = 0; i < audible; ++i) {
                const auto tick = clock_.tickAt((first + i * timelineStep) / sampleRate_);
                const auto pan = float(slot.panEnvelope.valueAt(tick, basePan));
                for (int ch = 0; ch < 2; ++ch) block.getWritePointer(ch)[i] *= ostfstudio::stereoBalance(pan, ch == 1);
            }
        }
        for (int channel = 0; channel < 2; ++channel) {
            const auto balance = automated ? 1.f : ostfstudio::stereoBalance(basePan, channel == 1);
            const auto gain = mix.level.load() * balance;
            const auto* data = block.getReadPointer(channel);
            trackMeters_[track].publish(channel, block.getMagnitude(channel, 0, audible) * gain);
            stems_[static_cast<std::size_t>(mix.bus.load())].addFrom(channel, 0, data, audible, gain);
            for (std::size_t r = 0; r < mix.sends.size(); ++r) {
                const auto send = mix.sends[r].load();
                if (send != 0.0f) stems_[3 + r].addFrom(channel, 0, data, audible, gain * send);
            }
            if (channels >= 4 && output[channel + 2])
                juce::FloatVectorOperations::addWithMultiply(output[channel + 2], data, mix.cue.load() * balance, audible);
        }
    }
    for (std::size_t i = 1; i < stems_.size(); ++i) {
        const auto gain = outputGains_[i].load();
        processEffects(inserts_[i], stems_[i], count);
        if (gain == 0.0f) continue;
        for (int c = 0; c < 2; ++c) {
            outputMeters_[i].publish(c, stems_[i].getMagnitude(c, 0, audible) * gain);
            stems_[0].addFrom(c, 0, stems_[i], c, 0, audible, gain);
        }
    }
    processEffects(inserts_[0], stems_[0], count);
    const float masterGain = outputGains_[0].load();
    for (int c = 0; c < 2; ++c) {
        outputMeters_[0].publish(c, stems_[0].getMagnitude(c, 0, audible) * masterGain);
        if (c < channels && output[c])
            juce::FloatVectorOperations::addWithMultiply(output[c], stems_[0].getReadPointer(c), masterGain, audible);
        if (channels >= 4 && output[c + 2] && audible > 0) {
            auto range = juce::FloatVectorOperations::findMinAndMax(output[c + 2], audible);
            cueMeter_.publish(c, std::max(std::abs(range.getStart()), std::abs(range.getEnd())));
        }
    }
}

ostfstudio::MeterFrame PreviewTransport::takeMeters() noexcept {
    ostfstudio::MeterFrame frame;
    for (std::size_t i = 0; i < trackMeters_.size(); ++i) frame.tracks[i] = trackMeters_[i].take();
    for (std::size_t i = 0; i < outputMeters_.size(); ++i) frame.outputs[i] = outputMeters_[i].take();
    frame.cue = cueMeter_.take();
    return frame;
}

void PreviewTransport::audioDeviceIOCallbackWithContext(const float* const* input, int inputs, float* const* output, int channels,
                                                        int count, const juce::AudioIODeviceCallbackContext&) {
    renderBlock(output, channels, count, input, inputs);
}

void PreviewTransport::renderBlock(float* const* output, int channels, int count, const float* const* input, int inputs) {
    for (int c = 0; c < channels; ++c)
        if (output[c]) juce::FloatVectorOperations::clear(output[c], count);
    if (!running_) return;
    if (pluginAudio_.getNumChannels() && count > pluginAudio_.getNumSamples()) { running_ = false; return; }
    const auto blockStart = juce::Time::getMillisecondCounterHiRes() + 1.0;
    blockTimeMs_ = blockStart;
    if (monitorOnly_) {
        if (pluginAudio_.getNumChannels()) renderInstruments(output, channels, count, int(sample_.load()), int(sample_.load()) + count);
        return;
    }
    int done = 0;
    if (launchClock_) {
        const auto remaining = (launchMicros_ - launchClock_->micros() - outputLatencyMicros_) * sampleRate_ / 1.0e6;
        if (remaining >= count) return;
        done = std::clamp(juce::roundToInt(remaining), 0, count);
        launchClock_ = nullptr;
    }
    double speed = 1.0;
    if (playbackLink_ && countRemaining_.load() == 0) {
        const auto audibleTime = playbackLink_->micros() + std::llround(outputLatencyMicros_ + done * 1.e6 / sampleRate_);
        const auto pulse = playbackLink_->audioPulse(audibleTime, linkQuantum_);
        linkBpm_ = pulse.tempo;
        const double framesPerBeat = sampleRate_ * 60.0 / linkBaseTempo_;
        const double localBeat = linkStartBeat_ + (elapsed_.load() + linkSubsample_) / framesPerBeat;
        const double phaseError = std::remainder(pulse.beat - localBeat, linkQuantum_);
        speed = std::clamp(pulse.tempo / linkBaseTempo_ + std::clamp(phaseError * .2, -.025, .025), .01, 64.);
    }
    while (done < count && running_) {
        if (const auto remaining = countRemaining_.load(); remaining > 0) {
            const auto played = countLength_ - remaining;
            const auto age = played % countBeat_;
            const int n = int(std::min<juce::int64>({count - done, remaining, countBeat_ - age}));
            mixClick(output, channels, done, n, int(age), played % countBar_ < countBeat_);
            countRemaining_ -= n;
            done += n;
            continue;
        }
        auto first = sample_.load();
        if (!loopSamples_.isEmpty() && first >= loopSamples_.getEnd()) {
            adoptPendingNotes();
            first = loopSamples_.getStart(); sample_ = first; loopRestart_ = true;
        }
        auto limit = loopSamples_.isEmpty() ? lastSample_ : juce::int64(loopSamples_.getEnd());
        if (captured_.getNumChannels()) limit = std::min(limit, juce::int64(captureOrigin_ + captured_.getNumSamples()));
        const auto available = limit - first;
        if (available <= 0) { running_ = false; break; }
        const int part = std::min(count - done, int(std::min<double>(count, std::ceil((available - linkSubsample_) / speed))));
        if (part <= 0) { running_ = false; break; }
        const double advance = part * speed + linkSubsample_;
        const int timelineFrames = int(std::min<double>(available, std::floor(advance)));
        linkSubsample_ = timelineFrames == available ? 0.0 : advance - timelineFrames;
        std::array<float*, 4> dest {};
        const int outs = std::min(channels, int(dest.size()));
        for (int c=0; c<outs; ++c) dest[c] = output[c] ? output[c] + done : nullptr;
        blockTimeMs_ = blockStart + done * 1000.0 / sampleRate_;
        if (pluginAudio_.getNumChannels()) renderInstruments(dest.data(), outs, part, int(first), int(first) + timelineFrames);
        if (captured_.getNumChannels()) {
            const int pos = int(first) - captureOrigin_;
            const int channelsNeeded = captured_.getNumChannels();
            if (!input || firstInputBuffer_ + channelsNeeded > inputs || !input[firstInputBuffer_]
                || (channelsNeeded == 2 && !input[firstInputBuffer_ + 1])) { running_ = false; break; }
            const auto gain = captureGain_.load();
            for (int c = 0; c < channelsNeeded; ++c)
                captured_.copyFrom(c, pos, input[firstInputBuffer_ + c] + done, part, gain);
            captureFirst_ = std::min(captureFirst_, pos);
            captureEnd_ = std::max(captureEnd_, pos + part);
        }
        if (metronome) {
            const int clickLength = std::max(1, int(sampleRate_ * .025));
            auto beat = std::lower_bound(beatSamples_.begin(), beatSamples_.end(), int(first) - clickLength,
                [](const auto& b, int x) { return b.first < x; });
            for (; beat != beatSamples_.end() && beat->first < first + timelineFrames; ++beat) {
                const int offset = std::clamp(int((beat->first - first) / speed), 0, part);
                mixClick(dest.data(), outs, offset, part - offset, std::max(0, int((first - beat->first) / speed)), beat->second);
            }
        }
        elapsed_ += timelineFrames;
        done += part; sample_ = first + timelineFrames; loopRestart_ = false;
        if (loopSamples_.isEmpty() && sample_ >= lastSample_) running_ = false;
    }
    for (int c = 0; c < channels; ++c)
        if (output[c])
            for (int i = 0; i < count; ++i) output[c][i] = juce::jlimit(-1.0f, 1.0f, output[c][i]);

}

void PreviewTransport::handleIncomingMidiMessage(juce::MidiInput* input, const juce::MidiMessage& message) {
    inputFromDevice(message, input ? input->getIdentifier() : juce::String{});
}

void PreviewTransport::inputFromDevice(const juce::MidiMessage& message, const juce::String& port) {
    if (!midiInput_.accepts(port, message.getChannel())) return;
    if (controlPort_.isNotEmpty() && port == controlPort_) return;
    auto mapped = message;
    if (mapInput_ && (message.isNoteOn() || message.isNoteOff())) mapped.setNoteNumber(inputMap_[message.getNoteNumber()]);
    inputNote(mapped);
}

void PreviewTransport::inputNote(const juce::MidiMessage &message) {
    const bool note = message.isNoteOnOrOff();
    if (!running_ || countingIn() || (!recording_ && !monitorTrack_) || captured_.getNumChannels() ||
        message.getChannel() <= 0 || (!note && !message.isController() && !message.isPitchWheel() && !message.isChannelPressure() && !message.isAftertouch() && !message.isProgramChange()) || (note && !inputPitches_[message.getNoteNumber()]))
        return;
    if (monitorTrack_) {
        const juce::SpinLock::ScopedLockType lock(liveLock_);
        auto routed = message;
        routed.setChannel(monitorChannel_);
        if (queuedLiveMessages_ >= 1024) {
            liveMessages_.clear();
            queuedLiveMessages_ = 0;
            liveMessages_.addEvent(juce::MidiMessage::allSoundOff(monitorChannel_), 0);
        }
        liveMessages_.addEvent(routed, 0);
        ++queuedLiveMessages_;
    }
    if (!recording_ || !note) return;
    const auto elapsed = elapsed_.load();
    const auto key = (message.getChannel() - 1) * 128 + message.getNoteNumber();
    const std::scoped_lock lock(recordingMutex_);
    auto& start = pressed_[static_cast<std::size_t>(key)];
    if (start >= 0) closeRecordedNote(size_t(key), elapsed);
    start = message.isNoteOn() ? elapsed : -1;
    if (message.isNoteOn()) pressedVelocity_[static_cast<std::size_t>(key)] = message.getVelocity();
}

juce::Range<juce::int64> PreviewTransport::recordedRange() const {
    auto first = takeStart_, last = first + elapsed_.load();
    if (!loopSamples_.isEmpty() && last > loopSamples_.getEnd()) {
        first = std::min(first, juce::int64(loopSamples_.getStart()));
        last = loopSamples_.getEnd();
    }
    return {std::llround(clock_.tickAt(first / sampleRate_)), std::llround(clock_.tickAt(last / sampleRate_))};
}

void PreviewTransport::closeRecordedNote(size_t key, juce::int64 elapsed) {
    auto begin = takeStart_ + pressed_[key];
    const auto end = takeStart_ + std::max(pressed_[key] + 1, elapsed);
    const auto cycleEnd = loopSamples_.getEnd();
    while (begin < end && recorded_.size() < 10000) {
        const auto pos = !loopSamples_.isEmpty() && begin >= cycleEnd
            ? loopSamples_.getStart() + (begin - cycleEnd) % loopSamples_.getLength() : begin;
        const auto limit = loopSamples_.isEmpty() ? lastSample_ : juce::int64(cycleEnd);
        const auto length = std::min(end - begin, limit - pos);
        if (length <= 0) break;
        const auto a = juce::int64(std::llround(clock_.tickAt(pos / sampleRate_)));
        const auto b = juce::int64(std::llround(clock_.tickAt((pos + length) / sampleRate_)));
        if (a < scoreTicks_) recorded_.push_back({a, std::min(scoreTicks_, std::max(a + 1, b)), int(key % 128), pressedVelocity_[key]});
        begin += length;
    }
}

juce::Result PreviewTransport::prepareTake(const TakeOptions& options) {
    jassert(!devices_);
    takeStart_ = sample_.load();
    controlPort_ = options.controlPort;
    midiInput_ = options.midiInput;
    if (options.drumMapping) { inputMap_ = *options.drumMapping; mapInput_ = true; }
    if (options.inputChannels) {
        if (options.firstInputBuffer < 0 || options.firstInputBuffer > 63 || !std::isfinite(options.inputGainDb)
            || options.inputGainDb < -60 || options.inputGainDb > 24) return juce::Result::fail("Invalid recording input or gain.");
        firstInputBuffer_ = options.firstInputBuffer;
        setInputGain(options.inputGainDb);
        if (options.inputChannels < 1 || options.inputChannels > 2) return juce::Result::fail("Choose mono or stereo.");
        recording_ = true;
        const int maxFrames = (16 * 1024 * 1024 - 4096) / (2 * options.inputChannels);
        captureOrigin_ = loopSamples_.isEmpty() ? int(takeStart_) : std::min(int(takeStart_), loopSamples_.getStart());
        const int span = (loopSamples_.isEmpty() ? int(lastSample_) : loopSamples_.getEnd()) - captureOrigin_;
        if (span <= 0 || (!loopSamples_.isEmpty() && span > maxFrames)) return juce::Result::fail("Choose a shorter audio recording range.");
        captured_.setSize(options.inputChannels, std::min(maxFrames, span));
        captureFirst_ = captured_.getNumSamples();
        captureEnd_ = 0;
    }
    if (options.countInBars > 0) {
        const auto tick = positionTick();
        const auto meter = ostfstudio::activeMeter(meters_, double(tick), {0, meterBeats_, beatUnit_});
        countBeat_ = std::max(1LL, std::llround(sampleRate_ * 60.0 / clock_.bpmAt(tick) * 4 / meter.denominator));
        countBar_ = countBeat_ * meter.numerator;
        countLength_ = countBar_ * std::clamp(options.countInBars, 1, 8);
        countRemaining_ = countLength_;
    }
    return juce::Result::ok();
}

void PreviewTransport::mixClick(float* const* output, int channels, int offset, int count, int age, bool accent) const {
    const int length = std::max(1, int(sampleRate_ * .025));
    const double freq = std::min(sampleRate_ * .22, accent ? 1500.0 : 1000.0);
    for (int i = 0; i < count && age + i < length; ++i) {
        const float click = float(.20 * (1.0 - double(age + i) / length)
            * std::sin(juce::MathConstants<double>::twoPi * freq * (age + i) / sampleRate_));
        for (int c = 0; c < std::min(2, channels); ++c) if (output[c]) output[c][offset + i] += click;
    }
}

std::optional<ostfstudio::ProjectAudio> PreviewTransport::finishAudioTake(size_t track) {
    jassert(!devices_);
    if (captureFirst_ >= captureEnd_ || !captured_.getNumChannels()) return {};
    ostfstudio::ProjectAudio clip;
    clip.track = track;
    clip.path = "Recording-" + juce::Uuid().toString() + ".wav";
    const int start = captureOrigin_ + captureFirst_, end = captureOrigin_ + captureEnd_;
    clip.startTick = juce::int64(std::llround(clock_.tickAt(start / sampleRate_)));
    clip.durationTicks = std::max<juce::int64>(1, std::llround(clock_.tickAt(end / sampleRate_)) - clip.startTick);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(clip.embedded.writable(), false);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate_)
        .withNumChannels(captured_.getNumChannels()).withBitsPerSample(16));
    if (!writer || !writer->writeFromAudioSampleBuffer(captured_, captureFirst_, end - start)) return {};
    writer.reset();
    captured_.setSize(0, 0);
    return clip;
}
