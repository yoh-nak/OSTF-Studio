#include "ui/MainComponent.h"
#include <future>
#include <iostream>
#include "ui/MediaImport.h"
#include "ui/GlobalTimelineEdit.h"
#include <juce_audio_basics/juce_audio_basics.h>

using namespace ostfstudio;
namespace {
bool save(const juce::File& dest, const std::function<juce::Result(juce::OutputStream&)>& write) {
    juce::MemoryOutputStream out;
    return write(out).wasOk() && dest.replaceWithData(out.getData(), out.getDataSize());
}
class ImportTestProvider final : public AnalysisProvider {
public:
    bool authenticated = true, invalid = false;
    juce::String name() const override { return "Test"; }
    bool connected() const override { return authenticated; }
    juce::Result disconnect() const override { return juce::Result::ok(); }
    juce::Result connect(const SignInLink&) const override { return juce::Result::ok(); }
    juce::Result request(const juce::String& prompt, const juce::String&, const juce::File& image, juce::var& reply) const override {
        if (prompt.startsWith("Read the printed score")) {
            juce::FileInputStream page(image);
            const auto signature = page.readIntBigEndian();
            page.setPosition(16);
            const auto width = page.readIntBigEndian(), height = page.readIntBigEndian();
            if (signature != static_cast<int>(0x89504e47) || std::max(width,height) != 2000 || std::min(width,height) <= 0)
                return juce::Result::fail("Score page was not rendered");
            reply = juce::JSON::parse(R"({"length":1920,"bpm":90,"beats":4,"unit":4,"notes":[{"channel":1,"name":"Piano","pitch":60,"start":480,"duration":480,"velocity":90}]})");
        } else if (prompt.startsWith("Assign each tonal onset")) {
            reply = juce::JSON::parse(R"({"parts":[3,3]})");
        } else {
            const int count = juce::StringArray::fromLines(prompt).size();
            if (count < 33) return juce::Result::fail("Incomplete evidence");
            reply = juce::JSON::parse(prompt.contains("channel=10")
                ? R"({"parts":["Keys","Kit"],"chords":[]})" : R"({"parts":["Lead"],"chords":[]})");
        }
        if (invalid) reply = juce::JSON::parse(R"({"parts":[],"chords":[]})");
        return juce::Result::ok();
    }
};
bool checkLongAudio(const juce::File& dir) {
    constexpr int rate = 8000, silent = 513 * rate, frames = silent + rate / 2;
    juce::MemoryOutputStream out;
    out.write("RIFF", 4); out.writeInt(36 + frames*2); out.write("WAVEfmt ",8); out.writeInt(16);
    out.writeShort(1); out.writeShort(1); out.writeInt(rate); out.writeInt(2*rate); out.writeShort(2); out.writeShort(16);
    out.write("data",4); out.writeInt(frames*2); out.writeRepeatedByte(0, silent*2);
    for (int i=0; i<rate/2; ++i) out.writeShort(short(14000 * std::sin(juce::MathConstants<double>::twoPi * 440 * i / rate)));
    const auto audio = dir.getChildFile("long-tail.wav"), score = dir.getChildFile("long-tail.ostf");
    std::unique_ptr<OstfpPreview> imported;
    juce::String info;
    if (!audio.replaceWithData(out.getData(),out.getDataSize()) || analyseScoreImport(audio, imported, info).failed()
        || !imported || imported->bars.endBar <= 257 || saveMediaImport(*imported, score).failed()) return false;
    juce::FileInputStream in(score);
    const auto hasLastNote = [](const OstfpPreview& song) {
        for (const auto& track : song.pitchedNotes)
            for (const auto& note : track)
                if (note.pitch == 69 && note.startTick > 256 * song.ticksPerBar) return true;
        return false;
    };
    if (readOstfPreview(in, imported).failed() || !hasLastNote(*imported)) return false;
    // Run the same check with a compressed fixture when the LAME encoder is available.
    const auto encoder = analysisTool("lame");
    if (!encoder.existsAsFile()) return true;
    const auto mp3 = dir.getChildFile("long-tail.mp3");
    if (auto result = runAnalysisTool({encoder.getFullPathName(), "--silent", "-b", "32", audio.getFullPathName(), mp3.getFullPathName()}, 30000); result.failed()) {
        std::cerr << result.getErrorMessage() << std::endl;
        return false;
    }
    if (auto result = analyseScoreImport(mp3, imported, info); result.failed()) {
        std::cerr << "MP3 analysis: " << result.getErrorMessage() << std::endl;
        return false;
    }
    if (imported && hasLastNote(*imported)) return true;
    std::cerr << "MP3 last note missing. " << info << std::endl;
    return false;
}
bool checkImport(const juce::File& dir, MainComponent& main) {
    auto file = dir.getChildFile("phrase.mid");
    juce::MidiFile song;
    song.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence seq;
    for (int channel : {1, 10}) {
        seq.addEvent(juce::MidiMessage::noteOn(channel, channel == 1 ? 64 : 38, static_cast<juce::uint8>(91)), 480);
        seq.addEvent(juce::MidiMessage::noteOff(channel, channel == 1 ? 64 : 38), 1200);
    }
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(2,2),1920);
    seq.addEvent(juce::MidiMessage::endOfTrack(),2160);
    song.addTrack(seq);
    juce::MemoryOutputStream midi;
    if (!song.writeTo(midi, 0) || !file.replaceWithData(midi.getData(), midi.getDataSize()))
        return false;
    std::unique_ptr<OstfpPreview> project;
    juce::String info;
    if (prepareMediaImport(file, project, info).failed() || project->sourceTrackCount() != 2 ||
        project->pitchedNotes[0].size() != 1 || project->drumNotes[1].size() != 1 ||
        project->pitchedNotes[0][0].startTick != 480 || project->pitchedNotes[0][0].endTick != 1200 ||
        project->sourceMidiChannels[1] != 10 || project->meters.size()!=2 || project->meters.back().numerator!=2 || saveMediaImport(*project, dir.getChildFile("phrase.ostf")).failed())
        return false;
    auto provider = std::make_shared<ImportTestProvider>();
    std::unique_ptr<OstfpPreview> aiSong;
    if (analyseScoreImport(file, aiSong, info, provider).failed() || aiSong->pitchedNotes[0][0].startTick != 480
        || aiSong->drumNotes[1][0].pitch != 38 || aiSong->meters.size() != 2) return false;
    provider->invalid = true;
    if (analyseScoreImport(file, aiSong, info, provider).wasOk()) return false;
    provider->invalid = false; provider->authenticated = false;
    if (analyseScoreImport(file, aiSong, info, provider).wasOk()) return false;
    provider->authenticated = true;
    if (!checkLongAudio(dir) || !scoreImportFileFilter().contains("*.pdf")) return false;
    juce::MemoryOutputStream wav;
    wav.write("RIFF", 4);
    wav.writeInt(36 + 1600);
    wav.write("WAVEfmt ", 8);
    wav.writeInt(16);
    wav.writeShort(1);
    wav.writeShort(1);
    wav.writeInt(8000);
    wav.writeInt(16000);
    wav.writeShort(2);
    wav.writeShort(16);
    wav.write("data", 4);
    wav.writeInt(1600);
    wav.writeRepeatedByte(0, 1600);
    file = dir.getChildFile("reference.wav");
    if (!file.replaceWithData(wav.getData(), wav.getDataSize()) || prepareMediaImport(file, project, info).failed())
        return false;
    const auto dest = dir.getChildFile("reference.ostf");
    if (saveMediaImport(*project, dest).failed())
        return false;
    juce::FileInputStream saved(dest);
    if (readOstfPreview(saved, project).failed() || project->audio.size() != 1 ||
        project->trackKinds.front() != TrackKind::audio || !project->pitchedNotes.front().empty() ||
        dir.getChildFile(project->audio[0].path).getSize() != file.getSize())
        return false;
    if (prepareMediaImport(file, project, info, true).wasOk())
        return false;
    juce::MemoryOutputStream tones;
    constexpr int frames = 12000;
    tones.write("RIFF", 4);
    tones.writeInt(36 + frames * 2);
    tones.write("WAVEfmt ", 8);
    tones.writeInt(16);
    tones.writeShort(1);
    tones.writeShort(1);
    tones.writeInt(8000);
    tones.writeInt(16000);
    tones.writeShort(2);
    tones.writeShort(16);
    tones.write("data", 4);
    tones.writeInt(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const double hz = i < 4800 ? 440. : i >= 6400 ? 523.251 : 0.;
        tones.writeShort(static_cast<short>(16000 * std::sin(juce::MathConstants<double>::twoPi * hz * i / 8000)));
    }
    // Two contrasting triads followed by silence exercise the analysis, import and edit paths.
    juce::MemoryOutputStream harmony;
    harmony.write(tones.getData(), 44);
    for (int i = 0; i < frames; ++i) {
        double sample = 0;
        if (i < 8000)
            for (int p : i < 4000 ? std::array<int, 3>{60, 64, 67} : std::array<int, 3>{62, 65, 69})
                sample += 5200 *
                          std::sin(juce::MathConstants<double>::twoPi * 440 * std::pow(2., (p - 69) / 12.) * i / 8000.);
        harmony.writeShort(static_cast<short>(sample));
    }
    const auto mixed = dir.getChildFile("triads.wav");
    std::unique_ptr<OstfpPreview> analysed;
    if (!mixed.replaceWithData(harmony.getData(), harmony.getDataSize()) ||
        prepareMediaImport(mixed, analysed, info, false, true).failed())
        return false;
    std::unique_ptr<OstfpPreview> localWaves, localNotes;
    if (analyseImportParts(mixed, false, localWaves, localNotes, info).failed() || !localWaves || !localNotes ||
        localWaves->sourceTrackCount() != 4 || localNotes->sourceTrackCount() != 4 ||
        localNotes->trackKinds[0] != TrackKind::drum || localWaves->audio.size() != 4 ||
        localNotes->trackKinds[3] != TrackKind::pitched)
        return false;
    juce::WavAudioFormat format;
    juce::AudioBuffer<float> sum(1, frames), partAudio(1, frames);
    sum.clear();
    for (const auto &part : localWaves->audio) {
        const auto &bytes = part.embedded;
        std::unique_ptr<juce::AudioFormatReader> reader(
            format.createReaderFor(new juce::MemoryInputStream(bytes, false), true));
        if (!reader || reader->lengthInSamples != frames || reader->sampleRate != 8000 ||
            !reader->read(&partAudio, 0, frames, 0, true, true) || partAudio.getMagnitude(0, frames) < .001f)
            return false;
        sum.addFrom(0, 0, partAudio, 0, 0, frames);
    }
    auto sourceWav = mixed.createInputStream();
    std::unique_ptr<juce::AudioFormatReader> original(format.createReaderFor(sourceWav.release(), true));
    if (!original || !original->read(&partAudio, 0, frames, 0, true, true))
        return false;
    sum.addFrom(0, 0, partAudio, 0, 0, frames, -1.f);
    if (sum.getMagnitude(0, frames) > .001f)
        return false;
    const auto scoreData = juce::JSON::parse(analysed->sourceScore.toString());
    const auto resultChords = scoreData["harmony"]["chords"];
    if (resultChords.size() != 2 || resultChords[0]["label"] != "C" || resultChords[1]["label"] != "Dm" ||
        static_cast<juce::int64>(resultChords[1]["start"]) != 480 ||
        static_cast<juce::int64>(resultChords[1]["duration"]) != 480)
        return false;
    juce::MemoryBlock before;
    const auto unchanged = analysed->sourceScore;
    auto edited = resultChords[1].clone();
    if (editChordSymbol(edited, "G7").failed() ||
        analysed->changeGlobalEvents(TimelineLane::chord, {resultChords[1]}, {edited}, before).failed() ||
        before != unchanged || analysed->contextAt(TimelineLane::chord, 600) != "G7")
        return false;
    if (analysed->changeGlobalEvents(TimelineLane::chord, {edited}, {}, before).failed() ||
        analysed->changeGlobalEvents(TimelineLane::chord, {}, {edited}, before).failed() ||
        analysed->changeGlobalEvents(TimelineLane::chord, {}, {edited}, before).wasOk())
        return false;
    const auto tempo = scoreData["timeline"]["tempoChanges"][0];
    if (analysed->changeGlobalEvents(TimelineLane::tempo, {tempo}, {}, before).wasOk())
        return false;
    auto tags = analysed->songInformation();
    tags.genres = juce::StringArray{"House", "Jazz"};
    tags.moods = juce::StringArray{"Bright"};
    if (analysed->applySongInformation(tags).failed() || analysed->songInformation().genres != tags.genres)
        return false;
    auto stems = dir.getChildFile("stems");
    if (stems.createDirectory().failed())
        return false;
    for (auto name : {"drums.wav", "bass.wav", "other.wav", "vocals.wav"})
        if (!stems.getChildFile(name).replaceWithData(harmony.getData(), harmony.getDataSize()))
            return false;
    std::unique_ptr<OstfpPreview> separated;
    if (prepareStemImport(stems, separated, info).failed() || separated->audio.size() != 4)
        return false;
    if (placeImportTrack(*separated, 0, *analysed, 0, 0, before).wasOk() ||
        placeImportTrack(*separated, 0, *analysed, 0, 0, before, true).failed() || analysed->audio.size() != 1 ||
        analysed->audio.front().localFile != stems.getChildFile("drums.wav"))
        return false;
    if (analysed->audio.front().regionId == separated->audio.front().regionId) return false;
    if (placeImportTrack(*analysed, -1, *separated, {}, 0, before).failed() ||
        juce::JSON::parse(separated->sourceScore.toString())["harmony"]["chords"].size() != 2)
        return false;
    main.openImportedSong(std::move(analysed));
    if (!main.getName().contains(TRANS("Unsaved project")) || !main.getName().contains("triads") ||
        dir.getChildFile("triads.ostf").exists() || dir.getChildFile("triads.media").exists())
        return false;
    SessionMix stemMix;
    stemMix.tracks.resize(4);
    stemMix.audioTracks.assign(4, true);
    juce::MemoryOutputStream package;
    if (writeOstfpPreview(*separated, stemMix, package).failed()) return false;
    juce::MemoryInputStream packaged(package.getData(), package.getDataSize(), false);
    std::unique_ptr<OstfpPreview> reopenedStems;
    if (readOstfpPreview(packaged, reopenedStems).failed() || reopenedStems->audio.size() != 4 ||
        reopenedStems->audio.back().embedded.isEmpty()) return false;
    auto savedStems = dir.getChildFile("stems.ostf");
    if (saveMediaImport(*separated, savedStems).failed())
        return false;
    juce::FileInputStream stemsInput(savedStems);
    if (readOstfPreview(stemsInput, separated).failed() || separated->audio.size() != 4)
        return false;
    auto melody = dir.getChildFile("melody.wav");
    if (!melody.replaceWithData(tones.getData(), tones.getDataSize()) ||
        prepareMediaImport(melody, project, info, true).failed() || project->pitchedNotes.size() != 2 ||
        project->pitchedNotes[1].size() != 2 || project->pitchedNotes[1][0].pitch != 69 ||
        project->pitchedNotes[1][1].pitch != 72 || std::abs(project->pitchedNotes[1][1].startTick - 768) > 60)
        return false;
    std::unique_ptr<OstfpPreview> draft;
    if (analyseScoreImport(melody, draft, info).failed() || !draft || !draft->audio.empty() ||
        draft->sourceTrackCount() != 1 || juce::String::fromUTF8(draft->trackLanes[0].label).contains(TRANS("(estimated)")) ||
        draft->pitchedNotes[0].size() != 2 ||
        draft->pitchedNotes[0][0].pitch != 69 || draft->pitchedNotes[0][1].pitch != 72 ||
        juce::JSON::parse(draft->sourceScore.toString())["harmony"]["chords"].size() != 0) {
        std::cerr << "Audio melody projection: " << info << std::endl;
        return false;
    }
    if (analyseScoreImport(melody, aiSong, info, provider).failed() || aiSong->pitchedNotes[0].size() != 2
        || juce::String::fromUTF8(aiSong->trackLanes[0].label) != "Lead") return false;
    // A sustained harmonic reed and a pure sine exercise both the candidate and abstention paths.
    juce::MemoryOutputStream reed;
    reed.write(tones.getData(), 44);
    constexpr std::array<double, 6> reedGains{1., .14, .52, .075, .22, .06};
    for (int i = 0; i < frames; ++i) {
        double sample = 0;
        for (size_t h = 0; h < reedGains.size(); ++h)
            sample += reedGains[h] * std::sin(juce::MathConstants<double>::twoPi * 220. * (h + 1) * i / 8000.);
        reed.writeShort(short(10000. * sample));
    }
    const auto reedFile = dir.getChildFile("reed.wav");
    if (!reedFile.replaceWithData(reed.getData(), reed.getDataSize()) || analyseScoreImport(reedFile, draft, info).failed() ||
        draft->sourceTrackCount() != 1 || !juce::String::fromUTF8(draft->trackLanes[0].label).contains(TRANS("Reed"))) return false;
    juce::AiffAudioFormat aiff;
    const auto aiffFile = dir.getChildFile("phrase.aiff");
    auto aiffOutput = aiffFile.createOutputStream();
    if (!aiffOutput || !aiffOutput->setPosition(0) || aiffOutput->truncate().failed()) return false;
    std::unique_ptr<juce::OutputStream> destination = std::move(aiffOutput);
    auto writer = aiff.createWriterFor(destination, juce::AudioFormatWriterOptions{}.withSampleRate(8000)
        .withNumChannels(1).withBitsPerSample(16));
    std::unique_ptr<juce::AudioFormatReader> toneReader(format.createReaderFor(melody.createInputStream().release(), true));
    if (!writer || !toneReader || !writer->writeFromAudioReader(*toneReader, 0, int(toneReader->lengthInSamples))) return false;
    writer.reset();
    if (analyseScoreImport(aiffFile, draft, info).failed() || draft->sourceTrackCount() != 1 ||
        draft->pitchedNotes[0].size() != 2 || !scoreImportFileFilter().containsIgnoreCase("*.aif")) return false;
    if (analyseScoreImport(mixed, draft, info).failed() || !draft->audio.empty() ||
        draft->contextAt(TimelineLane::chord, 0) != "C" || draft->contextAt(TimelineLane::chord, 600) != "Dm" ||
        draft->sourceTrackCount() < 2) return false;
    // The new-track target uses the same drop callback as a drag from the detached result window.
    ImportResultRow sourceRow("Melody", *draft, 0);
    auto* zone = main.findChildWithID("import-new-track");
    if (!zone || zone->getWidth() <= 0) return false;
    auto& target = static_cast<juce::DragAndDropTarget&>(main);
    juce::DragAndDropTarget::SourceDetails drop("ostf-import-result", &sourceRow,
        zone->getBounds().getCentre().withX(zone->getX() + 8));
    juce::ComboBox* picker = nullptr;
    for (auto* child : main.getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*>(child); box && box->getTitle() == TRANS("Arrangement track")) picker = box;
    if (!picker) return false;
    const auto oldCount = picker->getNumItems();
    target.itemDragMove(drop);
    if (!zone->isVisible()) return false;
    target.itemDropped(drop);
    if (zone->isVisible() || picker->getNumItems() != oldCount + 1) return false;
    if (analyseScoreImport(file, draft, info).wasOk()) return false; // Silent WAV
    if (analyseScoreImport(dir.getChildFile("phrase.mid"), draft, info).failed() || draft->sourceTrackCount() != 2 || draft->drumNotes[1].size() != 1) return false;
    if (analyseImportParts(melody, false, localWaves, localNotes, info).failed() ||
        std::none_of(localNotes->pitchedNotes[3].begin(), localNotes->pitchedNotes[3].end(),
                     [](const auto &n) { return n.pitch == 69; }))
        return false;
    std::unique_ptr<OstfpPreview> notesTarget;
    if (prepareMediaImport(dir.getChildFile("phrase.mid"), notesTarget, info).failed()) return false;
    if (notesTarget->slotsByTrack[1].size() != 16)
        return false;
    const auto originalHits = notesTarget->drumNotes[1].size();
    if (placeImportTrack(*notesTarget, 1, *notesTarget, 1, 0, before).failed() ||
        notesTarget->drumNotes[1].size() != originalHits * 2)
        return false;
    const auto snare = std::find_if(notesTarget->slotsByTrack[1].begin(), notesTarget->slotsByTrack[1].end(),
                                    [](const auto &p) { return p.pitch == 38; }) -
                       notesTarget->slotsByTrack[1].begin();
    if (notesTarget->editDrumPad(1, int(snare), {}, before).wasOk() ||
        notesTarget->editDrumPad(1, int(snare), 74, before).failed() || notesTarget->drumNotes[1].front().pitch != 74 ||
        notesTarget->editDrumPad(1, -1, 74, before).wasOk() || notesTarget->editDrumPad(1, -1, 75, before).failed() ||
        notesTarget->slotsByTrack[1].size() != 17 || notesTarget->editDrumPad(1, 16, {}, before).failed() ||
        notesTarget->slotsByTrack[1].size() != 16)
        return false;
    std::unique_ptr<OstfpPreview> kitCopy, freshKit;
    if (notesTarget->editTracks(TrackKind::drum,{},kitCopy,1).failed() ||
        kitCopy->editDrumPad(2,int(snare),76,before).failed() || kitCopy->drumNotes[1].front().pitch!=74 ||
        kitCopy->drumNotes[2].front().pitch!=76 || kitCopy->editTracks(TrackKind::drum,{},freshKit).failed() ||
        freshKit->slotsByTrack[3].size()!=16 || freshKit->slotsByTrack[3].front().pitch!=36) return false;
    std::unique_ptr<OstfpPreview> combined, reopened;
    if (combineImportParts(*localWaves,*localNotes,combined).failed() || combined->sourceTrackCount()!=8 ||
        !combined->savedMix->tracks[7].muted || combined->savedMix->tracks[0].muted) return false;
    juce::MemoryOutputStream savedReview;
    if (writeOstfpPreview(*combined,*combined->savedMix,savedReview).failed()) return false;
    juce::MemoryInputStream savedInput(savedReview.getData(),savedReview.getDataSize(),false);
    if (readOstfpPreview(savedInput,reopened).failed() || reopened->audio.size()!=4 ||
        !reopened->savedMix->tracks[7].muted || reopened->pitchedNotes[7].empty()) return false;
    const auto oldTempo = juce::JSON::parse(notesTarget->sourceScore.toString())["timeline"]["tempoChanges"][0].clone();
    auto slower = oldTempo.clone();
    slower.getDynamicObject()->setProperty("bpm", 60);
    if (notesTarget->changeGlobalEvents(TimelineLane::tempo, {oldTempo}, {slower}, before).failed() ||
        placeImportTrack(*project, 1, *notesTarget, 0, 120, before).failed()) return false;
    std::vector<PitchedNote> imported;
    const auto placement = juce::JSON::parse(notesTarget->sourceScore.toString())["tracks"][0]["clips"];
    const auto id = placement.getArray()->getLast()["id"].toString();
    for (const auto& n : notesTarget->pitchedNotes[0]) if (n.clipId == id) imported.push_back(n);
    if (imported.size() != 2) return false;
    for (std::size_t i = 0; i < imported.size(); ++i) {
        const double originalTime = project->clock.secondsAt(project->pitchedNotes[1][i].startTick);
        const double placedTime = notesTarget->clock.secondsAt(imported[i].startTick) - notesTarget->clock.secondsAt(120);
        // MIDI ticks are integral. Preserve the transcription's initial offset within half a destination tick.
        if (std::abs(placedTime - originalTime) > 0.500001 / notesTarget->ppq) return false;
    }
    const auto previousEnd = notesTarget->playbackEndTick();
    const auto previousScore = notesTarget->sourceScore;
    if (placeImportTrack(*project, 1, *notesTarget, 0, previousEnd, before).failed()
        || notesTarget->durationTicks <= previousEnd || before != previousScore) return false;
    juce::MemoryOutputStream extended;
    if (writeOstfPreview(*notesTarget, extended).failed()) return false;
    juce::MemoryInputStream roundTrip(extended.getData(), extended.getDataSize(), false);
    std::unique_ptr<OstfpPreview> extendedScore;
    if (readOstfPreview(roundTrip, extendedScore).failed() || extendedScore->durationTicks != notesTarget->durationTicks
        || extendedScore->pitchedNotes[0].back().endTick <= previousEnd
        || notesTarget->restoreNoteScore(before).failed() || notesTarget->sourceScore != previousScore) return false;
    PreviewTransport player;
    player.setOutputMix({});
    player.setChannelMix(0, {}, true, 0);
    if (player.preparePlayback(*project, {}, {}, 8000, 128).failed()) return false;
    juce::AudioBuffer<float> block(2, 128);
    const auto wavLevelAt = [&](double seconds) {
        player.seekToTick(std::llround(project->clock.tickAt(seconds)));
        player.renderBlock(block.getArrayOfWritePointers(), 2, block.getNumSamples());
        return block.getMagnitude(0, 0, block.getNumSamples());
    };
    if (wavLevelAt(.9) < .4f || wavLevelAt(.65) != 0.0f || wavLevelAt(.1) < .4f) return false;
    player.stop();
    auto score = dir.getChildFile("melody.ostf");
    if (saveMediaImport(*project, score).failed())
        return false;
    juce::FileInputStream newSong(score);
    return readOstfPreview(newSong, project).wasOk() && project->trackKinds[1] == TrackKind::pitched &&
           project->pitchedNotes[1].size() == 2;
}
} // namespace
static bool checkInterchange(const juce::File& demo, const juce::File& dir) {
    const auto source = demo.getParentDirectory().getSiblingFile("score-interchange").getChildFile("lantern-walk.musicxml");
    const auto xml = dir.getChildFile("song.musicxml");
    const auto scoreFile = dir.getChildFile("lantern.ostf");
    juce::String info;
    std::unique_ptr<OstfpPreview> project;
    const auto load = [&](const juce::File& f) {
        auto r = analyseScoreImport(f,project,info);
        if (r.failed()) std::cerr << f.getFileName() << ": " << r.getErrorMessage() << "\n";
        return r.wasOk() && project;
    };
    if (!load(source) || project->title != "Lantern Walk" || project->pitchedNotes[0].size() != 23) return false;
    const auto expected = project->pitchedNotes[0];
    auto sameMelody = [&] {
        if (project->sourceTrackCount() != 1 || project->pitchedNotes[0].size() != expected.size()) return false;
        for (size_t i=0; i<expected.size(); ++i) {
            const auto& n = project->pitchedNotes[0][i];
            if (n.pitch != expected[i].pitch || n.startTick != expected[i].startTick || n.endTick != expected[i].endTick) return false;
        }
        return true;
    };
    auto song = notationScore(*project);
    juce::MemoryOutputStream chords;
    if (song.key != "C major" || song.chords.size() != 8 || song.tempos.front().text.getDoubleValue() != 96 ||
        writeChordPro(chords,song).failed() || !chords.toString().contains("[Am]") || !chords.toString().contains("[G7]") ||
        !save(xml,[&](auto& out){return writeSongXml(out,song);}) || !load(xml) || !sameMelody() ||
        notationScore(*project).key != "C major" || notationScore(*project).chords.size() != 8 || saveMediaImport(*project,scoreFile).failed()) return false;
    juce::FileInputStream score(scoreFile);
    if (readOstfPreview(score,project).failed() || !sameMelody()) return false;
    const auto packed = dir.getChildFile("nested.mxl");
    juce::ZipFile::Builder zip;
    const juce::String manifest = "<container><rootfiles><rootfile full-path=\"scores/main.xml\"/></rootfiles></container>";
    zip.addEntry(std::make_unique<juce::MemoryInputStream>(manifest.toRawUTF8(),manifest.getNumBytesAsUTF8(),true),9,"META-INF/container.xml", {});
    zip.addEntry(source.createInputStream(),9,"scores/main.xml",{});
    if (!save(packed,[&](auto& out){return zip.writeToStream(out,nullptr) ? juce::Result::ok() : juce::Result::fail("zip");}) ||
        !load(packed) || !sameMelody()) return false;
    // Exercise a tied voice and a simultaneous second voice with text on the first note.
    const auto voices = dir.getChildFile("voices.musicxml");
    const juce::String piece = R"(<score-partwise><part-list><score-part id="P1"><part-name>Voices</part-name></score-part></part-list><part id="P1"><measure number="1"><attributes><divisions>1</divisions></attributes><note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><tie type="start"/><voice>1</voice><lyric><text>Sing</text></lyric></note><backup><duration>4</duration></backup><note><pitch><step>G</step><octave>4</octave></pitch><duration>4</duration><voice>2</voice></note></measure><measure number="2"><note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><tie type="stop"/><voice>1</voice></note></measure></part></score-partwise>)";
    if (!voices.replaceWithText(piece) || !load(voices) || project->pitchedNotes[0].size() != 2) return false;
    auto poly = notationScore(*project);
    if (poly.lyrics.size() != 1 || poly.lyrics[0].text != "Sing" ||
        !std::any_of(poly.parts[0].notes.begin(),poly.parts[0].notes.end(),[&](const auto& n){return n.pitch==60 && n.endTick==8*poly.ppq;})) return false;
    if (!voices.replaceWithText("<score-partwise/>") || analyseScoreImport(voices,project,info).wasOk()) return false;
    const auto realPdf = juce::SystemStats::getEnvironmentVariable("OSTF_PDF_IMPORT_FIXTURE",{});
    if (realPdf.isNotEmpty() && (!load(juce::File(realPdf)) || !sameMelody() || saveMediaImport(*project,dir.getChildFile("recognised.ostf")).failed())) return false;
    return true;
}

