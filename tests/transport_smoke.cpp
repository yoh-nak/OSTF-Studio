#include "ui/AudioRegionEdits.h"
#include "ui/MainComponent.h"
#include "ui/SongInformationPanel.h"
#include "ui/SongMidi.h"
#include <iostream>

using namespace ostfstudio;
namespace {
// A two-channel source with a deterministic output lets us check the host mix and MIDI stream.
class PerformanceProbe final : public juce::AudioPluginInstance {
public:
    explicit PerformanceProbe(bool insert = false) { setPlayConfigDetails(insert ? 2 : 0, 2, 960, 96); }
    juce::MidiBuffer received;
    int blocksProcessed = 0;
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        ++blocksProcessed;
        received.addEvents(midi, 0, -1, 0);
        midi.clear();
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill(audio.getWritePointer(ch), .5f, audio.getNumSamples());
    }
    const juce::String getName() const override { return "Performance probe"; }
    void fillInPluginDescription(juce::PluginDescription& d) const override { d.isInstrument = true; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

bool checkPerformanceAudio() {
    OstfpPreview song;
    song.ppq = 480;
    song.ticksPerBar = 1920;
    song.bars = {1, 2};
    song.clock = TempoMap(480);
    song.clock.append(0, 120);
    song.trackIds.add("kit");
    song.pitchedNotes.resize(1);
    song.drumNotes.resize(1);
    song.programChanges.resize(1);
    song.performanceCurves.resize(1);
    song.panAutomation["kit"] = {"pan", true, {{0, -1}, {95, 1}}};
    for (MidiControl k :
         {MidiControl{"controlChange", 1, 3}, MidiControl{"pitchBend", 0, 3}, MidiControl{"channelPressure", 0, 3}})
        song.performanceCurves[0].push_back({k.label(), {{10, 1, "p"}}, false, true, false, k});
    // The bank staged after program 40 must not change the bank of the chased program.
    song.programChanges[0].push_back({10, 40, 3});
    MidiControl bank{"controlChange", 0, 3};
    song.performanceCurves[0].push_back({bank.label(), {{10, 2./127, "bank", -2}, {20, 7./127, "next-bank", -2}}, false, true, false, bank});
    {
        PerformanceProbe referenceTone;
        juce::AudioBuffer<float> samples(2,96);
        for(bool audition:{false,true}) {
            auto monitor=std::make_unique<PreviewTransport>();
            std::array<PreviewTransport::TrackSetup,1> route{{{{},&referenceTone,1}}};
            route[0].auditionToMaster=audition;
            if(monitor->preparePlayback(song,route,{},960,96,0,1,true).failed())return false;
            monitor->setOutputMix({});
            monitor->setChannelMix(0,{},false,0);
            samples.clear();
            monitor->renderBlock(samples.getArrayOfWritePointers(),2,96);
            if((samples.getMagnitude(0,96)>.01f)!=audition)return false;
        }
    }
    PerformanceProbe fx1(true), fx2(true), probe;
    PreviewTransport::Inserts inserts{};
    inserts[PreviewTransport::outputCount][0] = &fx1;
    inserts[PreviewTransport::outputCount][1] = &fx2;
    PreviewTransport engine;
    const std::array<PreviewTransport::TrackSetup, 1> routes{{{{}, &probe, 10}}};
    if (engine.preparePlayback(song, routes, inserts, 960, 96).failed()) return false;
    engine.setChannelMix(0, {}, true, 0);
    engine.setOutputMix({});
    juce::AudioBuffer<float> audio(2, 96);
    engine.renderBlock(audio.getArrayOfWritePointers(), 2, 96);
    if (audio.getSample(0, 0) < .45f || audio.getSample(1, 0) > .01f || audio.getSample(0, 95) > .01f ||
        audio.getSample(1, 95) < .45f)
        return false;
    {
        PerformanceProbe clipVoice;
        PreviewTransport audition;
        const std::array<PreviewTransport::TrackSetup,1> solo{{{{},&clipVoice,1}}};
        const auto songPosition=engine.positionTick();
        if(audition.preparePlayback(song,solo,{},960,96).failed()) return false;
        audition.seekToTick(300);
        if(audition.setLiveSections({{"part","Part",{300,348},1}}).failed()) return false;
        audition.setLoop({300,348});
        audition.renderBlock(audio.getArrayOfWritePointers(),2,72);
        if(audition.positionTick()!=324 || engine.positionTick()!=songPosition) return false;
        audition.setLoop({});
        audition.renderBlock(audio.getArrayOfWritePointers(),2,48);
        if(audition.isRunning() || engine.positionTick()!=songPosition || !engine.isRunning()) return false;
    }
    {
        PerformanceProbe sectionInstrument;
        PreviewTransport live;
        const std::array<PreviewTransport::TrackSetup,1> sectionRoutes{{{{},&sectionInstrument,1}}};
        if(live.preparePlayback(song,sectionRoutes,{},960,96).failed())return false;
        if(live.setLiveSections({{"skip","Skip",{0,1},0},{"a","A",{1,62},2},{"b","B",{62,122},3}}).failed())return false;
        const auto visits=live.upcomingLiveVisits(3);
        if(visits.size()!=3 || visits[0].id!="a" || visits[1].id!="a" || visits[2].id!="b" || visits[1].ticks!=visits[0].ticks) return false;
        live.renderBlock(audio.getArrayOfWritePointers(),2,96);
        if(live.positionTick()!=36 || live.liveRepeats(1)!=1) { std::cerr << "First section ended at " << live.positionTick() << '\n'; return false; }
        live.setLiveRepeats(2,2);
        live.renderBlock(audio.getArrayOfWritePointers(),2,96);
        if(live.positionTick()!=72 || live.liveRepeats(2)!=1) { std::cerr << "Second section ended at " << live.positionTick() << '\n'; return false; }
        // Publish a different note set while the second visit is running. Neither
        // the cursor nor the processors are restarted by append or by audition.
        song.bars.endBar=3;
        auto& take=song.midiTakes["kit"];
        take.count=2; take.owners["alt"]=2; take.suppressed.insert("alt");
        song.pitchedNotes[0].push_back({75,80,74,99,false,"alt"});
        const std::array<ostfstudio::TakeSelection,1> audition{{{0,{62,122},2}}};
        if(live.extendLiveSong(song,{{"skip","Skip",{0,1},0},{"a","A",{1,62},2},{"b","B",{62,122},3},
                                     {"c","C",{122,200},1}},audition,7).failed()
           || live.positionTick()!=72 || live.soundingTakeRevision()!=0) return false;
        live.setLiveRepeats(2,2);
        live.renderBlock(audio.getArrayOfWritePointers(),2,96);
        bool heardAlternative=false;
        for (const auto event : sectionInstrument.received) {
            auto msg=event.getMessage();
            if(msg.isNoteOn() && msg.getNoteNumber()==74) heardAlternative=true;
        }
        if (live.positionTick()!=108 || live.soundingTakeRevision()!=7 || !heardAlternative) return false;
        live.renderBlock(audio.getArrayOfWritePointers(),2,32);
        if (live.positionTick()!=140 || !live.isRunning()) return false;
        song.pitchedNotes[0].pop_back(); song.midiTakes.clear(); song.bars.endBar=2;
        if(live.setLiveSections({{"a","A",{0,300},1},{"b","B",{200,301},1}}).wasOk())return false;
        live.stop();
        if (live.preparePlayback(song,sectionRoutes,{},960,96).failed()
            || live.setLiveSections({{"a","A",{0,32},1},{"skip","Skip",{32,64},0}}).failed()) return false;
        live.renderBlock(audio.getArrayOfWritePointers(),2,64);
        if (live.isRunning()) return false;
        live.stop();
        if (live.preparePlayback(song,sectionRoutes,{},960,96).failed()
            || live.setLiveSections({{"a","A",{0,32},0},{"b","B",{32,64},0}}).failed()) return false;
        live.renderBlock(audio.getArrayOfWritePointers(),2,64);
        if (!live.isRunning() || live.positionTick()!=0) return false;
        live.setLiveRepeats(1,1);
        live.renderBlock(audio.getArrayOfWritePointers(),2,16);
        if (!live.isRunning() || live.positionTick()!=48) return false;
        live.stop();

        song.bars.endBar=4;
        if (live.preparePlayback(song,sectionRoutes,{},960,96).failed()
            || live.setLiveSections({{"a","A",{0,3840},2},{"b","B",{3840,5760},1}}).failed()) return false;
        live.renderBlock(audio.getArrayOfWritePointers(),2,16);
        if (!live.queueLiveSection(1) || live.positionTick()!=16 || live.queuedLiveSection()!=1) return false;
        live.setLiveRepeats(1,0);
        if (!live.hasQueuedSection(1) || live.queueLiveSection(1)) return false;
        live.setLiveRepeats(1,1);
        if (!live.enqueueCue({0,2,{},71}) || !live.editCue(1,3,71) || live.editCue(1,1,72)) return false;
        const auto queue=live.pendingCues();
        if (queue.size()!=2 || queue[0].section!=1 || queue[1].repeats!=3) return false;
        if (!live.moveCue(71,-1) || live.queuedLiveSection()!=0 || live.moveCue(71,-1) || !live.moveCue(71,1)) return false;
        for (int block=0;block<80;++block) live.renderBlock(audio.getArrayOfWritePointers(),2,96);
        if (live.positionTick()!=3856 || live.queuedLiveSection()!=0) return false;
        for (int block=0;block<20;++block) live.renderBlock(audio.getArrayOfWritePointers(),2,96);
        if (live.positionTick()!=16 || live.queuedLiveSection()!=-1) return false;
        live.stop(); song.bars.endBar=2;
    }
    {
        PreviewTransport inputMonitor;
        PreviewTransport::TrackSetup guitar;
        guitar.monitorInput=1; guitar.monitorWidth=1;
        const std::array inputs{guitar};
        if (inputMonitor.preparePlayback(song,inputs,{},960,96).failed()) return false;
        inputMonitor.setChannelMix(0,{},true,0);
        inputMonitor.setOutputMix({});
        inputMonitor.setMonitorGain(0,-6.0206);
        juce::AudioBuffer<float> input(2,96);
        input.clear();
        juce::FloatVectorOperations::fill(input.getWritePointer(1),.4f,96);
        inputMonitor.renderBlock(audio.getArrayOfWritePointers(),2,96,input.getArrayOfReadPointers(),2);
        if (std::abs(audio.getSample(0,0)-.2f)>.005f || std::abs(audio.getSample(1,95)-.2f)>.005f) {
            std::cerr << "Monitor levels " << audio.getSample(0,0) << ", " << audio.getSample(1,95) << '\n'; return false;
        }
        inputMonitor.stop();
    }
    {
        OstfpPreview shared;
        shared.ppq = song.ppq;
        shared.clock = song.clock;
        shared.bars = song.bars;
        shared.ticksPerBar = song.ticksPerBar;
        shared.trackIds.addArray({"piano", "bass"});
        shared.trackKinds = {TrackKind::pitched, TrackKind::pitched};
        shared.pitchedNotes = {{{0, 60, 72, 90}}, {{0, 60, 42, 90}}};
        shared.programChanges.resize(2);
        shared.programChanges[0].push_back({0, 4, 2});
        shared.programChanges[1].push_back({0, 33, 1});
        PerformanceProbe rack, unusedLocal;
        PreviewTransport playback;
        const std::array<PreviewTransport::TrackSetup, 2> parts{{{{}, &rack, 1}, {0, &unusedLocal, 2}}};
        if (playback.preparePlayback(shared, parts, {}, 960, 96).failed()) return false;
        playback.renderBlock(audio.getArrayOfWritePointers(), 2, 96);
        bool piano = false, bass = false, patch = false, ownerPatch = false;
        for (const auto entry : rack.received) {
            const auto message = entry.getMessage();
            if (message.isNoteOn() && message.getNoteNumber() == 72) piano = message.isForChannel(1);
            if (message.isNoteOn() && message.getNoteNumber() == 42) bass = message.isForChannel(2);
            if (message.isProgramChange() && message.getProgramChangeNumber() == 4) ownerPatch = message.isForChannel(1);
            if (message.isProgramChange() && message.getProgramChangeNumber() == 33) {
                if (!message.isForChannel(2)) return false;
                patch = true;
            }
        }
        if (!piano || !bass || !patch || !ownerPatch || rack.blocksProcessed != 1 || unusedLocal.blocksProcessed != 0) return false;
    }
    {
        PerformanceProbe padInstrument;
        PreviewTransport capture;
        const std::array<PreviewTransport::TrackSetup, 1> drumRoutes{{{{}, &padInstrument, 10}}};
        song.trackKinds = {TrackKind::drum};
        song.slotsByTrack = {{{"kick", "Kick", 36}}};
        if (capture.preparePlayback(song, drumRoutes, {}, 960, 96).failed())
            return false;
        capture.prepareRecording(song, drumRoutes, 0);
        std::array<int, 128> mapping;
        for (int i = 0; i < 128; ++i) mapping[i] = i;
        mapping[61] = 36;
        PreviewTransport::TakeOptions setup;
        setup.drumMapping = &mapping;
        setup.midiInput = {"test-port", "Test keyboard", 1};
        if (capture.prepareTake(setup).failed()) return false;
        capture.inputFromDevice(juce::MidiMessage::noteOn(1, 61, juce::uint8(127)), "other-port");
        capture.inputFromDevice(juce::MidiMessage::noteOn(2, 61, juce::uint8(127)), "test-port");
        capture.inputFromDevice(juce::MidiMessage::noteOn(1, 61, juce::uint8(87)), "test-port");
        capture.inputNote(juce::MidiMessage::noteOn(1, 37, juce::uint8(127)));
        capture.renderBlock(audio.getArrayOfWritePointers(), 2, 64);
        capture.inputFromDevice(juce::MidiMessage::noteOff(1, 61), "test-port");
        capture.renderBlock(audio.getArrayOfWritePointers(), 2, 32);
        const auto hits = capture.stop();
        if (hits.size() != 1 || hits[0].pitch != 36 || hits[0].velocity != 87 || hits[0].startTick != 0 ||
            hits[0].endTick != 64 ||
            !std::any_of(padInstrument.received.begin(), padInstrument.received.end(), [](const auto &e) {
                auto m = e.getMessage();
                return m.isNoteOn() && m.getChannel() == 10 && m.getNoteNumber() == 36;
            }))
            return false;
    }
    {
        PerformanceProbe local, insert(true);
        juce::MidiBuffer hardware;
        auto port = std::make_shared<PreviewTransport::MidiDestination>();
        port->send = [&](const juce::MidiBuffer& b, double, double) { hardware.addEvents(b, 0, -1, 0); };
        port->clearPending = [] {};
        PreviewTransport monitor;
        PreviewTransport::Inserts chain{};
        chain[PreviewTransport::outputCount][0] = &insert;
        std::array<PreviewTransport::TrackSetup,1> live{{{{}, &local, 3}}};
        song.trackKinds = {TrackKind::pitched};
        song.pitchedNotes[0].push_back({2, 12, 72, 80});
        for (bool external : {false,true}) {
            live[0].external = external ? port : nullptr;
            local.received.clear(); insert.received.clear();
            if (monitor.preparePlayback(song, live, chain, 960, 96, 0, 1, true).failed()) return false;
            monitor.prepareRecording(song, live, 0, false);
            PreviewTransport::TakeOptions options;
            options.midiInput = {"keys", "Keyboard", 1};
            monitor.prepareTake(options);
            monitor.setChannelMix(0, {}, true, 0);
            monitor.setOutputMix({});
            monitor.inputFromDevice(juce::MidiMessage::noteOn(1,64,juce::uint8(78)), "keys");
            monitor.inputFromDevice(juce::MidiMessage::noteOn(1,65,juce::uint8(78)), "wrong");
            monitor.inputFromDevice(juce::MidiMessage::controllerEvent(1,64,127), "keys");
            monitor.inputFromDevice(juce::MidiMessage::noteOff(1,64), "keys");
            monitor.renderBlock(audio.getArrayOfWritePointers(),2,96);
            bool on = false, off = false, pedal = false;
            for (auto e : (external ? hardware : local.received)) {
                auto m = e.getMessage();
                if (m.getChannel() != 3 || (m.isNoteOnOrOff() && m.getNoteNumber() != 64)) return false;
                on |= m.isNoteOn(); off |= m.isNoteOff(); pedal |= m.isControllerOfType(64);
            }
            if (!on || !off || !pedal || monitor.positionTick() != 0 || (audio.getMagnitude(0,96) > 0) == external ||
                (monitor.takeMeters().tracks[0][0] > 0) == external || (external && (!local.received.isEmpty() || !insert.received.isEmpty())) ||
                !monitor.stop().empty()) return false;
        }
        hardware.clear();
        if (monitor.preparePlayback(song,live,chain,960,96).failed()) return false;
        monitor.renderBlock(audio.getArrayOfWritePointers(),2,96);
        if (!std::any_of(hardware.begin(),hardware.end(),[](auto e) { return e.getMessage().isNoteOn() && e.getMessage().getNoteNumber() == 72; })
            || audio.getMagnitude(0,96) != 0) return false;
        monitor.stop();
        song.pitchedNotes[0].clear();
        song.trackKinds = {TrackKind::drum};
    }
    for (int channels : {0, 1, 2}) {
        PreviewTransport capture;
        if (capture.preparePlayback(song, {}, {}, 960, 96).failed()) return false;
        capture.seekToTick(52);
        capture.setLoop({32, 96});
        if (!channels) capture.prepareRecording(song, {}, {});
        PreviewTransport::TakeOptions setup;
        setup.inputChannels = channels;
        juce::BigInteger mask;
        mask.setBit(0); mask.setBit(4); mask.setBit(5);
        AudioTrackInput route{4, -6.020599913};
        if (route.bufferIndex(mask, 2) != 1 || AudioTrackInput{0}.physical(mask, 2) >= 0
            || AudioTrackInput{}.physical(mask, 2) != 4) return false;
        setup.firstInputBuffer = route.bufferIndex(mask, channels ? channels : 1);
        setup.inputGainDb = route.gainDb;
        setup.countInBars = 1 + channels;
        capture.metronome=channels>0;
        if (capture.prepareTake(setup).failed()) return false;
        capture.inputNote(juce::MidiMessage::noteOn(1, 62, juce::uint8(77)));
        capture.renderBlock(audio.getArrayOfWritePointers(), 2, 96);
        if (!capture.countingIn() || capture.countInSeconds()<=0 || capture.positionTick() != 52
            || (audio.getMagnitude(0,0,96)>.01f)!=(channels>0)) return false;
        const juce::AudioBuffer<float> firstDownbeat(audio);
        for (int n = 1; n < 20 * setup.countInBars; ++n) {
            capture.renderBlock(audio.getArrayOfWritePointers(), 2, 96);
            if (n % 20 == 0) {
                if (!capture.countingIn() || capture.positionTick() != 52) return false;
                for (int frame = 0; frame < 96; ++frame)
                    if (std::abs(firstDownbeat.getSample(0, frame) - audio.getSample(0, frame)) > .0001f) return false;
            }
        }
        if (capture.countingIn() || capture.positionTick() != 52) return false;
        juce::AudioBuffer<float> input(3, 96);
        for (int i = 0; i < 96; ++i) {
            input.setSample(0, i, .75f);
            input.setSample(1, i, .001f * i);
            input.setSample(2, i, -.001f * i);
        }
        capture.inputNote(juce::MidiMessage::noteOn(1, 60, juce::uint8(88)));
        capture.renderBlock(audio.getArrayOfWritePointers(), 2, 96, input.getArrayOfReadPointers(), input.getNumChannels());
        if (capture.positionTick() != 84) return false;
        const auto notes = capture.stop();
        if (capture.recordedRange() != juce::Range<juce::int64>(32, 96)) return false;
        if (!channels) {
            if (notes.size() != 2 || notes[0].pitch != 60 || notes[0].startTick != 52 || notes[0].endTick != 96
                || notes[1].startTick != 32 || notes[1].endTick != 84) return false;
        } else {
            const auto take = capture.finishAudioTake(0);
            if (!take || take->startTick != 32 || take->durationTicks != 64 || !notes.empty()) return false;
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::MemoryInputStream(take->embedded, false), true));
            if (!reader || reader->lengthInSamples != 64 || int(reader->numChannels) != channels) return false;
            juce::AudioBuffer<float> check(channels, 64);
            if (!reader->read(&check, 0, 64, 0, true, true)) return false;
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < 64; ++i)
                    if (std::abs(check.getSample(c, i) - .5f * input.getSample(c + 1, i < 52 ? i + 44 : i - 20)) > .00005f) return false;
        }
    }
    {
        PreviewTransport duo;
        const std::array<PreviewTransport::TrackSetup,1> inputRoute{};
        song.trackKinds[0]=TrackKind::pitched;
        if (duo.preparePlayback(song,inputRoute,{},960,96).failed()) return false;
        duo.prepareRecording(song,inputRoute,0);
        PreviewTransport::TakeOptions setup;
        setup.inputChannels=1;
        setup.audioTracks={{0,0,1,0},{1,1,2,-6.020599913}};
        if (duo.prepareTake(setup).failed()) return false;
        juce::AudioBuffer<float> inputs(3,96);
        for(int c=0;c<3;++c) for (int i=0;i<96;++i) inputs.setSample(c,i,float(c+1)*.2f);
        duo.inputNote(juce::MidiMessage::noteOn(1,64,juce::uint8(90)));
        duo.renderBlock(audio.getArrayOfWritePointers(),2,96,inputs.getArrayOfReadPointers(),3);
        const auto midi=duo.stop();
        song.trackKinds[0]=TrackKind::drum;
        if(midi.size()!=1 || midi.front().pitch!=64 || midi.front().startTick!=0 || midi.front().endTick!=96) return false;
        for (int t=1;t>=0;--t) {
            auto clip=duo.finishAudioTake(size_t(t));
            if (!clip || clip->track!=size_t(t) || clip->startTick!=0 || clip->durationTicks!=96) return false;
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> read(wav.createReaderFor(new juce::MemoryInputStream(clip->embedded,false),true));
            juce::AudioBuffer<float> samples(t+1,96);
            if (!read || read->numChannels!=unsigned(t+1) || !read->read(&samples,0,96,0,true,true)) return false;
            for(int c=0;c<=t;++c) if (std::abs(samples.getSample(c,48)-(t==0?.2f:.1f*float(c+2)))>.00005f) return false;
        }
    }
    const auto heard = [&](int sample) {
        bool cc = false, bend = false, at = false;
        for (auto e : probe.received) {
            auto m = e.getMessage();
            if (e.samplePosition != sample || m.getChannel() != 3) continue;
            cc |= m.isControllerOfType(1) && m.getControllerValue() == 127;
            bend |= m.isPitchWheel() && m.getPitchWheelValue() == 16383;
            at |= m.isChannelPressure() && m.getChannelPressureValue() == 127;
        }
        return cc && bend && at;
    };
    const auto patchOrder = [](const juce::MidiBuffer& buffer) {
        std::vector<int> values;
        for (auto event : buffer) {
            auto m = event.getMessage();
            if (m.getChannel() == 3 && (m.isProgramChange() || m.isControllerOfType(0)))
                values.push_back(m.isProgramChange() ? 128 + m.getProgramChangeNumber() : m.getControllerValue());
        }
        return values == std::vector<int>{2,168,7};
    };
    if (!heard(10) || !patchOrder(probe.received) || !patchOrder(fx1.received) || !patchOrder(fx2.received)) return false;
    fx1.received.clear();
    fx2.received.clear();
    engine.seekToTick(48);
    probe.received.clear();
    engine.renderBlock(audio.getArrayOfWritePointers(), 2, 16);
    if (!heard(0) || !patchOrder(probe.received) || !patchOrder(fx2.received)) return false;
    engine.setLoop({48, 96});
    song.drumNotes[0].push_back({52, 60, 42, 111, true, "hat"});
    if (engine.updateLoopNotes(song).failed()) return false;
    probe.received.clear();
    engine.renderBlock(audio.getArrayOfWritePointers(), 2, 48);
    if (engine.positionTick() != 64 || !heard(0)) return false;
    for (auto e : probe.received) {
        auto m = e.getMessage();
        if (m.isNoteOn() && m.getNoteNumber() == 42 && m.getChannel() == 10 && m.getVelocity() == 111 &&
            e.samplePosition == 4)
            return true;
    }
    return false;
}

