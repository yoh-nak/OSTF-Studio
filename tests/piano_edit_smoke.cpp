#include "ui/MainComponent.h"
#include "ui/SongMidi.h"
#include "core/ScoreSchema.h"

using namespace ostfstudio;

namespace {
juce::MouseEvent pointer(juce::Component& grid, juce::Point<float> p, juce::Point<float> origin, int modifiers = 0) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | modifiers),
            1, 0, 0, 0, 0, &grid, &grid, now, origin, now, 1, p != origin};
}
int modelChecks(const juce::File& fixture) {
    std::unique_ptr<OstfpPreview> song;
    juce::FileInputStream input(fixture);
    if (readOstfpPreview(input, song).failed()) return 1;
    auto first = song->pitchedNotes.at(0).at(0);
    auto original = song->sourceScore;
    if (first.id.isEmpty() || !song->insertGeneratorPattern(0, 1)) return 2;
    auto generated = song->addedClips.front().notes.front();
    juce::MemoryBlock before;
    juce::StringArray ids;
    first.pitch = 78; first.velocity = 117;
    generated.endTick -= 1;
    if (song->editNotes(0, {{NoteEdit::change, first}, {NoteEdit::change, generated}}, before, ids).failed() || ids.size() != 2) return 3;
    if (before == original || song->addedClips.size() || song->pitchedNotes[0].size() < 5) return 4;
    const auto edited = song->sourceScore;
    auto bad = first; bad.endTick = bad.startTick;
    if (song->editNotes(0, {{NoteEdit::erase, generated}, {NoteEdit::change, bad}}, before, ids).wasOk()
        || edited != song->sourceScore) return 5;
    generated.startTick += song->ppq; generated.endTick += song->ppq;
    if (song->editNotes(0, {{NoteEdit::duplicate, generated}}, before, ids).failed() || ids[0] == generated.id) return 6;
    generated.id = ids[0];
    if (song->editNotes(0, {{NoteEdit::erase, generated}}, before, ids).failed()) return 7;
    auto drumTrack = std::find(song->trackKinds.begin(), song->trackKinds.end(), TrackKind::drum) - song->trackKinds.begin();
    PitchedNote hit{0, 60, 47, 105, true, {}};
    if (drumTrack >= song->sourceTrackCount() || song->editNotes(drumTrack, {{NoteEdit::insert, hit}}, before, ids).failed()) return 8;
    hit.id = ids[0]; hit.pitch = 48;
    if (song->editNotes(drumTrack, {{NoteEdit::change, hit}}, before, ids).failed()) return 9;
    // Outside General MIDI's 35..81 percussion range the event uses a custom MIDI mapping.
    hit.pitch = 87;
    if (song->editNotes(drumTrack, {{NoteEdit::change, hit}}, before, ids).failed()) return 16;
    auto slotId = song->slotsByTrack[drumTrack][0].id;
    if (song->editDrumPad(drumTrack, 0, {}, before, 2).failed() || song->slotsByTrack[drumTrack][2].id != slotId)
        return 92;
    for (auto index : {size_t(0), size_t(drumTrack)}) {
        const int pitch = index == 0 ? 62 : 87;
        if (song->addRecordedClip(index, {90, 230}, {{101, 211, pitch, 83}}, before).failed()) return 93;
        const auto data = juce::JSON::parse(song->sourceScore.toString());
        const auto clips = data["tracks"][int(index)]["clips"];
        const auto take = clips[clips.size() - 1];
        if (int(take["start"]) != 90 || int(take["duration"]) != 140 || int(take["notes"][0]["start"]) != 11
            || !take["notes"][0].hasProperty(index == 0 ? "pitch" : "soundSlotId")) return 94;
    }
    const MidiControl bend{"pitchBend"}, cc{"controlChange", 11}, pressure{"channelPressure"}, poly{"polyPressure", 60};
    const std::array controlLanes {bend, cc, pressure, poly, MidiControl{"programChange"}, MidiControl{"controlChange",0},
                                   MidiControl{"controlChange",32}, MidiControl{"controlChange",119}};
    for (const auto& key : controlLanes) {
        if (song->editController(0, key, {}, 70, key.maximum(), before).failed()) return 61;
        const auto& curves = song->performanceCurves.at(0);
        const auto lane = std::find_if(curves.begin(), curves.end(), [&](const auto& c){return c.label == key.label();});
        if (lane == curves.end()) return 62;
        const auto p = std::find_if(lane->points.begin(), lane->points.end(), [](const auto& p){return p.tick == 70 && p.value == 1;});
        if (p == lane->points.end()) return 63;
        const auto id = p->id;
        if (song->editController(0,key,id,85,73,before).failed()) return 64;
        if (song->editController(0,key,id,85,{},before).failed() || song->restoreNoteScore(before).failed()) return 65;
    }
    const auto owner = song->trackIds[0];
    song->panAutomation[owner] = {"test-pan", true, {{30,-1},{90,1}}};
    SessionMix mix = song->savedMix.value_or(SessionMix{});
    mix.tracks.resize(song->trackLanes.size());
    mix.recordCycle = {32, 295};
    mix.cycleEnabled = true;
    juce::MemoryOutputStream plain, packed, midi;
    if (writeOstfPreview(*song, mix, plain).failed() || writeOstfpPreview(*song, mix, packed).failed()
        || writeSongMidi(*song, SmfFormat::multitrack, song->sourceMidiChannels, midi).failed()) return 10;
    validateScoreSchema("5.0.0", {static_cast<const char*>(plain.getData()), plain.getDataSize()});
    juce::MidiFile events;
    juce::MemoryInputStream smf(midi.getData(), midi.getDataSize(), false);
    if (!events.readFrom(smf)) return 11;
    bool exported = false, exportedDrum = false;
    for (int i = 0; i < events.getNumTracks(); ++i) for (const auto* ev : *events.getTrack(i))
        if (ev->message.isNoteOn()) {
            exported |= ev->message.getNoteNumber() == 78 && ev->message.getVelocity() == 117;
            exportedDrum |= ev->message.getNoteNumber() == 87 && ev->message.getVelocity() == 105;
        }
    if (!exported || !exportedDrum) return 12;
    for (bool package : {false, true}) {
        auto& stream = package ? packed : plain;
        juce::MemoryInputStream reopen(stream.getData(), stream.getDataSize(), false);
        if ((package ? readOstfpPreview(reopen, song) : readOstfPreview(reopen, song)).failed()) return 13;
        if (!song->savedMix || !song->savedMix->cycleEnabled || song->savedMix->recordCycle != mix.recordCycle) return 95;
        if (!song->panAutomation.contains(owner) || std::abs(song->panAutomation.at(owner).valueAt(60,.5)) > .00001) return 66;
        for (const auto& key : controlLanes) {
            const auto& curves = song->performanceCurves.at(0);
            const auto lane = std::find_if(curves.begin(),curves.end(),[&](const auto& c){return c.label == key.label();});
            if (lane == curves.end() || std::none_of(lane->points.begin(),lane->points.end(),[&](const auto& p){
                return p.tick == 85 && juce::roundToInt(p.value * key.maximum()) == 73;
            })) return 67;
        }
        const auto& notes = song->pitchedNotes[0];
        if (std::none_of(notes.begin(), notes.end(), [&](const auto& n) {return n.id == first.id && n.pitch == 78 && n.velocity == 117;})) return 14;
        const auto& drums = song->drumNotes[drumTrack];
        if (std::none_of(drums.begin(), drums.end(), [&](const auto& n) {return n.id == hit.id && n.pitch == hit.pitch;})) return 15;
    }
    return 0;
}
}

