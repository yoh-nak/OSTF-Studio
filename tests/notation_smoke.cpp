#include "ui/MainComponent.h"
#include "ui/MediaImport.h"
#include "ui/GlobalTimelineEdit.h"
#include <juce_audio_basics/juce_audio_basics.h>

using namespace ostfstudio;
namespace {
bool save(const juce::File& dest, const std::function<juce::Result(juce::OutputStream&)>& write) {
    juce::MemoryOutputStream out;
    return write(out).wasOk() && dest.replaceWithData(out.getData(), out.getDataSize());
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
    if (placeImportTrack(*analysed, -1, *separated, {}, 0, before).failed() ||
        juce::JSON::parse(separated->sourceScore.toString())["harmony"]["chords"].size() != 2)
        return false;
    main.openImportedSong(std::move(analysed));
    auto* label = dynamic_cast<juce::Label*>(main.findChildWithID("current-document"));
    if (!label || !label->getText().contains(TRANS("Unsaved project")) || !label->getText().contains("triads") ||
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
    const auto& imported = notesTarget->addedClips.back().notes;
    if (imported.size() != 2) return false;
    for (std::size_t i = 0; i < imported.size(); ++i) {
        const double originalTime = project->clock.secondsAt(project->pitchedNotes[1][i].startTick);
        const double placedTime = notesTarget->clock.secondsAt(imported[i].startTick) - notesTarget->clock.secondsAt(120);
        // MIDI ticks are integral. Preserve the transcription's initial offset within half a destination tick.
        if (std::abs(placedTime - originalTime) > 0.500001 / notesTarget->ppq) return false;
    }
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
int checkNotation(const juce::File& fixture, const juce::File& dir) {
    std::unique_ptr<OstfpPreview> p;
    juce::FileInputStream input(fixture);
    if (dir.createDirectory().failed() || readOstfpPreview(input, p).failed())
        return 1;
    auto s = notationScore(*p);
    if (s.lyrics.empty() || s.sections.empty() || s.chords.empty() || s.parts.empty() ||
        sheetPages(s, true, StaffStyle::treble) < 2)
        return 2;
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
    return checkImport(dir, main) ? 0 : 11;
}
