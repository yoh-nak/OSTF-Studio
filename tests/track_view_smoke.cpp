#include "ui/MainComponent.h"

namespace {
juce::MouseEvent pointer(juce::Component& target, int y) {
    const auto time = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), {10.0f, static_cast<float>(y)},
            juce::ModifierKeys::leftButtonModifier, 1.0f, 0, 0, 0, 0, &target, &target, time, {10, 2}, time, 1, y != 2};
}

template <typename T>
T* findControl(juce::Component& root, const juce::String& title) {
    for (auto* child : root.getChildren()) {
        if (auto* control = dynamic_cast<T*>(child); control && control->getTitle() == title) return control;
        if (auto* found = findControl<T>(*child, title)) return found;
    }
    return nullptr;
}
}

int checkTrackView(const juce::File& fixture) {
    PianoNoteGrid grid;
    PianoInspector colours(grid);
    colours.showLegend = true;
    colours.setSize(492, 285);
    for (auto theme : {pianoHarmony::Theme::harmonic, pianoHarmony::Theme::studioBlue, pianoHarmony::Theme::graphite, pianoHarmony::Theme::aubergine}) {
        colours.theme = theme;
        const auto legend = colours.createComponentSnapshot(colours.getLocalBounds());
        for (int pc = 0; pc < 12; ++pc)
            if (legend.getPixelAt(8 + 40 * pc, PianoInspector::controlsHeight + 20) != pianoHarmony::degreeColour(pc + 60, 0)) return 67;
        if (legend.getPixelAt(7, PianoInspector::controlsHeight + 67) != pianoHarmony::toneColour(pianoHarmony::Tone::root)) return 68;
    }
    MainComponent main;
    main.setVisible(true);
    main.setSize(1450, 1200);
    auto* document = dynamic_cast<juce::Label*>(main.findChildWithID("current-document"));
    auto* tracks = findControl<juce::ComboBox>(main, TRANS("Arrangement track"));
    juce::MenuBarComponent* menus = nullptr;
    for (auto* child : main.getChildren())
        if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) menus = bar;
    if (!document || !document->getText().contains(TRANS("Unsaved project"))
        || !tracks || tracks->getNumItems() != 0 || !menus || !menus->getModel()) return 13;
    auto& model = *menus->getModel();
    const juce::StringArray names {TRANS("File"), TRANS("View"), TRANS("Track"), TRANS("Settings"), TRANS("Help")};
    if (model.getMenuBarNames() != names) return 38;
    const auto help = model.getMenuForIndex(4, {});
    bool hasShortcuts = false;
    for (juce::PopupMenu::MenuItemIterator it(help); it.next();)
        if (it.getItem().text == TRANS("Shortcuts...")) hasShortcuts = it.getItem().isEnabled;
    if (!hasShortcuts) return 39;
    const auto fileMenu = model.getMenuForIndex(0, {});
    bool newEnabled = false, saveScoreEnabled = false, savePackageEnabled = false;
    juce::StringArray songs;
    for (juce::PopupMenu::MenuItemIterator it(fileMenu, true); it.next();) {
        const auto& item = it.getItem();
        if (item.text == TRANS("New project")) newEnabled = item.isEnabled;
        if (item.text == TRANS("Save as OSTF...")) saveScoreEnabled = item.isEnabled;
        if (item.text == TRANS("Save as OSTFP...")) savePackageEnabled = item.isEnabled;
        if (item.text == TRANS("Demo songs") && item.subMenu)
            for (juce::PopupMenu::MenuItemIterator demo(*item.subMenu); demo.next();)
                songs.add(demo.getItem().text);
    }
    if (!newEnabled || !saveScoreEnabled || !savePackageEnabled || songs.size() != 6
        || !songs.contains("Copper Mile (Rock)") || !songs.contains("First Light Vector (Trance)")) return 14;
    auto* group = findControl<juce::ComboBox>(main, TRANS("Track height group"));
    auto* height = findControl<juce::Slider>(main, TRANS("Group track height"));
    if (!group || !height) return 16;
    const auto choose = [&](const juce::String& title) {
        for (int i = 0; i < group->getNumItems(); ++i)
            if (group->getItemText(i) == title) {
                group->setSelectedItemIndex(i, juce::sendNotificationSync);
                return true;
            }
        return false;
    };
    if (!choose(TRANS("Pitched tracks"))) return 17;
    height->setValue(84, juce::sendNotificationSync);
    const auto trackMenu = model.getMenuForIndex(2, {});
    for (juce::PopupMenu::MenuItemIterator it(trackMenu); it.next();)
        if (it.getItem().text == TRANS("Add pitched track") && it.getItem().isEnabled)
            model.menuItemSelected(it.getItem().itemID, 2);
    if (tracks->getNumItems() != 1 || tracks->getSelectedId() != 1) return 15;
    auto* addedGrip = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!addedGrip || addedGrip->height() != 84) return 18;
    auto* info = findControl<ViewButton>(main, TRANS("Song information"));
    if (!info || !info->isEnabled() || info->getTooltip().isEmpty() || !info->onClick) return 37;
    const auto click = [&](const char* label) {
        auto* button = findControl<ViewButton>(main, juce::translate(label));
        if (!button || !button->isEnabled() || button->getTooltip().isEmpty() || !button->onClick) return false;
        button->onClick();
        return true;
    };
    main.setSize(680, 570);
    juce::RectangleList<int> icons;
    for (auto* child : main.getChildren()) {
        if (auto* button = dynamic_cast<ViewButton*>(child)) {
            if (button->getTooltip().isEmpty() || icons.intersectsRectangle(button->getBounds())
                || !main.getLocalBounds().contains(button->getBounds())) return 33;
            icons.add(button->getBounds());
        }
    }
    if (!click("Add melody track") || !click("Add chorus vocal track") || !click("Add drum / percussion track")
        || !click("Add audio track") || tracks->getNumItems() != 5) return 34;
    auto* audioEditor = dynamic_cast<AudioClipEditor*>(main.findChildWithID("audio-clip-editor"));
    if (!audioEditor || !audioEditor->isVisible() || audioEditor->getWidth() < 200) return 51;
    const auto audioTrack = tracks->getSelectedId();
    tracks->setSelectedId(1, juce::sendNotificationSync);
    if (audioEditor->isVisible()) return 52;
    tracks->setSelectedId(audioTrack, juce::sendNotificationSync);
    if (!audioEditor->isVisible() || !main.isInterestedInFileDrag({"Splice sample.wav"})
        || main.isInterestedInFileDrag({"notes.mid"})) return 53;
    main.setSize(1450, 1200);
    const auto sample = fixture.getParentDirectory().getParentDirectory().getParentDirectory()
        .getChildFile("examples/demo-songs/assets/glass-lantern-chimes.wav");
    const auto drop = audioEditor->getBounds().getCentre();
    main.filesDropped({sample.getFullPathName()}, drop.x, drop.y);
    const auto* imported = audioEditor->sourceClip();
    if (!imported || imported->localFile != sample || imported->durationTicks <= 0) return 54;
    for (auto* child : audioEditor->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b && (b->getButtonText() == TRANS("Warp (re-pitch)") || b->getButtonText() == TRANS("Loop"))) {
            if (!b->isEnabled()) return 55;
            b->setToggleState(true, juce::dontSendNotification);
            b->onClick();
        }
    if (!imported->timing.loop || !imported->timing.cycleTicks) return 56;
    const auto editorPoint = audioEditor->getLocalBounds().getCentre().toFloat();
    const auto gesture = [](juce::Component* target, juce::Point<float> from, juce::Point<float> to) {
        const auto time = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), to,
            juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, target, target, time, from, time, 1, from != to);
    };
    audioEditor->mouseDoubleClick(gesture(audioEditor, editorPoint, editorPoint));
    if (imported->timing.markers.size() != 1) return 57;
    auto later = editorPoint.translated(65, 0);
    audioEditor->mouseDown(gesture(audioEditor, editorPoint, editorPoint));
    audioEditor->mouseDrag(gesture(audioEditor, editorPoint, later));
    audioEditor->mouseUp(gesture(audioEditor, editorPoint, later));
    if (imported->timing.markers[0].position <= imported->timing.markers[0].source) return 58;
    auto* grip = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!grip) return 59;
    const auto row = main.getLocalArea(grip, grip->getLocalBounds());
    const auto timeline = main.getLocalArea(&main, main.getLocalBounds());
    // Ask the hit-tested right edge (the amber grip) for its actual pixel, regardless of saved zoom or UI scale.
    const auto snapshot = main.createComponentSnapshot(timeline);
    int edge = -1;
    const int rowY = row.getY() - 6;
    for (int x = row.getX() + 150; x < row.getRight(); ++x)
        if (snapshot.getPixelAt(x, rowY).getARGB() == 0xffffc479) edge = x;
    if (edge < 0) return 60;
    const juce::Point<float> edgePoint{float(edge), float(rowY)};
    const auto extendTo = edgePoint.translated(50, 0);
    const auto length = imported->durationTicks;
    main.mouseDown(gesture(&main, edgePoint, edgePoint));
    main.mouseDrag(gesture(&main, edgePoint, extendTo));
    main.mouseUp(gesture(&main, edgePoint, extendTo));
    if (imported->durationTicks <= length) return 61;
    const auto selected = tracks->getText();
    auto trackActions = model.getMenuForIndex(2, {});
    for (juce::PopupMenu::MenuItemIterator it(trackActions); it.next();)
        if (it.getItem().text == TRANS("Duplicate track") && it.getItem().isEnabled)
            model.menuItemSelected(it.getItem().itemID, 2);
    if (tracks->getNumItems() != 6 || !tracks->getText().startsWith(selected)) return 35;
    if (!click("Add bus track") || !click("Add return track")) return 36;
    const auto* busC = findControl<TrackRowSize>(main, "Bus C track height");
    const auto* return2 = findControl<TrackRowSize>(main, "Return 2 track height");
    if (!busC || !busC->isVisible() || !return2 || !return2->isVisible()) return 37;
    auto& desktop = juce::Desktop::getInstance();
    const auto scale = desktop.getGlobalScaleFactor();
    const bool atMaxScale = scale >= 1.5f;
    if (!click(atMaxScale ? "Zoom out interface" : "Zoom in interface") || scale == desktop.getGlobalScaleFactor()
        || !click(atMaxScale ? "Zoom in interface" : "Zoom out interface") || scale != desktop.getGlobalScaleFactor()) return 38;
    main.setSize(1450, 1200);
    if (!main.openDocument(fixture)) return 1;
    auto* guitar = findControl<TrackRowSize>(main, "Lead Guitar track height");
    auto* bass = findControl<TrackRowSize>(main, "Bass track height");
    auto* master = findControl<TrackRowSize>(main, "Master track height");
    auto* scroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-vertical-scroll"));
    if (!guitar || !bass || !master || !scroll) return 2;
    auto reachable = [&](TrackRowSize& row) {
        return main.getComponentAt(main.getLocalPoint(&row, juce::Point<int>{10, 2})) == &row;
    };
    if (!reachable(*guitar)) return 3;
    const int bassY = bass->getY();
    guitar->mouseDown(pointer(*guitar, 2));
    guitar->mouseDrag(pointer(*guitar, 82));
    if (!guitar->expanded() || guitar->height() != TrackRowSize::normal + 80
        || bass->getY() != bassY + 80 || bass->height() != TrackRowSize::normal) return 4;
    const auto image = main.createComponentSnapshot(main.getLocalBounds());
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    bool hasNotePreview = false;
    for (int y = 0; y < image.getHeight() && !hasNotePreview; ++y)
        for (int x = 425; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y) == juce::Colour(0xffb3ebdc)) { hasNotePreview = true; break; }
    if (!hasNotePreview) return 5;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (!guitar->compact() || bass->getY() != bassY + TrackRowSize::minimum - TrackRowSize::normal) return 6;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (guitar->height() != TrackRowSize::normal + 80) return 7;
    main.setSize(790, 570);
    if (!scroll->isVisible()) return 8;
    scroll->setCurrentRangeStart(scroll->getMaximumRangeLimit(), juce::sendNotificationSync);
    auto* lastReturn = findControl<TrackRowSize>(main, "Return 1 track height");
    auto* ruler = master->getParentComponent()->findChildWithID("arrangement-ruler");
    if (!lastReturn || !reachable(*lastReturn) || reachable(*guitar) || !ruler || ruler->getY() != 0) return 9;
    // A short viewport can fit fewer than four output strips beneath the pinned ruler.
    if (master->getY() < ruler->getBottom())
        scroll->setCurrentRangeStart(scroll->getCurrentRangeStart() + master->getY() - ruler->getBottom(), juce::sendNotificationSync);
    if (!reachable(*master)) return 43;
    master->mouseDown(pointer(*master, 2));
    master->mouseDrag(pointer(*master, 55));
    if (!master->expanded() || !guitar->expanded()) return 10;

    auto* busA = findControl<TrackRowSize>(main, "Bus A track height");
    auto* busB = findControl<TrackRowSize>(main, "Bus B track height");
    auto* drums = findControl<TrackRowSize>(main, "Drums track height");
    if (!busA || !busB || !drums || !choose(TRANS("Output tracks"))) return 19;
    if (height->getTextFromValue(height->getValue()) != TRANS("Mixed")) return 20;
    height->setValue(94, juce::sendNotificationSync);
    if (master->height() != 94 || busA->height() != 94 || busB->height() != 94 || !guitar->expanded()) return 21;
    if (!choose(TRANS("Buses"))) return 22;
    height->setValue(38, juce::sendNotificationSync);
    if (master->height() != 94 || busA->height() != 38 || busB->height() != 38) return 23;
    if (!choose(TRANS("Drum tracks"))) return 24;
    height->setValue(79, juce::sendNotificationSync);
    if (drums->height() != 79 || bass->height() != TrackRowSize::normal) return 25;
    if (!choose(TRANS("Timeline lanes"))) return 26;
    scroll->setCurrentRangeStart(0, juce::sendNotificationSync);
    const int beforeLanes = bass->getY();
    height->setValue(62, juce::sendNotificationSync);
    if (bass->getY() <= beforeLanes || bass->height() != TrackRowSize::normal) return 27;
    if (ruler->getHeight() != 62 || group->getLookAndFeel().getPopupMenuFont().getHeight() < 15.f) return 62;
    auto* dock = dynamic_cast<juce::Viewport*>(main.findChildWithID("song-basics-dock"));
    auto* basics = dock ? dynamic_cast<SongBasicsPanel*>(dock->getViewedComponent()) : nullptr;
    if (!basics || !dock->isVisible() || !basics->moveDock || !basics->commit) return 63;
    const auto oldBounds = dock->getBounds();
    basics->moveDock();
    if (dock->getBounds() == oldBounds || !dock->isVisible()) return 64;
    basics->moveDock();
    basics->commit(SongBasicsPanel::title, "Edited title", 0);
    if (!document->getText().contains("Edited title")) return 65;
    basics->commit(SongBasicsPanel::key, {}, 10 + 8 * 3 + 1);
    auto* keys = dynamic_cast<juce::ComboBox*>(basics->findChildWithID("song-base-key"));
    if (!keys || keys->getText() != "Eb minor") return 66;
    if (!choose(TRANS("All tracks"))) return 28;
    height->setValue(35, juce::sendNotificationSync);
    if (!guitar->compact() || !drums->compact() || !master->compact() || !bass->compact()) return 29;

    // Paint independent L/R peaks and confirm silence and overload colours in both orientations.
    for (bool vertical : {false, true}) {
        LevelMeter meter(vertical);
        meter.setSize(vertical ? 16 : 120, vertical ? 120 : 16);
        meter.update({1.1f, 0.f}, 1.0);
        const auto lit = meter.createComponentSnapshot(meter.getLocalBounds());
        if (lit.getPixelAt(vertical ? 4 : 118, vertical ? 1 : 4) != juce::Colour(0xffff5654)
            || lit.getPixelAt(vertical ? 12 : 60, vertical ? 60 : 12) != juce::Colour(0xff152026)) return 11;
        meter.update({}, 5.0);
        const auto silent = meter.createComponentSnapshot(meter.getLocalBounds());
        if (silent.getPixelAt(vertical ? 4 : 60, vertical ? 60 : 4) != juce::Colour(0xff152026)) return 12;
    }
    if (!main.keyPressed(juce::KeyPress(juce::KeyPress::F1Key))) return 40;
    auto* guide = dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());
    if (!guide || guide->getName() != TRANS("Shortcuts") || !guide->getContentComponent()) return 41;
    bool listsPlayback = false, listsRuler = false;
    for (auto* child : guide->getContentComponent()->getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            listsPlayback |= label->getText() == TRANS("Play / stop");
            listsRuler |= label->getText() == TRANS("Click the bar ruler");
        }
    guide->exitModalState(0);
    if (!listsPlayback || !listsRuler) return 42;
    return 0;
}