bool checkWaveform(const juce::File& directory) {
    // One second of silence then a left-only half-second pulse. Trim the first half-second.
    constexpr int rate = 8000, frames = 16000;
    juce::MemoryOutputStream bytes;
    bytes.write("RIFF", 4); bytes.writeInt(36 + frames * 4); bytes.write("WAVEfmt ", 8);
    bytes.writeInt(16); bytes.writeShort(1); bytes.writeShort(2); bytes.writeInt(rate);
    bytes.writeInt(rate * 4); bytes.writeShort(4); bytes.writeShort(16);
    bytes.write("data", 4); bytes.writeInt(frames * 4);
    for (int i = 0; i < frames; ++i) {
        bytes.writeShort(i >= rate && i < rate * 3 / 2 ? (i % 8 < 4 ? 26000 : -26000) : 0);
        bytes.writeShort(0);
    }
    const auto file = directory.getChildFile("waveform-source.wav");
    if (!file.replaceWithData(bytes.getData(), bytes.getDataSize())) return false;
    ProjectAudio region{0, "pulse.wav", file, {}};
    region.startTick = 960;
    region.startFrame = 4000;
    region.frameCount = 8000;
    TempoMap clock(960);
    clock.append(0, 120); clock.append(1920, 60);
    AudioClipWaveforms waveforms;
    for (bool embedded : {false, true}) {
        if (embedded) { region.localFile = juce::File{}; region.embedded = bytes.getMemoryBlock(); }
        waveforms.load({region});
        if (!waveforms.available(region) || std::abs(waveforms.endTick(region, clock) - 2400) > .001) {
            std::cerr << "Could not load the trimmed WAV at its expected length\n";
            return false;
        }
        bool ready = false;
        for (int attempt = 0; !ready && attempt < 150; ++attempt) {
            juce::Image image(juce::Image::ARGB, 128, 48, true, juce::SoftwareImageType{});
            { juce::Graphics g(image); waveforms.draw(g, image.getBounds(), region, clock, 960, 2880); }
            // Sound begins at tick 1920 (x64) and the source ends at 2400 (x96).
            ready = image.getPixelAt(78, 6).getAlpha() > 0;
            if (image.getPixelAt(43, 6).getAlpha() || image.getPixelAt(106, 6).getAlpha()
                || image.getPixelAt(78, 30).getAlpha()) {
                std::cerr << "Unexpected signal outside the left-channel pulse\n";
                return false;
            }
            if (!ready) juce::Thread::sleep(10);
        }
        if (!ready) { std::cerr << "Missing left-channel waveform\n"; return false; }
        juce::Image zoom(juce::Image::ARGB, 128, 48, true, juce::SoftwareImageType{});
        { juce::Graphics g(zoom); waveforms.draw(g, zoom.getBounds(), region, clock, 1920, 2400); }
        if (zoom.getPixelAt(24, 6).getAlpha() == 0) { std::cerr << "Missing zoomed pulse\n"; return false; }
    }
    const ClipTiming unordered{false, 960, {{0.3, 0.8}, {0.6, 0.7}}};
    if (unordered.valid()) return false;
    region.durationTicks = 1920;
    region.timing = {true, 960, {{0.25, 0.5}}};
    juce::Image repeats(juce::Image::ARGB, 128, 48, true, juce::SoftwareImageType{});
    { juce::Graphics g(repeats); waveforms.draw(g, repeats.getBounds(), region, clock, 960, 2880); }
    if (repeats.getPixelAt(64, 4).getARGB() != AudioClipWaveforms::seamColour) return false;
    if (repeats.getPixelAt(8, 6).getAlpha() || repeats.getPixelAt(72, 6).getAlpha()
        || !repeats.getPixelAt(30, 6).getAlpha() || !repeats.getPixelAt(94, 6).getAlpha()) return false;
    OstfpPreview song;
    song.ppq = 960; song.ticksPerBar = 3840; song.bars = {1, 2}; song.clock = clock;
    song.trackIds.add("sample"); song.pitchedNotes.resize(1); song.drumNotes.resize(1);
    song.programChanges.resize(1); song.performanceCurves.resize(1);
    song.audio.push_back(region);
    const std::array<PreviewTransport::TrackSetup, 1> tracks{};
    PreviewTransport engine;
    juce::AudioBuffer<float> block(2, 32);
    auto run = [&](const ClipTiming& timing) {
        song.audio.front().timing = timing;
        if (engine.preparePlayback(song, tracks, {}, rate, 32).failed()) return false;
        engine.setChannelMix(0, {}, true, 0); engine.setOutputMix({});
        return true;
    };
    auto level = [&](int tick) {
        engine.seekToTick(tick);
        engine.renderBlock(block.getArrayOfWritePointers(), 2, 32);
        return block.getMagnitude(0, 0, 32);
    };
    if (!run(region.timing) || level(1056) > 0.01f || level(1440) < 0.15f
        || level(2016) > 0.01f || level(2400) < 0.15f || level(2880) > 0.01f) return false;
    // Playback measurements must agree on both sides of a cut through a warped cycle.
    const std::array<int, 7> ticks{1056, 1380, 1440, 1900, 2016, 2400, 2840};
    std::array<float, 7> baseline{};
    for (size_t i = 0; i < ticks.size(); ++i) baseline[i] = level(ticks[i]);
    auto right = rightSlice(region, 1380, 2880, clock, 1.);
    song.audio.front().durationTicks = 1380 - region.startTick;
    song.audio.push_back(right);
    if (!run(region.timing)) return false;
    for (size_t i = 0; i < ticks.size(); ++i)
        if (std::abs(baseline[i] - level(ticks[i])) > 0.0001f) return false;
    ProjectAudio consolidated;
    if (consolidateRegions(song.audio[0], song.audio[1], 2880, clock, consolidated).failed()
        || consolidated.timing != region.timing || consolidated.durationTicks != region.durationTicks) return false;
    song.audio = {region};
    // The trimmed native sample repeats every second, including across the tempo change.
    if (!run({true, 0, {}}) || level(2320) < 0.15f || level(2450) > 0.01f) return false;
    if (!run({false, 960, {}}) || level(1445) < 0.15f || level(2200) > 0.01f) return false;
    if (!run({false, 960, {}, 240, 800}) || level(1000) > 0.01f || level(1750) < 0.15f
        || level(2050) > 0.01f) return false;
    const auto hash = juce::SHA256(region.embedded).toHexString();
    const auto editFile = directory.getChildFile("edited-source.wav");
    auto editedBytes = region.embedded;
    static_cast<char*>(editedBytes.getData())[44 + 4 * rate] ^= 0x40;
    ProjectAudio replacement;
    if (!editFile.replaceWithData(editedBytes.getData(), editedBytes.getSize())
        || prepareEditedWav(region, editFile, "stale", replacement).wasOk()
        || prepareEditedWav(region, editFile, hash, replacement).failed()
        || replacement.embedded != editedBytes || replacement.sha256 == hash
        || replacement.startFrame != region.startFrame || replacement.startTick != region.startTick
        || !replacement.timing.loop || replacement.timing.markers.size() != 1
        || prepareEditedWav(replacement, editFile, replacement.sha256, region).wasOk()
        || region.embedded != bytes.getMemoryBlock()) return false;
    region.durationTicks = 240;
    juce::int64 clipFrames = 0;
    const auto excerpt = clipWav(region, region.embedded, clock, clipFrames);
    auto excerptReader = wavReader(excerpt);
    if (!excerptReader || clipFrames != 4000 || excerptReader->lengthInSamples != clipFrames) return false;
    juce::MemoryBlock changed;
    {
        auto writer = wavWriter(changed, *excerptReader);
        juce::AudioBuffer<float> fill(2, int(clipFrames));
        fill.clear();
        for (int i = 0; i < clipFrames; ++i) fill.setSample(0, i, .25f);
        if (!writer || !writer->writeFromAudioSampleBuffer(fill, 0, int(clipFrames))) return false;
    }
    if (!editFile.replaceWithData(changed.getData(), changed.getSize()) ||
        prepareEditedWav(region, editFile, hash, replacement, clipFrames).failed()) return false;
    auto original = wavReader(region.embedded), merged = wavReader(replacement.embedded);
    juce::AudioBuffer<float> a(2, frames), b(2, frames);
    if (!merged || merged->lengthInSamples != frames || !merged->read(&b, 0, frames, 0, true, true)
        || !original->read(&a, 0, frames, 0, true, true)) return false;
    for (int i = 0; i < frames; ++i) {
        const bool inside = i >= region.startFrame && i < region.startFrame + clipFrames;
        if (std::abs(b.getSample(0, i) - (inside ? .25f : a.getSample(0, i))) > .0001f) return false;
    }
    region.embedded.reset();
    waveforms.load({region});
    return !waveforms.available(region);
}

