#include "ui/MainComponent.h"
#include "ui/SongInformationPanel.h"
#include <iostream>

using namespace ostfstudio;
namespace {
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
        if (!waveforms.available(0) || std::abs(waveforms.endTick(region, clock) - 2400) > .001) {
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
    region.embedded.reset();
    waveforms.load({region});
    return !waveforms.available(0);
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
    std::unique_ptr<OstfpPreview> song;
    juce::FileInputStream input(fixture);
    if (dir.createDirectory().failed() || readOstfpPreview(input, song).failed()) return 1;
    if (!checkWaveform(dir)) return 42;
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
    if (song->clock.bpmAt(959) != 120 || song->clock.bpmAt(960) != 60
        || song->clock.nextChangeAfter(959) != 960 || song->clock.nextChangeAfter(960)
        || std::abs(song->clock.secondsAt(1920) - 1.5) > 1e-8 || std::abs(song->clock.tickAt(1.5) - 1920) > 1e-8
        || song->contextAt(TimelineLane::key, 960) != "G major"
        || song->contextAt(TimelineLane::chord, 959).isEmpty() || song->contextAt(TimelineLane::chord, 960).isNotEmpty()
        || song->contextAt(TimelineLane::scale, 960) != "G Lydian" || song->contextAt(TimelineLane::scale, 1440) != "A natural minor") return 3;
    PreviewTransport engine;
    juce::AudioBuffer<float> buffer(2, 400);
    if (engine.preparePlayback(*song, {}, {}, 8000, 400, 2, .5).failed()) return 43;
    for (int i = 0; i < 60; ++i) engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 1920 || song->clock.bpmAt(960) != 60) return 44;
    if (engine.preparePlayback(*song, {}, {}, 8000, 400).failed()) return 4;
    for (int i = 0; i < 30; ++i) engine.renderBlock(buffer.getArrayOfWritePointers(), 2, 400);
    if (engine.positionTick() != 1920) return 5;
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
    const auto full = arrangementScroll->getCurrentRangeSize();
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
    const auto button = [&](const char* id) { return dynamic_cast<juce::Button*>(deck->findChildWithID(id)); };
    auto* home = button("return-to-start");
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
