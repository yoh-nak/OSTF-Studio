#include "ui/PreviewTransport.h"
#include "plugins/studio/HostAudio.h"
#include "ui/MasterSpectrum.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include "../plugins/instruments/SingingScore.h"

PreviewTransport::PreviewTransport() : masterSpectrum_(std::make_unique<MasterSpectrum>()) {}
bool PreviewTransport::readMasterSpectrum(std::array<float,31>& bands) { return masterSpectrum_->read(bands); }
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
    return !effect.getPluginDescription().isInstrument && effect.getMainBusNumInputChannels()==2
        && effect.getTotalNumOutputChannels()==2 && effect.getBusCount(true)<=2
        && effect.getTotalNumInputChannels()<=4;
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
    if (record && options) for (const auto& lane : options->audioTracks)
        if (lane.first<0 || lane.width<1 || lane.width>2 || lane.first+lane.width>device->getActiveInputChannels().countNumberOfSetBits())
            return juce::Result::fail("Enable the required audio input channels in Settings.");
    if (inputs && (options->firstInputBuffer < 0 || options->firstInputBuffer + inputs > device->getActiveInputChannels().countNumberOfSetBits()))
        return juce::Result::fail("Enable the required audio input channels in Settings.");
    if (record && !inputs && (!drumInput || (options && options->midiInput.port.isNotEmpty()))) {
        bool enabled = false;
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            enabled = enabled || (devices.isMidiInputDeviceEnabled(input.identifier) && (!options ||
                (options->midiInput.enabled && (options->midiInput.port.isEmpty() || options->midiInput.port == input.identifier))));
        if (!enabled && device->getTypeName() != "DAW") return juce::Result::fail("Enable a MIDI input in Settings before recording.");
    }
    const auto result = preparePlayback(project, tracks, inserts, device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), semitones, tempoRate, monitorOnly, options ? std::span<const ostfstudio::TakeSelection>(options->auditions) : std::span<const ostfstudio::TakeSelection>{});
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
    if (options && !options->sections.empty()) {
        auto plan=setLiveSections(options->sections);
        if (plan.failed()) { stop(); return plan; }
        for(const auto& cue:options->cues) if(!enqueueCue(cue,&project)) {
            stop(); return juce::Result::fail("Could not prepare the queued section.");
        }
        launchFirstCue_=!options->cues.empty();
    }
    if (options && !options->cycle.isEmpty()) setLoop(options->cycle);
    if (inputTrack || (!inputs && record)) prepareRecording(project, tracks, inputTrack, record);
    if (options && (record || inputTrack || options->countInBars>0)) {
        if (const auto armed = prepareTake(*options); armed.failed()) { stop(); return armed; }
    }
    devices_ = &devices;
    performanceTick_.store(monitorOnly ? -1 : positionTick());
    devices.addAudioCallback(this);
    if (monitorTrack_ || (!inputs && record)) devices.addMidiInputDeviceCallback({}, this);
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
    liveHeld_.fill(false);livePedal_=false;
}

