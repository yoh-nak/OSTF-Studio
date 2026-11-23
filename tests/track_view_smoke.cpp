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
    bool hasNotePreview = false;
    for (int y = 0; y < image.getHeight() && !hasNotePreview; ++y)
        for (int x = 425; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y) == juce::Colour(0xffb3ebdc)) { hasNotePreview = true; break; }
    if (!hasNotePreview) return 5;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (!guitar->compact() || bass->getY() != bassY + TrackRowSize::minimum - TrackRowSize::normal) return 6;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (guitar->height() != TrackRowSize::normal + 80) return 7;
    main.setSize(790, 570);
    if (!scroll->isVisible()) return 8;
    scroll->setCurrentRangeStart(scroll->getMaximumRangeLimit(), juce::sendNotificationSync);
    if (!reachable(*master) || reachable(*guitar)) return 9;
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