int checkPianoEdits(const juce::File& fixture) {
    if (const auto failed = modelChecks(fixture)) return failed;
    {
        OstfpPreview score;
        score.ppq = 960; score.ticksPerBar = 3840; score.bars = {1,4};
        score.slotsByTrack = {{{"rim","Rim",38},{"snare","Snare",38}}};
        score.slotHits = {{{0,120,38,90,true,"a",false,{},"rim"}}};
        DrumSteps steps;
        steps.setSize(600,180);
        steps.bind(&score,0,500,240);
        std::vector<NoteEdit> edits;
        steps.commit = [&](auto e){edits = e;};
        for (auto* child : steps.getChildren()) if (auto* cb = dynamic_cast<juce::ComboBox*>(child); cb && cb->getNumItems()==4)
            cb->setSelectedId(3,juce::sendNotificationSync);
        auto touch = [&](float y) {
            juce::Point<float> xy{125,y};
            steps.mouseDown(pointer(steps,xy,xy,juce::ModifierKeys::shiftModifier));
        };
        steps.setLoopBars(2,2);
        steps.bind(&score,0,9000,240);
        if (steps.loopRange() != juce::Range<juce::int64>(3840,11520) || steps.range().getStart() != 0) return 131;
        steps.setLoopBars(3,8);
        if (steps.loopRange() != juce::Range<juce::int64>(7680,11520)) return 132;
        steps.bind(nullptr,0,0,240);
        steps.bind(&score,0,0,240);
        if (steps.loopRange() != steps.range()) return 133;
        touch(72);
        if (edits.size()!=1 || edits[0].action!=NoteEdit::erase || edits[0].note.id!="a") return 68;
        touch(48);
        if (edits.size()!=3 || edits[0].note.slotId!="snare" || edits[0].note.velocity!=120
            || edits[1].note.startTick!=80 || edits[2].note.endTick!=240) return 69;
    }
    {
        PianoNoteGrid editor;
        editor.setSize(500, 190);
        editor.editable = true;
        editor.grid = 50;
        editor.endTick = 500;
        editor.tickRange = [] { return juce::Range<double>(0, 500); };
        editor.notes = {{63, 119, 60, 80, true, "a"}, {63, 144, 64, 90, true, "b"},
                        {232, 280, 65, 96, true, "c"}, {475, 500, 66, 96, true, "d"}};
        editor.selection = {"a", "b", "c", "d"};
        std::vector<NoteEdit> results;
        editor.commit = [&](const auto& edits) { results = edits; };
        editor.quantizeSelection(50);
        if (results.size() != 3 || results[0].note.startTick != 56 || results[0].note.endTick != 112
            || results[1].note.startTick != 56 || results[2].note.startTick != 241) return 48;
        editor.shapeVelocity(40, 120);
        if (results.size() != 4 || results[0].note.velocity != 40 || results[1].note.velocity != 40
            || results[2].note.velocity != 73 || results[3].note.velocity != 120) return 49;
        editor.selection = {"a"};
        editor.absoluteSnap = true;
        auto resize = [&](bool bypass) {
            auto start = editor.box(editor.notes[0]).getCentre().withX(64.f);
            auto end = start.translated(21.f, 0);
            editor.mouseDown(pointer(editor, start, start));
            editor.mouseDrag(pointer(editor, end, start, bypass ? juce::ModifierKeys::shiftModifier : 0));
            editor.mouseUp(pointer(editor, end, start));
        };
        resize(false);
        if (results.size() != 1 || results.front().note.startTick != 100 || results.front().note.endTick != 119) return 50;
        resize(true);
        if (results.size() != 1 || results.front().note.startTick != 84 || results.front().note.endTick != 119) return 51;
    }
    {
        PianoNoteGrid editor;
        editor.setSize(500, 160);
        editor.editable = true;
        editor.endTick = 500;
        editor.quarter = 100;
        editor.grid = 20;
        editor.tickRange = [] { return juce::Range<double>(0, 500); };
        editor.notes = {{0, 101, 67, 80, true, "high"}, {0, 100, 60, 60, true, "low"},
                        {0, 98, 64, 85, true, "mid"}, {0, 99, 64, 88, true, "unison"},
                        {40, 42, 50, 65, true, "short"}, {40, 90, 55, 65, true, "second"},
                        {0, 120, 71, 99, true, "other"}};
        editor.selection = {"low", "high", "mid", "unison", "short", "second"};
        std::vector<NoteEdit> batch;
        editor.commit = [&](const auto& edits) { batch = edits; };
        editor.strumSelection(PianoNoteGrid::Stroke::alternate, 100);
        auto onset = [&](juce::String id) {
            for (const auto& edit : batch) if (edit.note.id == id) return int(edit.note.startTick);
            return -1;
        };
        if (batch.size() != 4 || onset("mid") != 20 || onset("unison") != 20 || onset("high") != 39
            || onset("short") != 41 || onset("other") != -1) return 70;
        editor.strumSelection(PianoNoteGrid::Stroke::highFirst, 12);
        if (onset("high") != -1 || onset("mid") != 6 || onset("low") != 12) return 71;
        editor.percussion = true;
        editor.kitPitches = {36, 38, 42, 46, 50, 55, 60, 64, 67, editor.high};
        std::sort(editor.kitPitches.begin(), editor.kitPitches.end());
        editor.kitPitches.erase(std::unique(editor.kitPitches.begin(), editor.kitPitches.end()),
                                editor.kitPitches.end());
        if (editor.rowPitch(editor.rowCount() - 1) != 36 || editor.pitchRow(37) != -1 ||
            editor.rowPitch(editor.pitchRow(46)) != 46)
            return 90;
        batch.clear();
        editor.strumSelection(PianoNoteGrid::Stroke::lowFirst, 20);
        if (!batch.empty()) return 72;
        editor.selection = {"high", "short"};
        editor.splitIntoTriplets();
        if (batch.size() != 3 || batch[0].note.startTick != 34 || batch[0].note.endTick != 67
            || batch[1].note.endTick != 101 || batch[2].note.startTick != 0 || batch[2].note.endTick != 34) return 73;

        const juce::Point<float> down {369, 12}, end {462, 12};
        editor.pencil = true;
        batch.clear();
        editor.mouseDown(pointer(editor, down, down));
        editor.mouseDrag(pointer(editor, end, down));
        if (!batch.empty()) return 74;
        editor.mouseUp(pointer(editor, end, down));
        if (batch.size() != 1 || batch[0].action != NoteEdit::insert || batch[0].note.startTick != 360 || batch[0].note.endTick != 460) return 75;
        editor.mouseDown(pointer(editor, down, down));
        editor.mouseUp(pointer(editor, down, down));
        if (batch.size() != 1 || batch[0].note.endTick != 380) return 76;
        batch.clear();
        editor.mouseDown(pointer(editor, down, down)); editor.cancel(); editor.mouseUp(pointer(editor, down, down));
        if (!batch.empty()) return 77;

        // PPQ 100 cannot represent an eighth-note triplet as a fixed integer tick length.
        editor.grid = 100.0 / 3;
        editor.notes = {{67, 90, editor.high, 96, true, "existing"}};
        const auto alt = juce::ModifierKeys::altModifier;
        const juce::Point<float> left {1, 1}, right {196, 1};
        auto paint = [&](auto from, auto to) {
            editor.mouseDown(pointer(editor, from, from, alt));
            editor.mouseDrag(pointer(editor, to, from, alt));
            editor.mouseUp(pointer(editor, to, from, alt));
        };
        for (bool reverse : {false, true}) {
            paint(reverse ? right : left, reverse ? left : right);
            if (batch.size() != 5 || batch[0].note.startTick != 0 || batch[1].note.startTick != 33
                || batch[1].note.endTick != 67 || batch[2].note.startTick != 100 || batch[4].note.endTick != 200) return 81;
        }
        const juce::Point<float> tail {499, 1};
        paint(tail, tail);
        if (batch.size() != 1 || batch[0].note.startTick != 467 || batch[0].note.endTick != 500) return 82;
        editor.pencil = false;
        editor.notes = {{33, 50, editor.high, 96, true, "fractional"}};
        editor.selection = {"fractional"};
        const auto centre = editor.box(editor.notes.front()).getCentre();
        const auto next = centre.translated(33.f, 0);
        editor.mouseDown(pointer(editor, centre, centre)); editor.mouseDrag(pointer(editor, next, centre));
        editor.mouseUp(pointer(editor, next, centre));
        if (batch.size() != 1 || batch[0].note.startTick != 67) return 85;
    }
    MainComponent main;
    main.setVisible(true); main.setSize(1280, 900);
    if (!main.openDocument(fixture)) return 20;
    auto* grid = dynamic_cast<PianoNoteGrid*>(main.findChildWithID("piano-note-grid"));
    if (!grid || !grid->editable || !grid->isVisible() || grid->getWidth() < 200 || grid->getHeight() < 20) {
        std::fprintf(stderr, "Initial piano roll: editable=%d, visible=%d, size=%d x %d\n",
                     grid && grid->editable, grid && grid->isVisible(), grid ? grid->getWidth() : 0, grid ? grid->getHeight() : 0);
        return 21;
    }
    for (auto* child : main.getChildren()) if (auto* slider = dynamic_cast<juce::Slider*>(child))
        if (slider->getTitle() == TRANS("Timeline horizontal zoom")) slider->setValue(1, juce::sendNotificationSync);
    const auto initial = grid->notes.size();
    const juce::Point<float> empty {grid->getWidth() * .25f, grid->getHeight() * .45f};
    grid->mouseDoubleClick(pointer(*grid, empty, empty));
    if (grid->notes.size() != initial + 1 || grid->selection.size() != 1) return 22;
    auto selected = [&] { for (auto n : grid->notes) if (grid->selection.contains(n.id)) return n; return PitchedNote{}; };
    const auto inserted = selected();
    grid->splitIntoTriplets();
    if (grid->notes.size() != initial + 3 || grid->selection.size() != 3 || !grid->selection.contains(inserted.id)) return 78;
    std::vector<PitchedNote> repetitions;
    for (const auto& n : grid->notes) if (grid->selection.contains(n.id)) repetitions.push_back(n);
    if (repetitions.size() != 3 || repetitions.front().startTick != inserted.startTick || repetitions.back().endTick != inserted.endTick
        || repetitions[0].endTick != repetitions[1].startTick || repetitions[1].endTick != repetitions[2].startTick
        || repetitions[2].velocity != inserted.velocity || repetitions[1].pitch != inserted.pitch) return 79;
    grid->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0));
    if (grid->notes.size() != initial + 1 || std::none_of(grid->notes.begin(), grid->notes.end(),
        [&](const auto& n) { return n.id == inserted.id && n.endTick == inserted.endTick; })) return 80;
    grid->selection = juce::StringArray(inserted.id);
    if (!grid->keyPressed(juce::KeyPress('B')) || !grid->pencil) return 83;
    grid->keyPressed(juce::KeyPress('B'));
    if (grid->pencil) return 84;
    auto drag = [&](juce::Point<float> start, juce::Point<float> delta, int mods = 0) {
        grid->mouseDown(pointer(*grid, start, start, mods));
        grid->mouseDrag(pointer(*grid, start + delta, start, mods));
        grid->mouseUp(pointer(*grid, start + delta, start, mods));
    };
    const float beat = float(grid->getWidth() * grid->quarter / grid->tickRange().getLength());
    drag(grid->box(inserted).getCentre(), {beat, 0});
    if (selected().startTick != inserted.startTick + grid->quarter) return 23;
    auto box = grid->box(selected());
    drag({box.getRight() - 1, box.getCentreY()}, {beat, 0});
    if (selected().endTick - selected().startTick != inserted.endTick - inserted.startTick + grid->quarter) return 24;
    drag(grid->box(selected()).getCentre(), {beat, 0}, juce::ModifierKeys::altModifier);
    if (grid->notes.size() != initial + 2 || selected().id == inserted.id) return 25;
    drag(grid->box(selected()).getCentre(), {0, -16}, juce::ModifierKeys::commandModifier);
    if (selected().velocity != 104) return 26;
    auto* velocity = dynamic_cast<PianoVelocityLane*>(main.findChildWithID("piano-velocity-lane"));
    if (!velocity || !velocity->isVisible()) return 33;
    const juce::Point<float> handle{grid->box(selected()).getX(), velocity->getHeight() - 1.f - 104.f / 127 * (velocity->getHeight() - 3)};
    const auto bottom = handle.withY(float(velocity->getHeight() - 1));
    velocity->mouseDown(pointer(*velocity, handle, handle));
    velocity->mouseDrag(pointer(*velocity, bottom, handle));
    velocity->mouseUp(pointer(*velocity, bottom, handle));
    if (selected().velocity != 1) return 34;
    grid->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    if (grid->notes.size() != initial + 1) return 27;
    grid->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0));
    if (grid->notes.size() != initial + 2) return 28;
    grid->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    if (grid->notes.size() != initial + 1) return 29;
    // Marquee across the first displayed bar, then extend the selection with Shift-click.
    const auto one = grid->box(inserted).getCentre() + juce::Point<float>{beat, 0};
    auto from = juce::Point<float>{1.f, 1.f};
    drag(from, {float(grid->getWidth() - 2), float(grid->getHeight() - 2)});
    if (!grid->selection.contains(inserted.id)) return 30;
    grid->mouseDown(pointer(*grid, one, one, juce::ModifierKeys::shiftModifier));
    if (grid->selection.contains(inserted.id)) return 31;
    grid->keyPressed(juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0));
    auto prior = grid->notes;
    grid->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    for (const auto& old : prior) {
        if (std::none_of(grid->notes.begin(), grid->notes.end(), [&](const auto& n) { return n.id == old.id && n.pitch == old.pitch + 1; }))
            return 32;
    }
    auto* divider = main.findChildWithID("piano-divider");
    if (!divider || !divider->isVisible()) return 52;
    const int initialY = divider->getY(), initialGridTop = grid->getY();
    divider->mouseDrag(pointer(*divider, {12.f, -26.f}, {12.f, 0.f}));
    if (grid->getY() >= initialGridTop || grid->getWidth() < 200) return 53;
    divider->mouseDrag(pointer(*divider, {12.f, float(initialY - divider->getY())}, {12.f, 0.f}));
    auto* column = main.findChildWithID("piano-column-divider");
    auto* upper = main.findChildWithID("arrangement-column-divider");
    const auto exposed = [&](juce::Component* grip) {
        return grip && grip->isVisible() && !grip->getBounds().isEmpty()
            && main.getComponentAt(grip->getBounds().getCentre()) == grip;
    };
    const auto aligned = [&] { return exposed(column) && exposed(upper) &&
        column->getRight() == grid->getX() && column->getX() == upper->getX() &&
        column->getWidth() == upper->getWidth(); };
    if (!aligned()) return 58;
    const auto dragDivider = [&](juce::Component& grip, int delta, float grabX) {
        const juce::Point<float> start(grabX, float(grip.getHeight() / 2));
        grip.mouseDown(pointer(grip, start, start));
        grip.mouseDrag(pointer(grip, start.translated(float(delta), 0.f), start));
        grip.mouseUp(pointer(grip, start, start));
    };
    const int gridX = grid->getX(), shift = gridX < 700 ? 32 : -32;
    dragDivider(*column, shift, 1.f);
    if (!aligned() || grid->getX() != gridX + shift) return 59;
    dragDivider(*upper, -shift, float(upper->getWidth() - 1));
    if (!aligned() || grid->getX() != gridX) return 107;
    juce::Button* browserButton = nullptr;
    for (auto* child : main.getChildren()) if (child->getTitle() == TRANS("Plug-in Browser"))
        browserButton = dynamic_cast<juce::Button*>(child);
    if (!browserButton || !browserButton->onClick) return 108;
    browserButton->onClick();
    auto* browserGrip = main.findChildWithID("plugin-browser-divider");
    PluginBrowser* browser = nullptr;
    for (auto* child : main.getChildren()) if (auto* panel = dynamic_cast<PluginBrowser*>(child)) browser = panel;
    if (!browser || !exposed(browserGrip) || browserGrip->getRight() != browser->getX()) return 109;
    const auto originalBrowser = browser->getBounds();
    const int wider = originalBrowser.getWidth() > 400 ? -36 : 36;
    dragDivider(*browserGrip, -wider, 1.f);
    const auto expanded = browser->getBounds();
    if (expanded.getRight() != originalBrowser.getRight() || expanded.getWidth() != originalBrowser.getWidth() + wider
        || !aligned() || !exposed(browserGrip) || browserGrip->getRight() != expanded.getX()) return 110;
    browserButton->onClick();
    if (browserGrip->isVisible() || browser->isVisible()) return 111;
    browserButton->onClick();
    if (expanded != browser->getBounds()) return 112;
    main.setSize(680, 570);
    if (!exposed(browserGrip) || !aligned() || browser->getWidth() > expanded.getWidth() || grid->getWidth() < 60)
        return 113;
    main.setSize(1280, 900);
    if (expanded != browser->getBounds()) return 114;
    dragDivider(*browserGrip, wider, float(browserGrip->getWidth() - 1));
    browserButton->onClick();
    juce::Button* shape = nullptr;
    for (auto* child : main.getChildren())
        if (auto* view = dynamic_cast<juce::Viewport*>(child); view && view->getViewedComponent()) {
            shape = dynamic_cast<juce::Button*>(view->getViewedComponent()->findChildWithID("shape-velocity"));
            if (shape) break;
        }
    if (!shape || !shape->isEnabled()) return 54;
    shape->onClick();
    const auto firstOnset = grid->notes.front().startTick;
    const auto lastOnset = grid->notes.back().startTick;
    for (const auto& note : grid->notes) {
        const double fraction = double(note.startTick - firstOnset) / std::max<juce::int64>(1, lastOnset - firstOnset);
        if (note.velocity != juce::roundToInt(65 + 40 * fraction)) return 55;
    }
    grid->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0));
    for (const auto& note : grid->notes)
        if (std::none_of(prior.begin(), prior.end(), [&](const auto& old) { return note.id == old.id && note.velocity == old.velocity; }))
            return 56;
    auto* picker = dynamic_cast<juce::ComboBox*>(main.findChildWithID("performance-picker"));
    auto* editor = dynamic_cast<PerformanceEditor*>(main.findChildWithID("performance-editor"));
    auto* enter = dynamic_cast<juce::Button*>(main.findChildWithID("performance-value-entry"));
    if (!picker || !editor || !enter || !enter->isVisible()) return 70;
    for (int i=0; i<picker->getNumItems(); ++i) if (picker->getItemText(i)=="CC 11")
        picker->setSelectedItemIndex(i,juce::sendNotificationSync);
    if (!editor->isVisible() || editor->getBounds().isEmpty() || velocity->isVisible()) return 71;
    enter->onClick();
    auto* valueDialog = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    if (!valueDialog || !valueDialog->getTextEditor("tick") || !valueDialog->getTextEditor("value")) return 117;
    valueDialog->exitModalState(0);
    const auto initialPoints=editor->points.size();
    const juce::Point<float> at{float(editor->getWidth()/2),8};
    editor->mouseDown(pointer(*editor,at,at)); editor->mouseUp(pointer(*editor,at,at));
    if (editor->points.size()!=initialPoints+1) return 72;
    main.keyPressed(juce::KeyPress('Z',juce::ModifierKeys::commandModifier,0));
    if (editor->points.size()!=initialPoints) return 73;
    auto* review = dynamic_cast<ChordReviewLane*>(main.findChildWithID("chord-review"));
    if (!review || !review->isVisible()) return 121;
    review->decide(true, ostfstudio::ChordReviewAction::keep);
    auto* drumMode = dynamic_cast<juce::ComboBox*>(main.findChildWithID("drum-mode"));
    auto* steps = dynamic_cast<DrumSteps*>(main.findChildWithID("drum-steps"));
    if (!drumMode || !steps) return 74;
    for (auto* child : main.getChildren()) {
        auto* select = dynamic_cast<juce::ComboBox*>(child);
        if (!select || select->getTitle()!=TRANS("Arrangement track")) continue;
        for (int i=0; i<select->getNumItems() && !drumMode->isVisible(); ++i)
            select->setSelectedItemIndex(i,juce::sendNotificationSync);
    }
    drumMode->setSelectedId(2,juce::sendNotificationSync);
    auto *bank = main.findChildWithID("drum-pad-bank");
    if (!bank || !bank->isVisible() || bank->getWidth() < 100 || bank->getHeight() < 80)
        return 91;
    auto* surface = dynamic_cast<DrumPads*>(bank);
    if (!exposed(column) || steps->getX() + DrumSteps::labelWidth != column->getRight() ||
        column->getY() < steps->getY() + DrumSteps::headerHeight) return 115;
    if (!surface || !steps->song || !surface->trigger || !surface->reorder) return 96;
    const auto hitCount = steps->song->slotHits.at(steps->track).size();
    const auto& kit = steps->song->slotsByTrack.at(steps->track);
    const auto firstId = kit.front().id;
    surface->trigger(0, true);
    surface->trigger(0, false);
    if (steps->song->slotHits.at(steps->track).size() != hitCount) return 97;
    const auto lit = steps->createComponentSnapshot(steps->getLocalBounds());
    bool rowLit = false;
    for (int y = 44; y < lit.getHeight(); ++y)
        if (lit.getPixelAt(1, y) == juce::Colour(0xffd28434)) { rowLit = true; break; }
    if (!rowLit) return 98;
    surface->reorder(0, 1);
    if (steps->song->slotsByTrack.at(steps->track)[1].id != firstId) return 99;
    if (!steps->isVisible() || steps->getWidth()<200 || grid->isVisible() || picker->isVisible()) return 75;
    const auto path=juce::SystemStats::getEnvironmentVariable("OSTF_DRUM_SMOKE_PNG",{});
    if (path.isNotEmpty()) {
        juce::FileOutputStream out{juce::File(path)};
        if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed()
            || !juce::PNGImageFormat().writeImageToStream(main.createComponentSnapshot(main.getLocalBounds()),out)) return 76;
    }
    drumMode->setSelectedId(1,juce::sendNotificationSync);
    if (steps->isVisible() || !grid->isVisible() || !picker->isVisible()) return 77;
    return 0;
}