juce::Result PreviewTransport::preparePlayback(const ostfstudio::OstfpPreview& project, std::span<const TrackSetup> tracks,
                                             const Inserts& inserts, double sampleRate, int blockSize, int semitones, double tempoRate, bool monitorOnly, std::span<const ostfstudio::TakeSelection> auditions) {
    stop();
    monitorOnly_ = monitorOnly;
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || blockSize < 1)
        return juce::Result::fail("Invalid audio configuration");
    sampleRate_ = sampleRate;
    for (auto& pair : vu_) for (auto& channel : pair) channel.prepare(sampleRate);
    for (auto& pair : outputVu_) for (auto& channel : pair) channel.prepare(sampleRate);
    for (auto& channel : cueVu_) channel.prepare(sampleRate);
    masterMeters_.prepare(sampleRate, 2);
    masterSpectrum_->prepare(sampleRate);
    historyCountdown_ = std::max(1, juce::roundToInt(sampleRate * .5));
    liveVu_ = {};
    for (auto& meter : masterVu_) meter.prepare(sampleRate);
    phaseDecimation_ = 0; scopeFrame_ = {};
    { const juce::SpinLock::ScopedLockType guard(masterPublishLock_); publishedMaster_ = {}; publishedVu_ = {}; }
    if (project.clock.empty()) return juce::Result::fail("The song needs an initial tempo.");
    if (!monitorOnly) {
        if (auto check = project.checkAudition(semitones, tempoRate); check.failed()) return check;
    }
    const auto scaled = project.clock.scaled(tempoRate);
    if (!scaled) return juce::Result::fail("Invalid tempo rate");
    playbackTempoRate_ = tempoRate;
    clock_ = *scaled;
    ppq_ = project.ppq;
    meters_ = project.meters;
    meterBeats_ = project.meterBeats;
    beatUnit_ = project.beatUnit;
    scoreTicks_ = project.playbackEndTick();
    const auto total = clock_.secondsAt(scoreTicks_) * sampleRate_;
    if (!std::isfinite(total) || total < 1.0 || total > sampleRate_ * 3600.0
        || total > std::numeric_limits<int>::max() - 1)
        return juce::Result::fail("Preview transport supports songs up to one hour.");
    liveBoundaries_.clear(); liveStarts_.clear(); liveEnds_.clear(); stagedStarts_.clear(); stagedEnds_.clear();
    liveIndex_=0; liveLinearReplay_=false; liveWaiting_=false; liveEntered_=false;
    soundingRevision_=0; pendingRevision_=0; queuedSection_=-1;
    cues_.clear(); consumedCues_=0; playingCue_.reset(); revisedCue_.reset(); launchFirstCue_=false;
    loopSamples_ = {}; loopRestart_ = false;
    captured_.setSize(0, 0);
    captureLanes_.clear();
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
    int outputChannels = std::any_of(tracks.begin(),tracks.end(),[](const auto& t){return t.monitorInput>=0;})
        || std::find(project.trackKinds.begin(),project.trackKinds.end(),ostfstudio::TrackKind::audio)!=project.trackKinds.end() ? 2 : 0;
    for (auto& slot : instruments_) {
        slot.timeline.clear();
        slot.seekEvents.clear();
        slot.regions.clear();
    }
    ostfstudio::AudioRegionPlayback::SourceCache sources;
    if (!monitorOnly) for (const auto& audio : project.audio) {
        if (audio.track >= instruments_.size() || (audio.track<tracks.size() && (!tracks[audio.track].playSource ||
            (!tracks[audio.track].onlyClips.isEmpty() && !tracks[audio.track].onlyClips.contains(audio.regionId))))) continue;
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
        if (auto result = makeMidiTimelines(project, tracks, semitones, timelines, false, auditions); result.failed()) return result;
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
        keyScratch_.setSize(2,blockSize);
        for(auto& track:dryTracks_) track.setSize(2,inserts.keyTracks.empty() ? 0 : blockSize);
        for(auto& midi:insertPatches_) midi.ensureSize(4096);
        for (auto& stem : stems_) stem.setSize(2, blockSize);
        cueBus_.setSize(2, blockSize);
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
            if (auto* key = effect->getBus(true,1)) {
                if (!key->enable(inserts_.keyTracks.contains(effect)) || !supportsInsert(*effect))
                    return juce::Result::fail("Unsupported sidechain layout");
            }
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
                                                int semitones, Timelines& timelines, bool patchesOnly, std::span<const ostfstudio::TakeSelection> auditions, const ostfstudio::TempoMap* renderClock) const {
    const auto& noteClock = renderClock ? *renderClock : clock_;
    std::optional<ostfstudio::singing::Lyrics> lyrics;
    int noteCount = 0;
    std::array<bool, maxTracks> sharedHosts{};
    for (const auto& route : tracks)
        if (route.outputTrack && *route.outputTrack < sharedHosts.size())
            sharedHosts[*route.outputTrack] = true;
    for (std::size_t track = 0; track < std::min(project.pitchedNotes.size(), instruments_.size()); ++track) {
        const auto setup = track < tracks.size() ? tracks[track] : TrackSetup{};
        if (!setup.playSource) continue;
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
                else midi->addEvent(msg, juce::roundToInt(noteClock.secondsAt(p.tick) * sampleRate_));
            }
        }
        if (track < project.programChanges.size()) for (const auto& pc : project.programChanges[track])
            patches.push_back({pc.tick, pc.order, pc.ordinal, juce::MidiMessage::programChange(sharedPart ? setup.midiChannel : pc.channel.value_or(setup.midiChannel), pc.program)});
        std::stable_sort(patches.begin(), patches.end(), [](const auto& a, const auto& b) {
            return std::tie(a.tick, a.order, a.ordinal) < std::tie(b.tick, b.order, b.ordinal);
        });
        for (const auto& e : patches) midi->addEvent(e.message, juce::roundToInt(noteClock.secondsAt(e.tick) * sampleRate_));
        if (!instrument || patchesOnly) continue;
        auto addNote = [&](juce::int64 start, juce::int64 end, int pitch, int velocity, int channel) {
            if (noteCount >= 20000) return false;
            const auto first = static_cast<int>(std::llround(noteClock.secondsAt(start) * sampleRate_));
            const auto last = static_cast<int>(std::llround(noteClock.secondsAt(end) * sampleRate_));
            if (last <= first) return true;
            ++noteCount;
            midi->addEvent(juce::MidiMessage::noteOn(channel, pitch, static_cast<juce::uint8>(velocity)), first);
            midi->addEvent(juce::MidiMessage::noteOff(channel, pitch), last);
            return true;
        };
        const int shift = track < project.trackKinds.size() && project.trackKinds[track] != ostfstudio::TrackKind::pitched ? 0 : semitones;
        const auto audible = [&](const ostfstudio::PitchedNote& note) {
            if (!setup.onlyClips.isEmpty() && !setup.onlyClips.contains(note.clipId)) return false;
            for (const auto& c : auditions) if (c.track==track && c.ticks.contains(note.startTick)) {
                const auto& state=project.takes(track);
                return state.takeOf(note.id)==c.number && (note.enabled || state.suppressed.contains(note.id));
            }
            return note.enabled;
        };
        if (tracks[outputTrack].instrument && tracks[outputTrack].instrument->getName() == "OSTF Formant Singer") {
            if (!lyrics) lyrics = ostfstudio::singing::lyricsFor(juce::JSON::parse(project.sourceScore.toString()));
            for (const auto& n : project.pitchedNotes[track]) if (audible(n)) {
                const int key=n.pitch+(n.fixedTrigger?0:shift);
                if (key<0 || key>127) continue;
                const auto entry=lyrics->find(n.id);
                if (entry == lyrics->end()) {
                    midi->addEvent(ostfstudio::singing::phoneMessage(setup.midiChannel,key,{}),juce::roundToInt(noteClock.secondsAt(n.startTick)*sampleRate_));
                } else {
                    const auto start = noteClock.secondsAt(n.startTick);
                    const auto duration = noteClock.secondsAt(n.endTick) - start;
                    const auto edges = ostfstudio::singing::articulationPositions(entry->second, duration);
                    for (size_t p = 0; p < entry->second.size(); ++p)
                        midi->addEvent(ostfstudio::singing::phoneMessage(setup.midiChannel, key, entry->second[p],
                                       duration * (edges[p+1] - edges[p])),
                                       juce::roundToInt((start + edges[p] * duration) * sampleRate_));
                }
            }
        }
        for (const auto& note : project.pitchedNotes[track])
            if (audible(note) && !addNote(note.startTick, note.endTick, note.pitch + (note.fixedTrigger ? 0 : shift), note.velocity.value_or(92), setup.midiChannel))
                return juce::Result::fail("Preview transport supports up to 20,000 notes.");
        if (track < project.drumNotes.size())
            for (const auto& note : project.drumNotes[track])
                if (audible(note) && !addNote(note.startTick, note.endTick, note.pitch, note.velocity.value_or(92),
                                             sharedPart || (outputTrack < tracks.size() && tracks[outputTrack].external) ? setup.midiChannel : 10))
                    return juce::Result::fail("Preview transport supports up to 20,000 notes.");
    }
    return juce::Result::ok();
}

void PreviewTransport::adoptPendingNotes() {
    if (!pendingNotes_) return;
    soundingRevision_.store(pendingRevision_);
    for (std::size_t i = 0; i < instruments_.size(); ++i) {
        instruments_[i].timeline.swapWith(pendingTimelines_[i]);
        instruments_[i].loopEvents.swapWith(pendingLoopEvents_[i]);
    }
    if (!stagedStarts_.empty()) { liveStarts_.swap(stagedStarts_); liveEnds_.swap(stagedEnds_); }
    pendingNotes_ = false;
}