bool savePng(const juce::File& file, juce::Component& component) {
    juce::MemoryOutputStream bytes;
    return juce::PNGImageFormat().writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), bytes)
        && file.replaceWithData(bytes.getData(), bytes.getDataSize());
}

int pianoCursorX(MainComponent& main) {
    auto img = main.createComponentSnapshot(main.getLocalBounds());
    // The playhead is continuous through the piano grid and performance lane.
    // Notes and the arrangement cursor cannot form this line in the lower pane.
    for (int x = 78; x < img.getWidth() - 18; ++x) {
        int run = 0;
        for (int y = img.getHeight() * 3 / 4; y < img.getHeight() - 35; ++y) {
            const auto c = img.getPixelAt(x, y);
            if (c.getRed() > 210 && c.getGreen() > 150 && c.getBlue() < 130) ++run;
            else run = 0;
            if (run > 28) return x;
        }
    }
    return -1;
}
}

int checkTransport(const juce::File& fixture, const juce::File& dir) {
    for (int pc=0;pc<12;++pc) {
        auto key = juce::JSON::parse(R"({"state":"tonal","mode":"minor"})");
        key.getDynamicObject()->setProperty("tonic",spelledClass(pc));
        const int minor=camelotCode(key);
        if (minor<1 || minor>12 || (pc==4 && minor!=9) || camelotCode(key,12)!=minor) return 127;
        key.getDynamicObject()->setProperty("mode","major");
        if(camelotCode(key,3)!=minor+12) return 128;
        key.getDynamicObject()->setProperty("state","unknown");
        if(camelotCode(key)!=0) return 129;
    }
    std::unique_ptr<OstfpPreview> song;
    juce::FileInputStream input(fixture);
    if (dir.createDirectory().failed() || readOstfpPreview(input, song).failed()) return 1;
    if (!checkWaveform(dir)) return 42;
    if (!checkPerformanceAudio()) return 46;
    auto score = juce::JSON::parse(song->sourceScore.toString());
    auto* timeline = score["timeline"].getDynamicObject();
    auto meter = score["timeline"]["meterChanges"][0].getDynamicObject();
    meter->setProperty("numerator", 8);
    meter->setProperty("denominator", 8);
    timeline->setProperty("tempoChanges", juce::JSON::parse(R"([{"at":0,"bpm":120},{"at":960,"bpm":60}])"));
    timeline->setProperty("sections", juce::JSON::parse(R"([{"id":"opening","name":"Opening","start":0,"duration":1440},{"id":"verse","name":"Verse","start":1440,"duration":2400}])"));
    timeline->setProperty("keyChanges", juce::JSON::parse(R"([{"id":"mod","at":960,"key":{"state":"tonal","tonic":{"step":"G","alter":0},"mode":"major"}}])"));
    auto harmony = score["harmony"].getDynamicObject();
    harmony->setProperty("scales", juce::JSON::parse(R"([{"id":"lyd","start":960,"duration":480,"label":"G Lydian","tonic":{"step":"G","alter":0},"intervals":[0,2,4,6,7,9,11]}])"));
    score["harmony"]["chords"][0].getDynamicObject()->setProperty("duration", 960);
    auto* fields = score["song"].getDynamicObject();
    fields->setProperty("scale", juce::JSON::parse(R"({"id":"base","label":"A natural minor","tonic":{"step":"A","alter":0},"intervals":[0,2,3,5,7,8,10]})"));
    fields->setProperty("credits", juce::JSON::parse(R"([{"id":"cr1","role":"composer","contributorId":"p1","label":"Music"}])"));
    score.getDynamicObject()->setProperty("contributors", juce::JSON::parse(R"([{"id":"p1","name":"First Artist","kind":"person"}])"));
    auto text = juce::JSON::toString(score);
    juce::MemoryInputStream raw(text.toRawUTF8(), text.getNumBytesAsUTF8(), false);
    if (readOstfPreview(raw, song).failed()) return 2;
    {
        TransportPanel panel;
        panel.setPosition(*song, song->durationTicks + 240, -1, 2);
        auto* key = dynamic_cast<juce::Label*>(panel.findChildWithID("key"));
        auto* scale = dynamic_cast<juce::Label*>(panel.findChildWithID("scale"));
        auto* badge = dynamic_cast<juce::Button*>(panel.findChildWithID("transport-camelot"));
        if (!key || key->getText() != "A major" || !badge || badge->getButtonText() != "11B"
            || !scale || scale->getText() != juce::String::charToString(0x2014)) return 130;
        if (!badge->onClick) return 131;
        badge->onClick();
        auto* popup = dynamic_cast<juce::CallOutBox*>(juce::Component::getCurrentlyModalComponent());
        if (!popup || !popup->findChildWithID("camelot-wheel")) return 132;
        popup->exitModalState(0);
    }
    if (song->clock.bpmAt(959) != 120 || song->clock.bpmAt(960) != 60
        || song->clock.nextChangeAfter(959) != 960 || song->clock.nextChangeAfter(960)
        || std::abs(song->clock.secondsAt(1920) - 1.5) > 1e-8 || std::abs(song->clock.tickAt(1.5) - 1920) > 1e-8
        || song->contextAt(TimelineLane::key, 960) != "G major"
        || song->contextAt(TimelineLane::chord, 959).isEmpty() || song->contextAt(TimelineLane::chord, 960).isNotEmpty()
        || song->contextAt(TimelineLane::scale, 960) != "G Lydian" || song->contextAt(TimelineLane::scale, 1440) != "A natural minor") return 3;
    const auto preRamp = song->sourceScore;
    juce::MemoryBlock previous;
    if (song->drawTempo(-1, 90, 700, 160, previous).wasOk() || preRamp != song->sourceScore) return 51;
    if (song->drawTempo(720, 150, 240, 90, previous).failed() || previous != preRamp
        || song->clock.bpmAt(239) != 120 || song->clock.bpmAt(240) != 90 || song->clock.bpmAt(480) != 120
        || song->clock.bpmAt(720) != 150 || song->clock.bpmAt(960) != 60) return 52;
    if (std::abs(song->clock.tickAt(song->clock.secondsAt(540)) - 540) > .0001) return 53;
    juce::MemoryOutputStream rampScore, rampPackage;
    SessionMix rampMix;
    rampMix.tracks.resize(song->trackLanes.size());
    if (writeOstfPreview(*song, rampScore).failed() || writeOstfpPreview(*song, rampMix, rampPackage).failed()) return 54;
    for (bool package : {false, true}) {
        const auto& out = package ? rampPackage : rampScore;
        juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
        std::unique_ptr<OstfpPreview> roundTrip;
        if ((package ? readOstfpPreview(in, roundTrip) : readOstfPreview(in, roundTrip)).failed()
            || roundTrip->clock.bpmAt(480) != 120 || roundTrip->clock.secondsAt(960) != song->clock.secondsAt(960)) return 55;
    }
    juce::MemoryOutputStream midiOut;
    if (writeSongMidi(*song, SmfFormat::multitrack, {}, midiOut).failed()) return 59;
    juce::MemoryInputStream midiIn(midiOut.getData(), midiOut.getDataSize(), false);
    juce::MidiFile midi;
    if (!midi.readFrom(midiIn) || midi.getNumTracks() == 0) return 60;
    juce::MidiMessageSequence tempos;
    midi.findAllTempoEvents(tempos);
    bool midpoint = false;
    for (auto* event : tempos)
        if (event->message.getTimeStamp() == 480 && std::abs(event->message.getTempoSecondsPerQuarterNote() - .5) < .000001) midpoint = true;
    if (!midpoint || tempos.getNumEvents() < 4) return 61;
    auto infoDraft = song->songInformation();
    infoDraft.title = "Combined edit";
    SongMusicEdit music;
    music.bpm = 1200;
    const auto rampBytes = song->sourceScore;
    if (song->applySongDetails(infoDraft, music, previous).wasOk() || song->sourceScore != rampBytes) return 56;
    music.bpm = 105;
    if (song->applySongDetails(infoDraft, music, previous).failed() || previous != rampBytes
        || song->clock.bpmAt(0) != 105 || song->title != infoDraft.title) return 57;
    if (song->restoreNoteScore(preRamp).failed()) return 58;
    PreviewTransport engine;
    juce::AudioBuffer<float> buffer(2, 400);
    if (engine.preparePlayback(*song, {}, {}, 8000, 400, 2, .5).failed()) return 43;
    for (int i = 0; i < 60; ++i) engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 1920 || song->clock.bpmAt(960) != 60) return 44;
    if (engine.preparePlayback(*song, {}, {}, 8000, 400).failed()) return 4;
    for (int i = 0; i < 30; ++i) engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 1920) return 5;
    engine.setLoop({0,96});
    engine.seekToTick(48);
    engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 48 || !engine.isRunning()) return 45;
    engine.setLoop({});
    engine.seekToTick(480);
    if (engine.positionTick() != 480 || std::abs(engine.cursor().seconds - .25) > 1e-8) return 23;
    engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 576) return 24;
    engine.seekToTick(1920);
    engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    PreviewTransport::Cursor parked{};
    engine.stop(&parked);
    if (parked.tick != 1968 || std::abs(parked.seconds - 1.55) > 1e-8) return 25;
    auto info = song->songInformation();
    info.title = "After Hours";
    info.subtitle = "Studio session";
    info.description = "Transport check";
    info.credits.front().name = "North Studio";
    info.credits.push_back({{}, "p1", "arranger", "North Studio"});
    const auto before = song->sourceScore;
    auto invalid = info;
    invalid.credits.front().role = "bad role";
    if (song->applySongInformation(invalid).wasOk() || song->sourceScore != before || !song->insertGeneratorPattern(0, 2)
        || song->applySongInformation(info).failed() || !song->addedClips.empty()) return 6;
    const auto noteCount = song->pitchedNotes[0].size();
    const auto after = song->sourceScore;
    if (song->applySongInformation(song->songInformation()).failed() || song->sourceScore != after) return 7;
    const auto doc = juce::JSON::parse(after.toString());
    if (doc["song"]["credits"][0]["label"].toString() != "Music" || doc["contributors"].size() != 1
        || doc["contributors"][0]["kind"].toString() != "person") return 8;
    const auto file = dir.getChildFile("after-hours.ostf");
    juce::MemoryOutputStream plain, packed;
    if (writeOstfPreview(*song, plain).failed() || !file.replaceWithData(plain.getData(), plain.getDataSize())) return 9;
    SessionMix mix;
    mix.tracks.resize(song->trackLanes.size());
    if (writeOstfpPreview(*song, mix, packed).failed()) return 10;
    juce::MemoryInputStream packedInput(packed.getData(), packed.getDataSize(), false);
    if (readOstfpPreview(packedInput, song).failed() || song->title != info.title || song->songInformation().credits.size() != 2
        || song->pitchedNotes[0].size() != noteCount) return 11;
    MainComponent main;
    main.setSize(1280, 800);
    main.setVisible(true);
    if (!main.openDocument(file)) return 12;
    if (!main.getName().contains(file.getFileName()) || !main.getName().contains(info.title)) return 13;
    main.displayTransportPosition(960);
    auto* deck = main.findChildWithID("transport");
    const auto reads = [&](const char* id, const juce::String& expected) {
        auto* value = deck ? dynamic_cast<juce::Label*>(deck->findChildWithID(id)) : nullptr;
        return value && value->getText() == expected;
    };
    if (!reads("bar-beat-tick", "001.03.000") || !reads("timecode", "00:00:00:15") || !reads("meter", "8/8")
        || !reads("key", "G major") || !reads("scale", "G Lydian") || !reads("tempo", "60")
        || !reads("chord", juce::String::charToString(0x2014)) || !reads("section", "Opening")) return 18;
    const auto first = pianoCursorX(main);
    main.displayTransportPosition(5760);
    if (first < 0 || pianoCursorX(main) <= first || !savePng(dir.getChildFile("transport-piano.png"), main)) return 14;
    if (!reads("section", juce::String::charToString(0x2014))) return 26;
    main.displayTransportPosition(1440);
    if (!reads("section", "Verse")) return 27;
    juce::Slider* arrangementZoom = nullptr;
    for (auto* child : main.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider && slider->getTitle() == TRANS("Timeline horizontal zoom"))
            arrangementZoom = slider;
    auto* arrangementScroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-horizontal-scroll"));
    auto* pianoRuler = dynamic_cast<ArrangementRuler*>(main.findChildWithID("piano-ruler"));
    if (!arrangementZoom || !arrangementScroll || !pianoRuler) return 19;
    const auto full = arrangementScroll->getMaximumRangeLimit() - arrangementScroll->getMinimumRangeLimit();
    if (std::abs(arrangementScroll->getCurrentRangeSize() - std::min(10.0, full)) > .01) return 90;
    arrangementZoom->setValue(0.63, juce::sendNotificationSync);
    if (std::abs(pianoRuler->visibleBars().getLength() - std::pow(full, 0.37)) > 0.001) return 20;
    arrangementZoom->setValue(1, juce::sendNotificationSync);
    arrangementScroll->setCurrentRangeStart(full, juce::sendNotificationSync);
    if (pianoRuler->visibleBars() != arrangementScroll->getCurrentRange() || pianoRuler->visibleBars().getLength() != 1) return 21;
    arrangementZoom->setValue(0, juce::sendNotificationSync);
    if (arrangementScroll->getCurrentRangeStart() != 1 || arrangementScroll->getCurrentRangeSize() != full
        || arrangementScroll->isEnabled()) return 22;
    main.displayTransportPosition(10 * song->ticksPerBar + 960);
    if (pianoCursorX(main) < 0) return 15;
    ostfstudio::ProjectAudio padded;
    padded.startTick = 240;
    padded.durationTicks = 480;
    padded.resizeBounds(120, 840, 720);
    if (padded.sourceTick() != 240 || padded.durationTicks != 720 || padded.timing.sourceLimitTicks != 480) return 163;
    const auto button = [&](const char* id) { return dynamic_cast<juce::Button*>(deck->findChildWithID(id)); };
    auto* home = button("return-to-start");
    auto* end = button("go-to-end");
    auto* metronome = dynamic_cast<TransportPanel*>(deck);
    if (!end || !metronome || metronome->countIn.getParentComponent() != &metronome->click
        || metronome->countIn.getNumItems() != 9) return 161;
    end->onClick();
    if (!reads("bar-beat-tick", juce::String(song->bars.endBar).paddedLeft('0', 3) + ".01.000")) return 162;
    auto* back = button("rewind");
    auto* next = button("fast-forward");
    auto* follow = button("follow-playhead");
    if (!home || !back || !next || !follow || !follow->getToggleState()) return 28;
    home->onClick();
    arrangementZoom->setValue(1, juce::sendNotificationSync);
    next->onClick();
    if (!reads("bar-beat-tick", "002.01.000") || !arrangementScroll->getCurrentRange().contains(2.0)) return 29;
    const auto arrangementPage = arrangementScroll->getCurrentRange();
    const auto pianoPage = pianoRuler->visibleBars();
    follow->setToggleState(false, juce::sendNotificationSync);
    main.displayTransportPosition(10 * song->ticksPerBar + 960);
    if (arrangementScroll->getCurrentRange() != arrangementPage || pianoRuler->visibleBars() != pianoPage) return 30;
    follow->setToggleState(true, juce::sendNotificationSync);
    if (!arrangementScroll->getCurrentRange().contains(11.25) || !pianoRuler->visibleBars().contains(11.25)) return 31;
    main.displayTransportPosition(song->playbackEndTick() - 960);
    next->onClick();
    next->onClick();
    if (!reads("bar-beat-tick", juce::String(song->bars.endBar).paddedLeft('0', 3) + ".01.000")
        || arrangementScroll->getCurrentRange().getEnd() != song->bars.endBar) return 32;
    back->onClick();
    if (!reads("bar-beat-tick", juce::String(song->bars.endBar - 1).paddedLeft('0', 3) + ".01.000")) return 33;
    home->onClick();
    back->onClick();
    if (!reads("section", "Opening") || !reads("bar-beat-tick", "001.01.000")
        || arrangementScroll->getCurrentRangeStart() != 1 || pianoRuler->visibleBars().getStart() != 1) return 34;
    ArrangementRuler* ruler = nullptr;
    for (auto* child : main.getChildren())
        if (auto* r = dynamic_cast<ArrangementRuler*>(child->findChildWithID("arrangement-ruler"))) ruler = r;
    if (!ruler || !ruler->isVisible()) return 35;
    const auto origin = main.getLocalPoint(ruler, juce::Point<int>{ruler->headerWidth, 0});
    if (origin.x != pianoRuler->getX() || ruler->getWidth() - ruler->headerWidth != pianoRuler->getWidth()) return 47;
    const int width = ruler->getWidth() - ruler->headerWidth;
    const int centre = ruler->headerWidth + width / 2;
    const auto pointer = [&](int x, int origin, bool down, int modifiers = 0) {
        const auto time = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {float(x), 9.f},
            (down ? juce::ModifierKeys::leftButtonModifier : 0) | modifiers, 1.f, 0, 0, 0, 0, ruler, ruler, time,
            {float(origin), 9.f}, time, 1, x != origin);
    };
    arrangementScroll->setCurrentRangeStart(4, juce::sendNotificationSync);
    ruler->mouseDown(pointer(centre, centre, true)); ruler->mouseUp(pointer(centre, centre, false));
    const auto clicked = ruler->playheadBar();
    if (std::abs(clicked - 4.5) > .003 || arrangementScroll->getCurrentRangeStart() != 4) return 36;
    const int left = centre - width / 4;
    ruler->mouseDown(pointer(centre, centre, true)); ruler->mouseDrag(pointer(left, centre, true));
    ruler->mouseUp(pointer(left, centre, false));
    if (std::abs(arrangementScroll->getCurrentRangeStart() - 4.25) > .01 || ruler->playheadBar() != clicked) return 37;
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -0.5f;
    auto wheelEvent = pointer(centre, centre, false, juce::ModifierKeys::shiftModifier).getEventRelativeTo(&main);
    main.mouseWheelMove(wheelEvent, wheel);
    if (arrangementScroll->getCurrentRangeStart() <= 4.25 || ruler->playheadBar() != clicked) return 38;
    if (!main.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)) || ruler->playheadBar() != 1) return 39;
    ruler = dynamic_cast<ArrangementRuler*>(main.findChildWithID("piano-ruler"));
    if (!ruler || !ruler->isVisible()) return 40;
    arrangementZoom->setValue(1, juce::sendNotificationSync);
    arrangementScroll->setCurrentRangeStart(7, juce::sendNotificationSync);
    const auto pianoRange = pianoRuler->visibleBars();
    const auto half = ruler->getWidth() / 2;
    ruler->mouseDown(pointer(half, half, true)); ruler->mouseUp(pointer(half, half, false));
    if (std::abs(ruler->playheadBar() - 7.5) > .003 || pianoRuler->visibleBars() != pianoRange
        || !arrangementScroll->getCurrentRange().contains(ruler->playheadBar()) || pianoCursorX(main) < 0) return 41;
    home->onClick();
    main.setSize(680, 550);
    main.displayTransportPosition(960);
    if (!savePng(dir.getChildFile("transport-compact.png"), main) || pianoCursorX(main) < 0) return 16;
    bool applied = false;
    SongInformationPanel editor(*song, [&](const SongInformation& edited, const SongMusicEdit&) {
        applied = edited.title == info.title && edited.credits.size() == 3 && edited.credits.back().role == "composer";
        return juce::Result::ok();
    });
    if (!savePng(dir.getChildFile("song-information.png"), editor)) return 17;
    const auto press = [&](auto&& visit, juce::Component& item, const char* caption) -> bool {
        if (auto* b = dynamic_cast<juce::TextButton*>(&item); b && b->getButtonText() == juce::translate(caption)) { b->onClick(); return true; }
        for (auto* c : item.getChildren()) if (visit(visit, *c, caption)) return true;
        return false;
    };
    for (const auto* caption : {"Add credit", "Apply"})
        if (!press(press, editor, caption)) return 19;
    return applied ? 0 : 19;
}