int checkNotation(const juce::File& fixture, const juce::File& dir) {
    std::unique_ptr<OstfpPreview> p;
    juce::FileInputStream input(fixture);
    if (dir.createDirectory().failed()) return 1;
    if (fixture.hasFileExtension("ostf")) {
        if (readOstfPreview(input, p).failed()) return 110;
        const auto piano = notationScore(*p);
        if (piano.parts.size() != 1 || piano.parts.front().preferred != StaffStyle::grand) return 111;
        const auto measures = partMeasures(piano, 0, StaffStyle::grand);
        if (measures.empty() || measures.front().size() != 4 || !measures.front().front().grace) return 112;
        juce::MemoryOutputStream xml;
        if (writeSongXml(xml,piano).failed() || !xml.toString().contains("<grace slash=\"yes\"")
            || !xml.toString().contains("<tie type=\"start\"") || !xml.toString().contains("<staff>2</staff>")) return 113;
        if (!save(dir.getChildFile("written-piano.pdf"),[&](auto& out){return writeSheetPdf(out,piano,0,StaffStyle::grand,{},false);})) return 114;
        juce::FileInputStream clarinetFile(fixture.getSiblingFile("13-notation-transposition.ostf"));
        if (readOstfPreview(clarinetFile,p).failed()) return 115;
        const auto clarinet = notationScore(*p);
        juce::MemoryOutputStream clarinetXml;
        if (writeSongXml(clarinetXml,clarinet).failed() || !clarinetXml.toString().contains("<transpose>")
            || !clarinetXml.toString().contains("<step>D</step>") || !clarinetXml.toString().contains("<alter>-1</alter>")) return 116;
        juce::FileInputStream drumFile(fixture.getSiblingFile("14-notation-tab-percussion.ostf"));
        if (readOstfPreview(drumFile,p).failed()) return 117;
        const auto ensemble=notationScore(*p);
        if (ensemble.parts.size()!=2 || ensemble.parts[1].preferred!=StaffStyle::percussion) return 118;
        const auto kit=partMeasures(ensemble,1,StaffStyle::percussion);
        if (kit.empty() || kit[0].size()!=2 || kit[0][0].at!=0 || kit[0][1].at!=ensemble.ppq
            || kit[0][0].percussionInstrument!="kick" || kit[0][1].percussionInstrument!="snare") return 119;
        juce::MemoryOutputStream drumXml;
        if (writeSongXml(drumXml,ensemble).failed()) return 120;
        auto xmlDoc=juce::parseXML(drumXml.toString());
        if (!xmlDoc) return 121;
        auto* kitPart=xmlDoc->getChildByAttribute("id","P2");
        auto* kitDesc=xmlDoc->getChildByName("part-list")->getChildByAttribute("id","P2");
        if (!kitPart || !kitDesc) return 122;
        auto* bar=kitPart->getChildByName("measure");
        auto* hit=bar ? bar->getChildByName("note") : nullptr;
        for (const auto& note : kit[0]) {
            if (!hit) return 123;
            const auto* pos=hit->getChildByName("unpitched");
            const auto* inst=hit->getChildByName("instrument");
            const auto* def=inst ? kitDesc->getChildByAttribute("id",inst->getStringAttribute("id")) : nullptr;
            if (!pos || !def || pos->getChildElementAllSubText("display-step",{})!=note.writtenStep
                || pos->getChildElementAllSubText("display-octave",{}).getIntValue()!=note.writtenOctave
                || def->getChildElementAllSubText("instrument-name",{})!=ensemble.parts[1].percussionNames.at(note.percussionInstrument)) return 124;
            hit=hit->getNextElementWithTagName("note");
        }
        if (!save(dir.getChildFile("written-drums.pdf"),[&](auto& out){return writeSheetPdf(out,ensemble,1,StaffStyle::percussion,{},false);})) return 125;
        juce::FileInputStream audioFile(fixture.getSiblingFile("11-audio-single-edit-render.ostfp"));
        if (readOstfpPreview(audioFile,p).failed()) return 126;
        AudioClipWaveforms waves;
        waves.load(p->audio);
        AudioClipEditor editor(waves);
        editor.clock=[&]() -> const TempoMap& {return p->clock;};
        editor.recordRuler.visibleBars=[] {return juce::Range<double>(1.,2.);};
        editor.setSize(1000,220);
        editor.setTimelineColumns(200,1000);
        const auto now=juce::Time::getCurrentTime();
        for (const auto& clip : p->audio) {
            editor.sourceClip=[&]{return &clip;};
            editor.bind(p->ppq,p->ticksPerBar,1,true,p->importedProduction);
            const juce::Point<float> point{300,80};
            const juce::MouseEvent move{juce::Desktop::getInstance().getMainMouseSource(),point,{},0,0,0,0,0,
                                        &editor,&editor,now,point,now,0,false};
            editor.mouseMove(move);
            if (!editor.getTooltip().contains(clip.regionId=="r-original" ? "440.0 Hz" : "880.0 Hz")) return 127;
            editor.mouseExit(move);
            if (editor.getTooltip().contains(" Hz")) return 128;
        }
        return 0;
    }
    if (readOstfpPreview(input, p).failed())
        return 1;
    if (!checkInterchange(fixture,dir)) return 48;
    auto s = notationScore(*p);
    {
        auto info=p->songInformation();
        info.credits.push_back({{}, {}, "composer", "Music test credit"});
        info.credits.push_back({{}, {}, "lyricist", "Lyrics test credit"});
        juce::MemoryBlock before;
        if(p->applySongInformation(info,&before).failed()) return 106;
        auto annotated=notationScore(*p);
        juce::StringArray removed;
        for (const auto& line : annotated.printCredits) if(line.contains("Lyrics test credit")) removed.add(line);
        annotated.printCredits=selectedCreditLines(annotated.printCredits,removed);
        if(removed.size()!=1 || !annotated.printCredits.joinIntoString(" ").contains("Music test credit")
           || annotated.printCredits.joinIntoString(" ").contains("Lyrics test credit") || p->restoreNoteScore(before).failed()) return 107;
        if(!save(dir.getChildFile("selected-credits.pdf"),[&](auto& out){return writeSheetPdf(out,annotated,0,StaffStyle::treble,{},true);})) return 108;
    }
    juce::MemoryOutputStream leadSheet;
    if (writeChordPro(leadSheet,s).failed() ||
        !leadSheet.toString().contains(juce::String::fromUTF8("[Am9]雨の音が")) ||
        leadSheet.toString().contains("Ah")) return 41;
    const auto ensemble = dir.getChildFile("ensemble.musicxml");
    std::unique_ptr<OstfpPreview> ensembleImport;
    juce::String ensembleInfo;
    if (!save(ensemble, [&](auto& out) { return writeSongXml(out,s); }) ||
        analyseScoreImport(ensemble, ensembleImport, ensembleInfo).failed() || !ensembleImport ||
        notationScore(*ensembleImport).lyrics.empty() ||
        ensembleImport->sourceTrackCount() != std::count_if(p->trackKinds.begin(),p->trackKinds.begin()+p->sourceTrackCount(),
            [](auto kind){return kind != TrackKind::audio;})) return 49;
    if (s.lyrics.empty() || s.sections.empty() || s.chords.empty() || s.parts.empty() ||
        sheetPages(s, true, StaffStyle::treble) < 2)
        return 2;
    {
        auto credited=s;
        credited.bars=160; credited.printCredits.clear();
        for(int i=0;i<15;++i) credited.printCredits.add("Performer " + juce::String(i+1));
        const auto starts=sheetPageStarts(credited,true,StaffStyle::treble,{});
        if(starts.size()<3 || starts[1]>=starts[2]-starts[1]) return 103;
        SheetOptions hidden;
        hidden.showCredits=false;
        auto empty=credited; empty.printCredits.clear();
        if (sheetPageStarts(credited,false,StaffStyle::treble,hidden)!=sheetPageStarts(empty,false,StaffStyle::treble,{})) return 105;
        credited.bars=s.bars;
        if (!save(dir.getChildFile("lead-credits.pdf"), [&](auto& out){return writeSheetPdf(out,credited,0,StaffStyle::treble,{},true);})) return 104;
    }
    SheetOptions split;
    split.pageBreakBars = {9, 5, 5, -3, s.bars + 1};
    const auto pages = sheetPageStarts(s, true, StaffStyle::treble, split);
    if (pages.size() < 3 || pages[0] != 0 || pages[1] != 4 || pages[2] != 8) return 93;
    if (sheetPages(s, true, StaffStyle::treble, SheetOptions{2, {}}) != 4 || s.lyrics.front().voice.isEmpty())
        return 12;
    if (!save(dir.getChildFile("lead.pdf"),
              [&](auto& out) { return writeSheetPdf(out, s, 0, StaffStyle::treble, {}, true); }))
        return 3;
    const auto piano = std::find_if(s.parts.begin(), s.parts.end(),
                                    [](const auto& part) { return part.name.containsIgnoreCase("piano"); });
    if (piano == s.parts.end())
        return 4;
    const int index = static_cast<int>(piano - s.parts.begin());
    const auto notes = partMeasures(s, index, StaffStyle::grand);
    // A keyboard part uses the same vocal source as the lead. Each lyric mode and the chord switch must alter the staff.
    const auto staffSample = [&](const SheetOptions& options) {
        juce::Image sheet(juce::Image::RGB,900,220,true,juce::SoftwareImageType{});
        juce::Graphics g(sheet);
        drawReadingGrid(g,sheet.getBounds(),s,StaffStyle::grand,notes,false,int(s.lyrics.front().at/s.barTicks),2,1,options,-1);
        juce::MemoryOutputStream out; juce::PNGImageFormat().writeImageToStream(sheet,out);
        return out.getMemoryBlock();
    };
    SheetOptions notation;
    const auto written=staffSample(notation);
    notation.lyricDisplay=LyricDisplay::pronunciation;
    const auto pronounced=staffSample(notation);
    notation.lyricDisplay=LyricDisplay::both;
    if (staffSample(notation)==written || staffSample(notation)==pronounced) return 106;
    if (!save(dir.getChildFile("piano-chords-and-lyrics.pdf"),[&](auto& out){return writeSheetPdf(out,s,index,StaffStyle::grand,notes,false,notation);})) return 107;
    notation.lyricDisplay=LyricDisplay::text; notation.showChords=false;
    if (staffSample(notation)==written) return 108;
    if (!save(dir.getChildFile("piano.musicxml"),
              [&](auto& out) { return writePartXml(out, s, index, StaffStyle::grand, notes); }))
        return 13;
    if (!save(dir.getChildFile("part.pdf"),
              [&](auto& out) { return writeSheetPdf(out, s, index, StaffStyle::grand, notes, false); }))
        return 5;
    MainComponent main;
    main.setSize(1280, 800);
    main.setVisible(true);
    if (!main.openDocument(fixture))
        return 6;
    auto viewButton = [&](const char* label) -> juce::Button* {
        for (auto* child : main.getChildren())
            if (auto* b = dynamic_cast<ViewButton*>(child); b && b->getTitle() == juce::translate(label))
                return b;
        return nullptr;
    };
    auto* score = viewButton("Score");
    auto* lead = viewButton("Lead sheet");
    auto* arrangement = viewButton("Arrangement");
    if (!score || !lead || !arrangement)
        return 6;
    score->onClick();
    auto* view = dynamic_cast<ScoreView*>(main.findChildWithID("notation-workspace"));
    if (!view || view->getParentComponent() != &main || !view->isVisible() || !score->getToggleState() ||
        lead->getToggleState())
        return 6;
    auto* content = view->getChildComponent(0);
    auto combo = [&](const juce::String& title) -> juce::ComboBox* {
        for (auto* child : content->getChildren())
            if (child->getTitle() == title)
                return dynamic_cast<juce::ComboBox*>(child);
        return nullptr;
    };
    auto* track = combo(TRANS("Score track"));
    auto* staff = combo(TRANS("Staff type"));
    if (!track || !staff || track->getNumItems() < 2)
        return 14;
    const int first = track->getSelectedId();
    staff->setSelectedId(static_cast<int>(StaffStyle::alto), juce::sendNotificationSync);
    track->setSelectedId(first == 1 ? 2 : 1, juce::sendNotificationSync);
    track->setSelectedId(first, juce::sendNotificationSync);
    if (staff->getSelectedId() != static_cast<int>(StaffStyle::alto))
        return 15;
    lead->onClick();
    auto* lyrics = combo(TRANS("Lyric voice"));
    if (staff->isVisible() || !lyrics || !lyrics->isVisible() || lyrics->getNumItems() < 2)
        return 16;
    if (!lead->getToggleState() || score->getToggleState())
        return 17;
    auto* range = dynamic_cast<juce::Label*>(content->findChildWithID("reading-bar-range"));
    auto* grid = content->findChildWithID("reading-grid");
    if (!range || !grid || grid->getWidth() < 1000)
        return 18;
    auto toggle = [&](const char* name) -> juce::ToggleButton* {
        for (auto* child : content->getChildren())
            if (auto* b = dynamic_cast<juce::ToggleButton*>(child); b && b->getButtonText() == juce::translate(name))
                return b;
        return nullptr;
    };
    auto* scale = toggle("Scales");
    auto* words = toggle("Lyrics");
    auto* follow = toggle("Follow playback");
    if (!scale || !words || !follow)
        return 19;
    auto choose = [](juce::ToggleButton& b, bool state) {
        b.setToggleState(state, juce::dontSendNotification);
        b.onClick();
    };
    if (scale->isEnabled() != !s.scales.empty())
        return 30;
    choose(*words, false);
    if (lyrics->isVisible())
        return 20;
    choose(*words, true);
    auto* print=dynamic_cast<juce::Button*>(content->findChildWithID("sheet-print"));
    auto* close=dynamic_cast<juce::Button*>(content->findChildWithID("sheet-print-back"));
    auto* breaks=dynamic_cast<juce::TextEditor*>(content->findChildWithID("sheet-page-breaks"));
    if(!print || !close || !breaks) return 94;
    print->onClick();
    if(!breaks->isVisible() || !range->getText().contains(TRANS("Page"))) return 94;
    breaks->setText("5, 9",false); breaks->onTextChange();
    auto previewImage=grid->createComponentSnapshot(grid->getLocalBounds());
    if(previewImage.isNull() || range->getText()!=TRANS("Page")+" 1 / "+juce::String(pages.size())) return 95;
    breaks->setText("9999",false); breaks->onTextChange(); if(print->isEnabled()) return 96;
    breaks->setText("5, 9",false); breaks->onTextChange(); if(!print->isEnabled()) return 97;
    split.marginMm=20;
    if(!save(dir.getChildFile("lead-with-breaks.pdf"),[&](auto& out){return writeSheetPdf(out,s,0,StaffStyle::treble,{},true,split);})) return 98;
    auto* sheetPaper = dynamic_cast<juce::ComboBox*>(print->getParentComponent()->findChildWithID("sheet-paper-size"));
    auto* orientation = dynamic_cast<juce::ComboBox*>(print->getParentComponent()->findChildWithID("sheet-orientation"));
    if (!sheetPaper || !orientation || !sheetPaper->isVisible()) return 99;
    sheetPaper->setSelectedId(2, juce::sendNotificationSync);
    orientation->setSelectedId(2, juce::sendNotificationSync);
    split.usLetter = split.landscape = true;
    const auto landscape = dir.getChildFile("lead-letter-landscape.pdf");
    if (barsPerPage(true, StaffStyle::treble, split) >= barsPerPage(true, StaffStyle::treble, {})
        || !save(landscape, [&](auto& out){return writeSheetPdf(out,s,0,StaffStyle::treble,{},true,split);})) return 99;
    juce::MemoryBlock pdfContent;
    if (!landscape.loadFileAsData(pdfContent)) return 99;
    const std::string pdfBytes(static_cast<const char*>(pdfContent.getData()), pdfContent.getSize());
    if (pdfBytes.find("/MediaBox [0 0 792 612]") == std::string::npos) return 99;
    close->onClick();
    const auto opening = range->getText();
    PreviewTransport playback;
    juce::AudioBuffer<float> block(2, 1024);
    if (playback.preparePlayback(*p, {}, {}, 8000, block.getNumSamples()).failed())
        return 27;
    while (playback.isRunning() && playback.positionTick() < 40 * s.barTicks + s.ppq) {
        playback.renderBlock(block.getArrayOfWritePointers(), 2, block.getNumSamples());
        main.displayTransportPosition(playback.positionTick());
    }
    auto* position = dynamic_cast<juce::Label*>(content->findChildWithID("reading-position"));
    if (!position || !position->getText().contains("41.2"))
        return 28;
    playback.stop();
    const auto after = range->getText();
    if (after == opening)
        return 21;
    choose(*follow, false);
    main.displayTransportPosition(0);
    if (range->getText() != after)
        return 22;
    choose(*follow, true);
    if (range->getText() != opening)
        return 23;
    auto snapshot = [&](const char* name) {
        auto image = main.createComponentSnapshot(main.getLocalBounds());
        return save(dir.getChildFile(name), [&](auto& out) {
            return juce::PNGImageFormat().writeImageToStream(image, out) ? juce::Result::ok()
                                                                         : juce::Result::fail("PNG");
        });
    };
    main.displayTransportPosition(s.barTicks * 4 + s.ppq);
    if (!snapshot("lead-workspace.png"))
        return 24;
    score->onClick();
    track->setSelectedId(index + 1, juce::sendNotificationSync);
    main.displayTransportPosition(0);
    const auto firstSystem = range->getText();
    main.displayTransportPosition(40 * s.barTicks + s.ppq);
    if (range->getText() == firstSystem)
        return 29;
    main.displayTransportPosition(4 * s.barTicks + s.ppq);
    if (!snapshot("score-workspace.png"))
        return 25;
    arrangement->onClick();
    if (view->isVisible() || !arrangement->getToggleState() || score->getToggleState())
        return 26;
    if (!main.openDocument(fixture.getSiblingFile("neon-platforms.ostfp")))
        return 31;
    lead->onClick();
    view = dynamic_cast<ScoreView*>(main.findChildWithID("notation-workspace"));
    if (!view)
        return 32;
    content = view->getChildComponent(0);
    scale = toggle("Scales");
    grid = content->findChildWithID("reading-grid");
    if (!scale || !scale->isEnabled() || !grid)
        return 33;
    auto noScale = grid->createComponentSnapshot(grid->getLocalBounds());
    choose(*scale, true);
    auto withScale = grid->createComponentSnapshot(grid->getLocalBounds());
    int changed = 0;
    for (int y = 0; y < withScale.getHeight(); y += 2)
        for (int x = 0; x < withScale.getWidth(); x += 2)
            if (withScale.getPixelAt(x, y) != noScale.getPixelAt(x, y))
                ++changed;
    if (changed < 30)
        return 34;
    main.displayTransportPosition(s.barTicks + s.ppq);
    if (!snapshot("scale-workspace.png"))
        return 35;
    // A chord across a barline requires separate string assignments, two voices and ties.
    s.bars = 2;
    s.parts[index].notes = {{s.barTicks - 120, s.barTicks + 120, 64, 90}, {s.barTicks - 120, s.barTicks + 240, 60, 80}};
    for (int i = 1; i <= 7; ++i) {
        auto style = static_cast<StaffStyle>(i);
        auto bars = partMeasures(s, index, style);
        if (bars.size() != 2 || bars[0].size() != 2 || !bars[0][0].tiedTo || !bars[1][0].tiedFrom ||
            bars[1][0].at != 0 || bars[1][1].length != 240 || bars[0][0].voice == bars[0][1].voice)
            return 7;
        if (style == StaffStyle::guitarTab &&
            (bars[0][0].string != 1 || bars[0][0].fret != 0 || bars[0][1].string == 1))
            return 8;
        auto file = dir.getChildFile("staff-" + juce::String(i) + ".musicxml");
        if (!save(file, [&](auto& out) { return writePartXml(out, s, index, style, bars); }))
            return 9;
        auto xml = juce::parseXML(file);
        if (!xml || !xml->hasTagName("score-partwise"))
            return 10;
    }
    const auto shortPdf = dir.getChildFile("ai-score.pdf");
    auto shortBars = partMeasures(s, index, StaffStyle::treble);
    if (!save(shortPdf, [&](auto& out) { return writeSheetPdf(out, s, index, StaffStyle::treble, shortBars, false); })) return 36;
    if (!std::async(std::launch::async, [shortPdf] {
        std::unique_ptr<OstfpPreview> parsed;
        juce::String info;
        return analyseScoreImport(shortPdf, parsed, info, std::make_shared<ImportTestProvider>()).wasOk()
            && parsed && parsed->sourceTrackCount() == 1 && parsed->pitchedNotes[0][0].pitch == 60;
    }).get()) return 37;
    return checkImport(dir, main) ? 0 : 11;
}