juce::Result PreviewTransport::updateLoopNotes(const ostfstudio::OstfpPreview& project, std::span<const ostfstudio::TakeSelection> auditions, unsigned revision) {
    if ((loopSamples_.isEmpty() && liveBoundaries_.empty()) || !running_) return juce::Result::fail("Loop playback ended.");
    Timelines next, chase;
    if (auto result = makeMidiTimelines(project, playbackRoutes_, playbackShift_, next, false, auditions); result.failed()) return result;
    for (std::size_t i = 0; i < instruments_.size(); ++i)
        if (!next[i].isEmpty() || instruments_[i].instance) chase[i] = midiAtSeek(next[i],loopSamples_.getStart());
    std::vector<Timelines> starts(liveBoundaries_.size()), ends(liveBoundaries_.size());
    for (size_t s=0;s<liveBoundaries_.size();++s) {
        for (size_t t=0;t<std::min(playbackRoutes_.size(),instruments_.size());++t) {
            if (!playbackRoutes_[t].instrument && !playbackRoutes_[t].external) continue;
            starts[s][t]=midiAtSeek(next[t],liveBoundaries_[s].samples.getStart());
            ends[s][t]=midiAtSeek(next[t],liveBoundaries_[s].samples.getEnd());
        }
    }
    std::vector<Cue> choices;
    const auto snapshot=[&] {
        if (playingCue_ && playingCue_->events) choices.push_back(playingCue_->cue);
        for (size_t i=consumedCues_;i<cues_.size();++i) if (cues_[i]->events) choices.push_back(cues_[i]->cue);
    };
    if (devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());snapshot();} else snapshot();
    std::vector<std::unique_ptr<PreparedCue>> replacements;
    for (auto& choice:choices) {
        auto prepared=std::make_unique<PreparedCue>();
        prepared->cue=std::move(choice);
        if (auto r=prepareCueEvents(*prepared,project);r.failed()) return r;
        replacements.push_back(std::move(prepared));
    }
    const auto publish = [&] {
        for (auto& replacement:replacements) {
            if (playingCue_ && replacement->cue.serial==playingCue_->cue.serial) {
                revisedCue_.swap(replacement);
                continue;
            }
            for (size_t i=consumedCues_;i<cues_.size();++i) if (replacement->cue.serial==cues_[i]->cue.serial) {
                replacement->events.swap(cues_[i]->events);
                replacement->chase.swap(cues_[i]->chase);
                break;
            }
        }
        stagedStarts_.swap(starts); stagedEnds_.swap(ends);
        for (std::size_t i = 0; i < instruments_.size(); ++i) {
            next[i].swapWith(pendingTimelines_[i]);
            chase[i].swapWith(pendingLoopEvents_[i]);
        }
        pendingRevision_=revision;
        pendingNotes_ = true;
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); publish(); }
    else publish();
    return juce::Result::ok();
}

juce::Result PreviewTransport::extendLiveSong(const ostfstudio::OstfpPreview& project,
                                             const std::vector<LiveSection>& sections,
                                             std::span<const ostfstudio::TakeSelection> auditions, unsigned revision) {
    if (recording_ || liveBoundaries_.empty() || sections.size() <= liveBoundaries_.size())
        return juce::Result::fail("Live section list changed.");
    auto nextClock = project.clock.scaled(playbackTempoRate_);
    if (!nextClock) return juce::Result::fail("Invalid tempo rate");
    const auto ticks = project.playbackEndTick();
    const auto frames = std::ceil(nextClock->secondsAt(ticks) * sampleRate_);
    if (!std::isfinite(frames) || frames <= lastSample_ || frames > sampleRate_ * 3600 || frames >= std::numeric_limits<int>::max())
        return juce::Result::fail("Invalid live song duration.");
    Timelines next;
    if (auto result = makeMidiTimelines(project,playbackRoutes_,playbackShift_,next,false,auditions,&*nextClock); result.failed()) return result;
    std::vector<LiveBoundary> boundaries;
    std::vector<Timelines> starts(sections.size()), ends(sections.size());
    juce::int64 end = 0;
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        if (i < liveBoundaries_.size() && (section.id != liveBoundaries_[i].source.id || section.ticks != liveBoundaries_[i].source.ticks))
            return juce::Result::fail("Live append must preserve existing sections.");
        if (section.ticks.isEmpty() || section.ticks.getStart() < end || section.ticks.getEnd() > ticks)
            return juce::Result::fail("Invalid live section range.");
        end = section.ticks.getEnd();
        auto& b = boundaries.emplace_back();
        b.source = section;
        b.remaining = section.repeats;
        b.samples = {juce::roundToInt(nextClock->secondsAt(section.ticks.getStart()) * sampleRate_),
                     juce::roundToInt(nextClock->secondsAt(end) * sampleRate_)};
        for (size_t t=0; t<playbackRoutes_.size() && t<instruments_.size(); ++t) {
            if (!playbackRoutes_[t].instrument && !playbackRoutes_[t].external) continue;
            starts[i][t] = midiAtSeek(next[t],b.samples.getStart());
            ends[i][t] = midiAtSeek(next[t],b.samples.getEnd());
        }
    }
    std::array<std::vector<ostfstudio::AudioRegionPlayback>,maxTracks> regions;
    ostfstudio::AudioRegionPlayback::SourceCache sources;
    for (const auto& audio : project.audio) if (audio.track < regions.size()) {
        auto& region = regions[audio.track].emplace_back();
        if (auto result = region.load(audio,*nextClock,sampleRate_,int(frames),&sources); result.failed()) return result;
    }
    auto meters = project.meters;
    auto beats = beatSamples_;
    for (auto bar = (scoreTicks_/project.ticksPerBar)*project.ticksPerBar; bar < ticks; bar += project.ticksPerBar) {
        const auto meter = ostfstudio::activeMeter(meters,double(bar),{0,meterBeats_,beatUnit_});
        for (int beat = 0; beat < meter.numerator; ++beat) {
            const auto tick = bar + std::llround(beat * 4.0 * ppq_ / meter.denominator);
            if (tick >= scoreTicks_ && tick < ticks) beats.emplace_back(juce::roundToInt(nextClock->secondsAt(tick)*sampleRate_),beat==0);
        }
    }
    const auto install = [&] {
        for (size_t i=0; i<liveBoundaries_.size(); ++i) boundaries[i].remaining=liveBoundaries_[i].remaining;
        liveBoundaries_.swap(boundaries);
        stagedStarts_.swap(starts); stagedEnds_.swap(ends);
        for (size_t i=0; i<instruments_.size(); ++i) {
            next[i].swapWith(pendingTimelines_[i]);
            regions[i].swap(instruments_[i].regions);
        }
        std::swap(*nextClock,clock_);
        beatSamples_.swap(beats); meters_.swap(meters);
        scoreTicks_=ticks; lastSample_=juce::int64(frames);
        pendingRevision_=revision; pendingNotes_=true;
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); install(); } else install();
    return juce::Result::ok();
}

