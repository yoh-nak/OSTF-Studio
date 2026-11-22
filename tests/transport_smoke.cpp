#include "ui/MainComponent.h"
#include "ui/SongInformationPanel.h"

using namespace ostfstudio;
namespace {
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
    std::unique_ptr<OstfpPreview> song;
    juce::FileInputStream input(fixture);
    if (dir.createDirectory().failed() || readOstfpPreview(input, song).failed()) return 1;
    auto score = juce::JSON::parse(song->sourceScore.toString());
    auto* timeline = score["timeline"].getDynamicObject();
    auto meter = score["timeline"]["meterChanges"][0].getDynamicObject();
    meter->setProperty("numerator", 8);
    meter->setProperty("denominator", 8);
    timeline->setProperty("tempoChanges", juce::JSON::parse(R"([{"at":0,"bpm":120},{"at":960,"bpm":60}])"));
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
    if (song->clock.bpmAt(959) != 120 || song->clock.bpmAt(960) != 60
        || std::abs(song->clock.secondsAt(1920) - 1.5) > 1e-8 || std::abs(song->clock.tickAt(1.5) - 1920) > 1e-8
        || song->contextAt(TimelineLane::key, 960) != "G major"
        || song->contextAt(TimelineLane::chord, 959).isEmpty() || song->contextAt(TimelineLane::chord, 960).isNotEmpty()
        || song->contextAt(TimelineLane::scale, 960) != "G Lydian" || song->contextAt(TimelineLane::scale, 1440) != "A natural minor") return 3;
    PreviewTransport engine;
    juce::AudioBuffer<float> buffer(2, 400);
    if (engine.preparePlayback(*song, {}, {}, 8000, 400).failed()) return 4;
    for (int i = 0; i < 30; ++i) engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 1920) return 5;
    engine.stop();
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
    auto* document = dynamic_cast<juce::Label*>(main.findChildWithID("current-document"));
    if (!document || !document->getText().contains(file.getFileName()) || !document->getText().contains(info.title)) return 13;
    main.displayTransportPosition(960);
    auto* deck = main.findChildWithID("transport");
    const auto reads = [&](const char* id, const juce::String& expected) {
        auto* value = deck ? dynamic_cast<juce::Label*>(deck->findChildWithID(id)) : nullptr;
        return value && value->getText() == expected;
    };
    if (!reads("bar-beat-tick", "001.03.000") || !reads("timecode", "00:00:00:15") || !reads("meter", "8/8")
        || !reads("key", "G major") || !reads("scale", "G Lydian") || !reads("tempo", "60")
        || !reads("chord", juce::String::charToString(0x2014))) return 18;
    const auto first = pianoCursorX(main);
    main.displayTransportPosition(5760);
    if (first < 0 || pianoCursorX(main) <= first || !savePng(dir.getChildFile("transport-piano.png"), main)) return 14;
    juce::Slider* pianoZoom = nullptr;
    juce::Slider* arrangementZoom = nullptr;
    for (auto* child : main.getChildren()) {
        auto* slider = dynamic_cast<juce::Slider*>(child);
        if (slider && slider->getTitle() == TRANS("Piano roll horizontal zoom")) pianoZoom = slider;
        if (slider && slider->getTitle() == TRANS("Arrangement horizontal zoom")) arrangementZoom = slider;
    }
    auto* pianoScroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("piano-horizontal-scroll"));
    auto* arrangementScroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-horizontal-scroll"));
    if (!pianoZoom || !arrangementZoom || !pianoScroll || !arrangementScroll) return 19;
    const auto full = pianoScroll->getCurrentRangeSize();
    pianoZoom->setValue(0.63, juce::sendNotificationSync);
    const auto span = pianoScroll->getCurrentRangeSize();
    if (std::abs(span - std::pow(full, 0.37)) > 0.001 || arrangementScroll->getCurrentRangeSize() != full) return 20;
    arrangementZoom->setValue(1, juce::sendNotificationSync);
    arrangementScroll->setCurrentRangeStart(full, juce::sendNotificationSync);
    if (arrangementScroll->getCurrentRangeSize() != 1 || pianoScroll->getCurrentRangeSize() != span) return 21;
    arrangementZoom->setValue(0, juce::sendNotificationSync);
    if (arrangementScroll->getCurrentRangeStart() != 1 || arrangementScroll->getCurrentRangeSize() != full
        || arrangementScroll->isEnabled()) return 22;
    main.displayTransportPosition(10 * song->ticksPerBar + 960);
    if (pianoCursorX(main) < 0) return 15;
    main.setSize(680, 550);
    main.displayTransportPosition(960);
    if (!savePng(dir.getChildFile("transport-compact.png"), main) || pianoCursorX(main) < 0) return 16;
    bool applied = false;
    SongInformationPanel editor(info, [&](const SongInformation& edited) {
        applied = edited.title == info.title && edited.credits.size() == 3 && edited.credits.back().role == "composer";
        return juce::Result::ok();
    });
    if (!savePng(dir.getChildFile("song-information.png"), editor)) return 17;
    for (const auto* caption : {"Add credit", "Apply"})
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText() == juce::translate(caption))
                button->onClick();
    return applied ? 0 : 19;
}
