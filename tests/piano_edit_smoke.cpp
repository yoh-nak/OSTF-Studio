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
    SessionMix mix = song->savedMix.value_or(SessionMix{});
    mix.tracks.resize(song->trackLanes.size());
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
    MainComponent main;
    main.setVisible(true); main.setSize(1280, 900);
    if (!main.openDocument(fixture)) return 20;
    auto* grid = dynamic_cast<PianoNoteGrid*>(main.findChildWithID("piano-note-grid"));
    if (!grid || !grid->editable || !grid->isVisible() || grid->getWidth() < 200 || grid->getHeight() < 20) return 21;
    for (auto* child : main.getChildren()) if (auto* slider = dynamic_cast<juce::Slider*>(child))
        if (slider->getTitle() == TRANS("Timeline horizontal zoom")) slider->setValue(1, juce::sendNotificationSync);
    const auto initial = grid->notes.size();
    const juce::Point<float> empty {grid->getWidth() * .25f, grid->getHeight() * .45f};
    grid->mouseDoubleClick(pointer(*grid, empty, empty));
    if (grid->notes.size() != initial + 1 || grid->selection.size() != 1) return 22;
    auto selected = [&] { for (auto n : grid->notes) if (grid->selection.contains(n.id)) return n; return PitchedNote{}; };
    const auto inserted = selected();
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
    if (!column || !column->isVisible()) return 58;
    const int gridX = grid->getX(), columnX = column->getX();
    const int shift = gridX < 700 ? 32 : -32;
    column->mouseDrag(pointer(*column, {float(shift + 6), 10.f}, {0.f, 10.f}));
    if (grid->getX() != gridX + shift) return 59;
    column->mouseDrag(pointer(*column, {float(columnX - column->getX() + 6), 10.f}, {0.f, 10.f}));
    juce::Button* shape = nullptr;
    for (auto* child : main.getChildren())
        if (auto* view = dynamic_cast<juce::Viewport*>(child); view && view->getViewedComponent())
            shape = dynamic_cast<juce::Button*>(view->getViewedComponent()->findChildWithID("shape-velocity"));
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
    return 0;
}