juce::Result PreviewTransport::setLiveSections(const std::vector<LiveSection>& sections) {
    std::vector<LiveBoundary> prepared;
    std::vector<Timelines> starts;
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); adoptPendingNotes(); } else adoptPendingNotes();
    juce::int64 endTick=0;
    for (const auto& section:sections) {
        if (section.ticks.isEmpty() || section.ticks.getStart()<endTick || section.ticks.getEnd()>scoreTicks_
            || section.repeats<0 || section.repeats>99) return juce::Result::fail("Invalid live section range or repeat count.");
        endTick=section.ticks.getEnd();
        auto& boundary=prepared.emplace_back();
        boundary.source=section;
        boundary.samples={juce::roundToInt(clock_.secondsAt(section.ticks.getStart())*sampleRate_),
                          juce::roundToInt(clock_.secondsAt(endTick)*sampleRate_)};
        if (boundary.samples.isEmpty()) return juce::Result::fail("Live section is shorter than one audio sample.");
        boundary.remaining=section.repeats;
        auto& chase=starts.emplace_back();
        for (size_t i=0;i<instruments_.size();++i) if (!instruments_[i].timeline.isEmpty())
            chase[i]=midiAtSeek(instruments_[i].timeline,boundary.samples.getStart());
    }
    const auto install=[&] {
        liveBoundaries_.swap(prepared); liveStarts_.swap(starts);
        queuedSection_=-1;
        cues_.clear(); consumedCues_=0; playingCue_.reset(); launchFirstCue_=false;
        liveLinearReplay_=false; loopRestart_=false; liveWaiting_=false; liveEntered_=false;
        liveIndex_=0;
        while(liveIndex_<liveBoundaries_.size() && liveBoundaries_[liveIndex_].samples.getEnd()<=sample_.load()) ++liveIndex_;
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); install(); } else install();
    return juce::Result::ok();
}
bool PreviewTransport::queueLiveSection(size_t index) {
    if(index>=liveBoundaries_.size()) return false;
    return enqueueCue({index,liveBoundaries_[index].source.repeats,{}});
}
juce::Result PreviewTransport::prepareCueEvents(PreparedCue& prepared,const ostfstudio::OstfpPreview& score) const {
    Timelines all;
    std::unique_ptr<ostfstudio::OstfpPreview> alternate;
    const auto& choices = prepared.cue.comps;
    const bool changed = std::any_of(choices.begin(), choices.end(), [&](const auto& pick) {
        return std::none_of(score.compChoices.begin(), score.compChoices.end(), [&](const auto& current) {
            return current.active && current.group == pick.group && current.comp == pick.comp;
        });
    });
    if (changed) {
        if (auto result = score.compPlayback(choices, alternate); result.failed()) return result;
    }
    if(auto result=makeMidiTimelines(alternate ? *alternate : score,playbackRoutes_,playbackShift_,all,false,prepared.cue.takes);result.failed()) return result;
    const auto range=liveBoundaries_[prepared.cue.section].samples;
    prepared.events=std::make_unique<Timelines>(); prepared.chase=std::make_unique<Timelines>();
    for(size_t t=0;t<all.size();++t) if(!all[t].isEmpty()) {
        (*prepared.events)[t].addEvents(all[t],range.getStart(),range.getLength(),0);
        (*prepared.chase)[t]=midiAtSeek(all[t],range.getStart());
    }
    return juce::Result::ok();
}
bool PreviewTransport::enqueueCue(Cue cue,const ostfstudio::OstfpPreview* score) {
    const auto index=cue.section;
    if(index>=liveBoundaries_.size() || cue.repeats<1 || cue.repeats>99) return false;
    auto prepared=std::make_unique<PreparedCue>();
    prepared->cue=std::move(cue);
    if(score && prepareCueEvents(*prepared,*score).failed()) return false;
    const auto queue=[&] {
        if(!running_ || recording_ || cues_.size()-consumedCues_>=32) return false;
        cues_.erase(cues_.begin(),cues_.begin()+static_cast<std::ptrdiff_t>(consumedCues_));
        consumedCues_=0;
        cues_.push_back(std::move(prepared));
        queuedSection_=int(cues_.front()->cue.section);
        return true;
    };
    if (devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());return queue();}
    return queue();
}
bool PreviewTransport::hasQueuedSection(size_t index) const {
    const auto read=[&] {
        for (size_t i=consumedCues_; i<cues_.size(); ++i) if(cues_[i]->cue.section==index) return true;
        return false;
    };
    if(devices_){const juce::ScopedLock lock(devices_->getAudioCallbackLock());return read();}
    return read();
}
std::vector<PreviewTransport::Cue> PreviewTransport::pendingCues() const {
    std::vector<Cue> out;
    const auto read=[&]{for(size_t i=consumedCues_;i<cues_.size();++i) out.push_back(cues_[i]->cue);};
    if(devices_){const juce::ScopedLock lock(devices_->getAudioCallbackLock());read();}else read();
    return out;
}
std::vector<ostfstudio::TakeSelection> PreviewTransport::playingCueTakes() const {
    const auto read=[&]{return playingCue_?playingCue_->cue.takes:std::vector<ostfstudio::TakeSelection>{};};
    if(devices_){const juce::ScopedLock lock(devices_->getAudioCallbackLock());return read();}return read();
}
bool PreviewTransport::editCue(size_t position,int count,unsigned serial) {
    const auto change=[&]{
        const auto at=consumedCues_+position;
        if(at>=cues_.size() || cues_[at]->cue.serial!=serial || count<0 || count>99)return false;
        if(count) cues_[at]->cue.repeats=count;
        else cues_.erase(cues_.begin()+static_cast<std::ptrdiff_t>(at));
        queuedSection_=consumedCues_<cues_.size()?int(cues_[consumedCues_]->cue.section):-1;
        return true;
    };
    if(devices_){const juce::ScopedLock lock(devices_->getAudioCallbackLock());return change();}return change();
}

bool PreviewTransport::moveCue(unsigned serial,int step) {
    if(step!=1 && step!=-1) return false;
    const auto move=[&] {
        for(size_t i=consumedCues_;i<cues_.size();++i) if(cues_[i]->cue.serial==serial) {
            if ((step<0 && i==consumedCues_) || (step>0 && i+1==cues_.size())) return false;
            std::swap(cues_[i],cues_[step<0?i-1:i+1]);
            queuedSection_=int(cues_[consumedCues_]->cue.section);
            return true;
        }
        return false;
    };
    if (devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());return move();}
    return move();
}

void PreviewTransport::setLiveRepeats(size_t i,int count) {
    const auto apply=[&] {
        if(i>=liveBoundaries_.size()) return;
        liveBoundaries_[i].source.repeats=liveBoundaries_[i].remaining=std::clamp(count,0,99);
        if(liveWaiting_ && count>0) liveIndex_=i;
    };
    if(devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());apply();} else apply();
}
int PreviewTransport::liveRepeats(size_t i) const {
    const auto read=[&] {return i<liveBoundaries_.size()?liveBoundaries_[i].remaining:0;};
    if(devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());return read();}
    return read();
}

std::vector<PreviewTransport::LiveSection> PreviewTransport::upcomingLiveVisits(size_t maximum) const {
    maximum = std::min(maximum, size_t(32));
    std::vector<LiveSection> visits;
    visits.reserve(maximum);
    const auto read = [&] {
        const bool explicitPlan=playingCue_ || queuedSection_>=0;
        for (size_t i = liveIndex_; !launchFirstCue_ && i < liveBoundaries_.size() && visits.size() < maximum; ++i) {
            const auto& section = liveBoundaries_[i];
            for (int left = std::max(section.remaining, int(i==liveIndex_ && liveEntered_)); left > 0 && visits.size() < maximum; --left) {
                visits.push_back(section.source);
                visits.back().repeats = left;
            }
            if(explicitPlan) break;
        }
        for(size_t i=consumedCues_;i<cues_.size() && visits.size()<maximum;++i) {
            const auto& cue=cues_[i]->cue;
            for(int n=cue.repeats;n>0 && visits.size()<maximum;--n) {
                visits.push_back(liveBoundaries_[cue.section].source); visits.back().repeats=n;
            }
        }
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); read(); }
    else read();
    return visits;
}

