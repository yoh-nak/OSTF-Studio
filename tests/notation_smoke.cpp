#include "ui/MainComponent.h"
#include "ui/MediaImport.h"
#include <juce_audio_basics/juce_audio_basics.h>

using namespace ostfstudio;
namespace {
bool save(const juce::File& dest, const std::function<juce::Result(juce::OutputStream&)>& write) {
    juce::MemoryOutputStream out;
    return write(out).wasOk() && dest.replaceWithData(out.getData(), out.getDataSize());
}
bool checkImport(const juce::File& dir) {
    auto file = dir.getChildFile("phrase.mid");
    juce::MidiFile song;
    song.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence seq;
    for (int channel : {1, 10}) {
        seq.addEvent(juce::MidiMessage::noteOn(channel, channel == 1 ? 64 : 38, static_cast<juce::uint8>(91)), 480);
        seq.addEvent(juce::MidiMessage::noteOff(channel, channel == 1 ? 64 : 38), 1200);
    }
    song.addTrack(seq);
    juce::MemoryOutputStream midi;
    if (!song.writeTo(midi, 0) || !file.replaceWithData(midi.getData(), midi.getDataSize()))
        return false;
    std::unique_ptr<OstfpPreview> project;
    juce::String info;
    if (prepareMediaImport(file, project, info).failed() || project->sourceTrackCount() != 2 ||
        project->pitchedNotes[0].size() != 1 || project->drumNotes[1].size() != 1 ||
        project->pitchedNotes[0][0].startTick != 480 || project->pitchedNotes[0][0].endTick != 1200 ||
        project->sourceMidiChannels[1] != 10 || saveMediaImport(*project, dir.getChildFile("phrase.ostf")).failed())
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
    auto melody = dir.getChildFile("melody.wav");
    if (!melody.replaceWithData(tones.getData(), tones.getDataSize()) ||
        prepareMediaImport(melody, project, info, true).failed() || project->pitchedNotes.size() != 2 ||
        project->pitchedNotes[1].size() != 2 || project->pitchedNotes[1][0].pitch != 69 ||
        project->pitchedNotes[1][1].pitch != 72 || std::abs(project->pitchedNotes[1][1].startTick - 768) > 60)
        return false;
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
    return checkImport(dir) ? 0 : 11;
}