void PreviewTransport::setMonitorGain(size_t track, double db) {
    const auto apply = [&] {
        if (track < playbackRoutes_.size())
            playbackRoutes_[track].monitorGain = juce::Decibels::decibelsToGain(float(db));
    };
    if (devices_) { const juce::ScopedLock lock(devices_->getAudioCallbackLock()); apply(); }
    else apply();
}
void PreviewTransport::setLiveMidiInput(const ostfstudio::OstfpPreview& song,std::optional<size_t> track,
        const ostfstudio::MidiInputRoute& route,const std::array<int,128>& padMap) {
    if (recording_ || !running_ || (track && (*track>=song.sourceTrackCount() || *track>=playbackRoutes_.size()))) return;
    if (devices_ && monitorTrack_) devices_->removeMidiInputDeviceCallback({},this);
    const auto apply=[&] {
        const juce::SpinLock::ScopedLockType inputLock(liveLock_);
        if (monitorTrack_) {
            auto& release=instruments_[*monitorTrack_].seekEvents;
            if(livePedal_) release.addEvent(juce::MidiMessage::controllerEvent(monitorChannel_,64,0),0);
            for(int i=0;i<128;++i) if(liveHeld_[size_t(i)]) release.addEvent(juce::MidiMessage::noteOff(monitorChannel_,i),0);
        }
        liveHeld_.fill(false);livePedal_=false;liveMessages_.clear();queuedLiveMessages_=0;
        monitorTrack_.reset();midiInput_=route;
        mapInput_=track && song.trackKinds[*track]==ostfstudio::TrackKind::drum;
        inputPitches_.fill(!mapInput_);
        if (mapInput_) {
            inputMap_=padMap;
            for(const auto& pad:song.slotsByTrack[*track]) if(pad.pitch>=0 && pad.pitch<128) inputPitches_[size_t(pad.pitch)]=true;
        }
        if(track) {
            liveMessages_.ensureSize(32768);
            monitorTrack_=playbackRoutes_[*track].outputTrack.value_or(*track);
            monitorChannel_=playbackRoutes_[*track].midiChannel;
        }
    };
    if(devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());apply();} else apply();
    if(devices_ && monitorTrack_) devices_->addMidiInputDeviceCallback({},this);
}
void PreviewTransport::setMonitorInput(size_t track,int first,int width,double db) {
    const auto apply=[&] {
        if(track>=playbackRoutes_.size()) return;
        auto& route=playbackRoutes_[track];
        route.monitorInput=first; route.monitorWidth=width;
        route.monitorGain=juce::Decibels::decibelsToGain(float(db));
    };
    if(devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());apply();} else apply();
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
        liveIndex_=0;
        while(liveIndex_<liveBoundaries_.size() && liveBoundaries_[liveIndex_].samples.getEnd()<=sample) ++liveIndex_;
        sample_ = sample; liveEntered_=false; liveWaiting_=false; queuedSection_=-1;
        consumedCues_=cues_.size(); playingCue_.reset(); launchFirstCue_=false;
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
        if (monitorTrack_ || (!captured_.getNumChannels() && recording_)) devices_->removeMidiInputDeviceCallback({}, this);
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
    performanceTick_.store(-1);
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
    position.setBpm(clock_.bpmAt(static_cast<juce::int64>(tick + 1e-9)));
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
        if (!effect || inserts_.bypassed(effect)) continue;
        pluginMidi_.clear();
        if (patches && effect->acceptsMidi()) pluginMidi_.addEvents(*patches, 0, -1, 0);
        const int keys = effect->getTotalNumInputChannels()-2;
        if (keys <= 0) { effect->processBlock(stereo,pluginMidi_); continue; }
        if (keys > 2) continue;
        const auto source = inserts_.keyTracks.find(effect);
        for(int ch=0;ch<keys;++ch) {
            if(source!=inserts_.keyTracks.end() && source->second<dryTracks_.size())
                keyScratch_.copyFrom(ch,0,dryTracks_[source->second],ch,0,count);
            else keyScratch_.clear(ch,0,count);
        }
        float* ports[]{audio.getWritePointer(0),audio.getWritePointer(1),keyScratch_.getWritePointer(0),keyScratch_.getWritePointer(1)};
        juce::AudioBuffer<float> keyed(ports,2+keys,count);
        effect->processBlock(keyed, pluginMidi_);
    }
}

void PreviewTransport::mixRenderedTrack(size_t track, juce::AudioBuffer<float>& block, int count, int first, double timelineStep) {
    const auto& slot=instruments_[track];
    processEffects(inserts_[outputCount+track],block,count,&insertPatches_[track]);
    const auto& mix = gains_[track];
    const auto cueGain = mix.cue.load();
    const auto basePan = mix.pan.load();
    const bool automated = !slot.panEnvelope.points.empty();
    if (automated) {
        for (int i = 0; i < count; ++i) {
            const auto tick = clock_.tickAt((first + i * timelineStep) / sampleRate_);
            const auto pan = float(slot.panEnvelope.valueAt(tick, basePan));
            for (int ch = 0; ch < 2; ++ch) block.getWritePointer(ch)[i] *= ostfstudio::stereoBalance(pan, ch == 1);
        }
    }
    auto& trackVu = liveVu_.vu[track];
    for (int channel = 0; channel < 2; ++channel) {
        const auto balance = automated ? 1.f : ostfstudio::stereoBalance(basePan, channel == 1);
        const auto gain = mix.level.load() * balance;
        const auto* data = block.getReadPointer(channel);
        trackVu[channel] = vu_[track][size_t(channel)].process(data, count, gain);
        trackMeters_[track].publish(channel, block.getMagnitude(channel, 0, count) * gain);
        stems_[static_cast<std::size_t>(mix.bus.load())].addFrom(channel, 0, data, count, gain);
        for (std::size_t r = 0; r < mix.sends.size(); ++r) {
            const auto send = mix.sends[r].load();
            if (send != 0.0f) stems_[3 + r].addFrom(channel, 0, data, count, gain * send);
        }
        if (cueGain != 0.0f)
            cueBus_.addFrom(channel, 0, data, count, cueGain * balance);
    }
}

void PreviewTransport::renderInstruments(float* const* output, int channels, int count, int first, int end, bool listenToCue) {
    for (auto& stem : stems_) stem.clear(0, count);
    cueBus_.clear(0, count);
    std::array<bool,maxTracks> rendered{};
    const bool hasDetector = !inserts_.keyTracks.empty();
    if(hasDetector) for(auto& dry:dryTracks_) dry.clear(0,count);
    const int audible = count;
    const auto timelineStep = double(end - first) / count;
    for (std::size_t track = 0; track < instruments_.size(); ++track) {
        auto& slot = instruments_[track];
        const auto& chain = inserts_[outputCount + track];
        const auto* route=track<playbackRoutes_.size()?&playbackRoutes_[track]:nullptr;
        const bool hardware=route && route->monitorInput>=0 && (route->monitorWidth==1 || route->monitorWidth==2);
        if (!hardware && !slot.external && !slot.instance && slot.regions.empty() && std::none_of(chain.begin(), chain.end(), [](auto* fx) { return fx != nullptr; })) continue;
        const auto channelCount = slot.instance ? std::max({2, slot.instance->getTotalNumInputChannels(),
                                                            slot.instance->getTotalNumOutputChannels()}) : 2;
        if (channelCount < 1 || channelCount > pluginAudio_.getNumChannels()) continue;
        juce::AudioBuffer<float> block(pluginAudio_.getArrayOfWritePointers(), channelCount, count);
        pluginMidi_.clear();
        if (releaseOnStandby_) for (int ch=1; ch<=16; ++ch) {
            pluginMidi_.addEvent(juce::MidiMessage::controllerEvent(ch,64,0),0);
            pluginMidi_.addEvent(juce::MidiMessage::allNotesOff(ch),0);
        }
        if (loopRestart_) pluginMidi_.addEvents(playingCue_ && playingCue_->chase ? (*playingCue_->chase)[track] :
            liveIndex_<liveBoundaries_.size() ? liveStarts_[liveIndex_][track] : slot.loopEvents, 0, -1, 0);
        if(liveLinearReplay_ && liveIndex_>0) pluginMidi_.addEvents(liveEnds_[liveIndex_-1][track],0,-1,0);
        pluginMidi_.addEvents(slot.seekEvents, 0, -1, 0);
        slot.seekEvents.clear();
        if (!monitorOnly_) {
            const auto& timeline=playingCue_ && playingCue_->events ? (*playingCue_->events)[track] : slot.timeline;
            if (end - first == count) pluginMidi_.addEvents(timeline, first, count, -first);
            else if (end > first) {
                for (auto it = timeline.findNextSamplePosition(first); it != timeline.cend(); ++it) {
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
        auto& patches = insertPatches_[track];
        patches.clear();
        if (std::any_of(chain.begin(), chain.end(), [](auto* fx) { return fx && fx->acceptsMidi(); })) {
            for (const auto event : pluginMidi_) {
                const auto msg = event.getMessage();
                if (msg.isProgramChange() || msg.isController())
                    patches.addEvent(msg, event.samplePosition);
            }
        }
        block.clear();
        if (slot.instance) {
            juce::AudioBuffer<float> pluginBlock(block.getArrayOfWritePointers(),
                std::max(slot.instance->getTotalNumInputChannels(), slot.instance->getTotalNumOutputChannels()), count);
            const bool active = !inserts_.bypassed(slot.instance);
            if (active) slot.instance->processBlock(pluginBlock, pluginMidi_);
            else if (slot.sounding) {
                pluginMidi_.clear();
                for (int ch = 1; ch <= 16; ++ch) {
                    pluginMidi_.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
                    pluginMidi_.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
                }
                slot.instance->processBlock(pluginBlock, pluginMidi_);
                pluginBlock.clear();
            }
            slot.sounding = active;
            if (slot.instance->getMainBusNumOutputChannels() == 1) block.copyFrom(1, 0, block, 0, 0, count);
        }
        if (hardware && liveInputs_ && route->monitorInput+route->monitorWidth<=liveInputCount_)
            for(int ch=0;ch<2;++ch) {
                auto* src=liveInputs_[route->monitorInput+ch%route->monitorWidth];
                if(src) block.addFrom(ch,0,src+liveOffset_,count,route->monitorGain);
            }
        if (!liveWaiting_) for (const auto& region : slot.regions) region.mixInto(block, count, first, timelineStep, sampleRate_, clock_);
        if (monitorOnly_ && route && route->auditionToMaster) {
            for (int ch=0; ch<2; ++ch) stems_[0].addFrom(ch,0,block,ch,0,count);
            continue;
        }
        if(!hasDetector) { mixRenderedTrack(track,block,count,first,timelineStep); continue; }
        for(int ch=0;ch<2;++ch) dryTracks_[track].copyFrom(ch,0,block,ch,0,count);
        rendered[track]=true;
    }
    // All source taps are from this same block, including tracks later in the arrangement.
    for(size_t track=0;track<instruments_.size();++track) {
        if(!rendered[track]) continue;
        juce::AudioBuffer<float> block(pluginAudio_.getArrayOfWritePointers(),2,count);
        for(int ch=0;ch<2;++ch) block.copyFrom(ch,0,dryTracks_[track],ch,0,count);
        mixRenderedTrack(track,block,audible,first,timelineStep);
    }
    for (std::size_t i = 1; i < stems_.size(); ++i) {
        const auto gain = outputGains_[i].load();
        processEffects(inserts_[i], stems_[i], count);
        for (int c = 0; c < 2; ++c) {
            liveVu_.outputVu[i][c] = outputVu_[i][c].process(stems_[i].getReadPointer(c), audible, gain);
            if (gain == 0.f) continue;
            outputMeters_[i].publish(c, stems_[i].getMagnitude(c, 0, audible) * gain);
            stems_[0].addFrom(c, 0, stems_[i], c, 0, audible, gain);
        }
    }
    processEffects(inserts_[0], stems_[0], count);
    const float masterGain = outputGains_[0].load();
    const auto* left = stems_[0].getReadPointer(0);
    const auto* right = stems_[0].getReadPointer(1);
    for (int i = 0; i < audible; ++i) {
        const float l = left[i] * masterGain, r = right[i] * masterGain;
        masterMeters_.push(l,r);
        masterSpectrum_->push(l,r);
        if (--historyCountdown_ <= 0) {
            historyCountdown_ = std::max(1, juce::roundToInt(sampleRate_ * .5));
            scopeFrame_.shortHistory[scopeFrame_.historyWrite] = float(masterMeters_.values.shortTerm);
            scopeFrame_.historyWrite = (scopeFrame_.historyWrite + 1) % scopeFrame_.shortHistory.size();
            scopeFrame_.historyCount = std::min<unsigned>(scopeFrame_.shortHistory.size(),scopeFrame_.historyCount + 1);
        }
        if (--phaseDecimation_ <= 0) {
            scopeFrame_.scope[scopeFrame_.nextPoint] = {l, r};
            scopeFrame_.nextPoint = (scopeFrame_.nextPoint + 1) % scopeFrame_.scope.size();
            phaseDecimation_ = std::max(1, int(sampleRate_ / 6000));
        }
    }
    scopeFrame_.levels = masterMeters_.values;
    for (int c = 0; c < 2; ++c)
        liveVu_.cueVu[c] = cueVu_[c].process(cueBus_.getReadPointer(c), audible, 1.f);
    scopeFrame_.vu = {masterVu_[0].process(left, audible, masterGain),
                      masterVu_[1].process(right, audible, masterGain)};
    { const juce::SpinLock::ScopedTryLockType guard(masterPublishLock_);
      if (guard.isLocked()) { publishedMaster_ = scopeFrame_; publishedVu_ = liveVu_; } }
    for (int c = 0; c < 2; ++c) {
        outputMeters_[0].publish(c, stems_[0].getMagnitude(c, 0, audible) * masterGain);
        cueMeter_.publish(c, cueBus_.getMagnitude(c, 0, audible));
        const auto* cue = cueBus_.getReadPointer(c);
        if (c < channels && output[c]) {
            if (listenToCue)
                juce::FloatVectorOperations::copy(output[c], cue, audible);
            else
                juce::FloatVectorOperations::addWithMultiply(output[c], stems_[0].getReadPointer(c), masterGain, audible);
        }
        if (channels >= 4 && output[c + 2])
            juce::FloatVectorOperations::copy(output[c + 2], cue, audible);
    }
}

ostfstudio::MeterFrame PreviewTransport::takeMeters() noexcept {
    ostfstudio::MeterFrame frame;
    for (std::size_t i = 0; i < trackMeters_.size(); ++i) frame.tracks[i] = trackMeters_[i].take();
    for (std::size_t i = 0; i < outputMeters_.size(); ++i) frame.outputs[i] = outputMeters_[i].take();
    frame.cue = cueMeter_.take();
    { const juce::SpinLock::ScopedLockType guard(masterPublishLock_); frame.master = publishedMaster_; static_cast<ostfstudio::MixerVuLevels&>(frame) = publishedVu_; }
    return frame;
}

void PreviewTransport::audioDeviceIOCallbackWithContext(const float* const* input, int inputs, float* const* output, int channels,
                                                        int count, const juce::AudioIODeviceCallbackContext&) {
    if (const auto* midi = ostfstudio::HostAudio::midi; midi && (monitorTrack_ || recording_) && channels == 2) {
        const int first = ostfstudio::HostAudio::blockOffset;
        int rendered = 0;
        auto through = [&](int end) {
            if (end <= rendered) return;
            float* offset[]{output[0] + rendered, output[1] + rendered};
            renderBlock(offset, 2, end - rendered);
            rendered = end;
        };
        for (auto it = midi->findNextSamplePosition(first); it != midi->end(); ++it) {
            auto event = *it;
            if (event.samplePosition >= first + count) break;
            through(event.samplePosition - first);
            inputFromDevice(event.getMessage(), {});
        }
        through(count);
        return;
    }
    renderBlock(output, channels, count, input, inputs);
}

void PreviewTransport::renderBlock(float* const* output, int channels, int count, const float* const* input, int inputs) {
    for (int c = 0; c < channels; ++c)
        if (output[c]) juce::FloatVectorOperations::clear(output[c], count);
    if (!running_) return;
    const int pairs = outputPairs_.load();
    std::array<float*,4> logical{};
    for (int bus=0; bus<2; ++bus) {
        const int first = ((pairs >> (8*bus)) & 255) - 1;
        if (first < 0 || (bus == 1 && first + 1 >= channels)) continue;
        for (int side=0; side<2 && first+side<channels; ++side)
            logical[2*bus+side] = output[first+side];
    }
    output = logical.data();
    channels = int(logical.size());
    const bool listenToCue = listenToCue_.load();
    if (pluginAudio_.getNumChannels() && count > pluginAudio_.getNumSamples()) { running_ = false; return; }
    const auto blockStart = juce::Time::getMillisecondCounterHiRes() + 1.0;
    blockTimeMs_ = blockStart;
    liveInputs_=input; liveInputCount_=inputs; liveOffset_=0;
    if (monitorOnly_) {
        if (pluginAudio_.getNumChannels()) renderInstruments(output, channels, count, int(sample_.load()), int(sample_.load()) + count, listenToCue);
        return;
    }
    int done = 0;
    while (done < count && running_) {
        if (const auto remaining = countRemaining_.load(); remaining > 0) {
            const auto played = countLength_ - remaining;
            const auto age = played % countBeat_;
            const int n = int(std::min<juce::int64>({count - done, remaining, countBeat_ - age}));
            if (metronome && !listenToCue) mixClick(output, channels, done, n, int(age), played % countBar_ < countBeat_);
            countRemaining_ -= n;
            done += n;
            continue;
        }
        auto first = sample_.load();
        if (queuedSection_>=0 && (launchFirstCue_ || liveWaiting_ || liveIndex_>=liveBoundaries_.size()
            || (first>=liveBoundaries_[liveIndex_].samples.getEnd() && liveBoundaries_[liveIndex_].remaining<=1))) {
            adoptPendingNotes();
            if (liveIndex_<liveBoundaries_.size()) liveBoundaries_[liveIndex_].remaining=liveBoundaries_[liveIndex_].source.repeats;
            playingCue_.swap(cues_[consumedCues_++]);
            launchFirstCue_=false;
            liveIndex_=playingCue_->cue.section;
            queuedSection_=consumedCues_<cues_.size()?int(cues_[consumedCues_]->cue.section):-1;
            auto& next=liveBoundaries_[liveIndex_];
            next.remaining=playingCue_->cue.repeats;
            first=next.samples.getStart(); sample_=first;
            liveEntered_=true; liveWaiting_=false; loopRestart_=true; liveLinearReplay_=false;
        }
        if (!loopSamples_.isEmpty() && first >= loopSamples_.getEnd()) {
            adoptPendingNotes();
            first = loopSamples_.getStart(); sample_ = first; loopRestart_ = true;
        }
        auto limit = loopSamples_.isEmpty() ? lastSample_ : juce::int64(loopSamples_.getEnd());
        if (liveIndex_<liveBoundaries_.size()) {
            auto& boundary=liveBoundaries_[liveIndex_];
            if (first>=boundary.samples.getEnd() && liveEntered_) {
                liveLinearReplay_=pendingNotes_ && boundary.remaining<=1;
                adoptPendingNotes();
                if (boundary.remaining>1) {
                    if(playingCue_ && revisedCue_ && playingCue_->cue.serial==revisedCue_->cue.serial) {
                        revisedCue_->events.swap(playingCue_->events);
                        revisedCue_->chase.swap(playingCue_->chase);
                        revisedCue_->cue.serial=0;
                    }
                    --boundary.remaining;
                    first=boundary.samples.getStart(); sample_=first; loopRestart_=true;
                } else {
                    if(playingCue_) {running_=false;break;}
                    // Preserve the configured count for an explicit later launch.
                    boundary.remaining=boundary.source.repeats;
                    ++liveIndex_;
                    liveEntered_=false;
                    if (liveIndex_==liveBoundaries_.size()) {
                        if (std::any_of(liveBoundaries_.begin(),liveBoundaries_.end(),[](const auto& b){return b.source.repeats>0;})) {
                            running_=false; break;
                        }
                        liveIndex_=0;
                    }
                }
            }
            if (!liveEntered_) {
                const auto old=liveIndex_;
                while(liveIndex_<liveBoundaries_.size() && liveBoundaries_[liveIndex_].remaining==0) ++liveIndex_;
                if (liveIndex_==liveBoundaries_.size()) {
                    if (std::any_of(liveBoundaries_.begin(),liveBoundaries_.end(),[](const auto& b){return b.source.repeats>0;})) {
                        running_=false; break;
                    }
                    releaseOnStandby_ = !liveWaiting_;
                    liveWaiting_=true; liveIndex_=old;
                    loopRestart_=false; liveLinearReplay_=false;
                    std::array<float*,4> dest{};
                    for (int c=0; c<channels; ++c) dest[c]=output[c] ? output[c]+done : nullptr;
                    liveOffset_=done;
                    if(pluginAudio_.getNumChannels()) renderInstruments(dest.data(),channels,count-done,int(first),int(first),listenToCue);
                    releaseOnStandby_=false;
                    elapsed_+=count-done;
                    break;
                }
                auto start=liveBoundaries_[liveIndex_].samples.getStart();
                if (old!=liveIndex_ || liveWaiting_ || first<start) {
                    first=start; sample_=first; loopRestart_=true; liveLinearReplay_=false;
                    adoptPendingNotes();
                }
                liveEntered_=true; liveWaiting_=false;
            }
            limit=std::min(limit,juce::int64(liveBoundaries_[liveIndex_].samples.getEnd()));
        }
        if (captured_.getNumChannels()) limit = std::min(limit, juce::int64(captureOrigin_ + captured_.getNumSamples()));
        const auto available = limit - first;
        if (available <= 0) { running_ = false; break; }
        const int part = int(std::min<juce::int64>(count - done, available));
        std::array<float*, 4> dest {};
        const int outs = std::min(channels, int(dest.size()));
        for (int c=0; c<outs; ++c) dest[c] = output[c] ? output[c] + done : nullptr;
        blockTimeMs_ = blockStart + done * 1000.0 / sampleRate_;
        liveOffset_=done;
        if (pluginAudio_.getNumChannels()) renderInstruments(dest.data(), outs, part, int(first), int(first) + part, listenToCue);
        if (captured_.getNumChannels()) {
            const int pos = int(first) - captureOrigin_;
            for (const auto& lane:captureLanes_) {
                if (!input || lane.first+lane.width>inputs || !input[lane.first]
                    || (lane.width==2 && !input[lane.first+1])) {running_=false; break;}
                const float gain=captureLanes_.size()==1 ? captureGain_.load() : lane.gain;
                for(int c=0;c<lane.width;++c)
                    captured_.copyFrom(lane.offset+c,pos,input[lane.first+c]+done,part,gain);
            }
            if (!running_) break;
            captureFirst_ = std::min(captureFirst_, pos);
            captureEnd_ = std::max(captureEnd_, pos + part);
        }
        if (metronome && !listenToCue) {
            const int clickLength = std::max(1, int(sampleRate_ * .025));
            auto beat = std::lower_bound(beatSamples_.begin(), beatSamples_.end(), int(first) - clickLength,
                [](const auto& b, int x) { return b.first < x; });
            for (; beat != beatSamples_.end() && beat->first < first + part; ++beat) {
                const int offset = std::clamp(int(beat->first - first), 0, part);
                mixClick(dest.data(), outs, offset, part - offset, std::max(0, int(first - beat->first)), beat->second);
            }
        }
        elapsed_ += part;
        done += part; sample_ = first + part; loopRestart_ = false; liveLinearReplay_=false;
        performanceTick_.store(std::llround(clock_.tickAt(double(first + part)/sampleRate_)));
        if (queuedSection_<0 && loopSamples_.isEmpty() && sample_ >= lastSample_ && (liveIndex_>=liveBoundaries_.size()
            || (liveBoundaries_[liveIndex_].remaining<=1 && std::any_of(liveBoundaries_.begin(),liveBoundaries_.end(),
                [](const auto& b){return b.source.repeats>0;})))) running_ = false;
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
    if (!running_ || countingIn() || (!recording_ && !monitorTrack_) || (captured_.getNumChannels() && !monitorTrack_) ||
        message.getChannel() <= 0 || (!note && !message.isController() && !message.isPitchWheel() && !message.isChannelPressure() && !message.isAftertouch() && !message.isProgramChange()) || (note && !inputPitches_[message.getNoteNumber()]))
        return;
    if (monitorTrack_) {
        const juce::SpinLock::ScopedLockType lock(liveLock_);
        auto routed = message;
        routed.setChannel(monitorChannel_);
        if(note) liveHeld_[size_t(routed.getNoteNumber())]=routed.isNoteOn();
        if(routed.isControllerOfType(64)) livePedal_=routed.getControllerValue()>=64;
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
        std::vector<CaptureLane> lanes;
        int channels=0, widest=options.inputChannels;
        for(const auto& r:options.audioTracks) {
            if(r.track>=maxTracks || r.first<0 || r.first>63 || r.width<1 || r.width>2 || !std::isfinite(r.gainDb)
               || r.gainDb < -60 || r.gainDb>24 || std::any_of(lanes.begin(),lanes.end(),[&](const auto& l){return l.track==r.track;}))
                return juce::Result::fail("Invalid recording input or gain.");
            lanes.push_back({r.track,r.first,r.width,channels,juce::Decibels::decibelsToGain(float(r.gainDb))});
            channels+=r.width;
            widest=std::max(widest,r.width);
        }
        if(lanes.empty()) {channels=options.inputChannels; lanes.push_back({maxTracks,firstInputBuffer_,channels,0,captureGain_.load()});}
        const int maxFrames = std::min((16 * 1024 * 1024 - 4096) / (2 * widest), (64*1024*1024)/(4*channels));
        captureOrigin_ = loopSamples_.isEmpty() ? int(takeStart_) : std::min(int(takeStart_), loopSamples_.getStart());
        const int span = (loopSamples_.isEmpty() ? int(lastSample_) : loopSamples_.getEnd()) - captureOrigin_;
        if (span <= 0 || (!loopSamples_.isEmpty() && span > maxFrames)) return juce::Result::fail("Choose a shorter audio recording range.");
        captured_.setSize(channels, std::min(maxFrames, span));
        captureLanes_=std::move(lanes);
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
    auto lane=std::find_if(captureLanes_.begin(),captureLanes_.end(),[&](const auto& l){return !l.read && (l.track==maxTracks || l.track==track);});
    if (lane==captureLanes_.end()) return {};
    ostfstudio::ProjectAudio clip;
    clip.track = track;
    clip.path = "Recording-" + juce::Uuid().toString() + ".wav";
    const int start = captureOrigin_ + captureFirst_, end = captureOrigin_ + captureEnd_;
    clip.startTick = juce::int64(std::llround(clock_.tickAt(start / sampleRate_)));
    clip.durationTicks = std::max<juce::int64>(1, std::llround(clock_.tickAt(end / sampleRate_)) - clip.startTick);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(clip.embedded.writable(), false);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate_)
        .withNumChannels(lane->width).withBitsPerSample(16));
    juce::AudioBuffer<float> view(captured_.getArrayOfWritePointers()+lane->offset,lane->width,captured_.getNumSamples());
    if (!writer || !writer->writeFromAudioSampleBuffer(view, captureFirst_, end - start)) return {};
    writer.reset();
    lane->read=true;
    if(std::all_of(captureLanes_.begin(),captureLanes_.end(),[](const auto& l){return l.read;})) captured_.setSize(0,0);
    return clip;
}
void PreviewTransport::setTrackInputGain(size_t track,double db) {
    const auto change=[&]{
        if(captureLanes_.size()==1) setInputGain(db);
        else for(auto& lane:captureLanes_) if(lane.track==track) lane.gain=juce::Decibels::decibelsToGain(float(db));
    };
    if(devices_) {const juce::ScopedLock lock(devices_->getAudioCallbackLock());change();} else change();
}
