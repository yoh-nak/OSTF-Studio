#include "ui/MainComponent.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
template <typename T>
T* descendant(juce::Component& parent) {
    for (auto* child : parent.getChildren()) {
        if (auto* match = dynamic_cast<T*>(child)) return match;
        if (auto* match = descendant<T>(*child)) return match;
    }
    return nullptr;
}

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

// Optional integration check using the user's installed VST3 and its real native editor.
// Exercise the same menu item twice, closing and reopening the independent window.
std::unique_ptr<juce::Timer> startSpliceWindowCheck(std::function<void(int)> finished) {
    class Probe final : public juce::Timer {
    public:
        explicit Probe(std::function<void(int)> done) : finish(std::move(done)) {
            studio.setBounds(-12000, -12000, 1440, 900);
            studio.addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
            studio.setVisible(true);
            for (auto* child : studio.getChildren())
                if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) menus = bar->getModel();
            if (menus) {
                const auto menu = menus->getMenuForIndex(5, {});
                for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                    if (it.getItem().text == TRANS("Open Splice Sounds")) command = it.getItem().itemID;
                if (auto* pane = descendant<PluginBrowser>(studio)) {
                    browserBounds = pane->getBounds();
                    browserVisible = pane->isVisible();
                }
                if (command) menus->menuItemSelected(command, 5);
            }
            startTimer(100);
        }
    private:
        int inspect() {
            if (!command) return 1;
            PluginBrowser* pane = nullptr;
            for (auto* child : studio.getChildren())
                if (auto* p = dynamic_cast<PluginBrowser*>(child)) pane = p;
            if (!pane || pane->isVisible() != browserVisible || (browserVisible && pane->getBounds() != browserBounds)) return 2;
            juce::DocumentWindow* window = nullptr;
            auto& desktop = juce::Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i) {
                auto* component = desktop.getComponent(i);
                if (component->getName() == "Splice Sounds") window = dynamic_cast<juce::DocumentWindow*>(component);
            }
            auto* editor = window ? descendant<juce::AudioProcessorEditor>(*window) : nullptr;
            if (!editor) {
                const auto types = pane->types();
                const bool installed = std::any_of(types.begin(), types.end(), [](const auto& t) {
                    return t.name.equalsIgnoreCase("Splice Sounds") && t.pluginFormatName == "VST3";
                });
                return installed ? -1 : 77;
            }
            if (!editor->processor.getName().equalsIgnoreCase("Splice Sounds")
                || !editor->isShowing() || editor->getPeer() == studio.getPeer()
                || editor->getWidth() <= 0 || editor->getHeight() <= 0) return 3;
#if JUCE_WINDOWS
            auto* native = descendant<juce::HWNDComponent>(*editor);
            if (!native) return -1;
            auto host = static_cast<HWND>(native->getHWND());
            if (!host || !IsChild(static_cast<HWND>(window->getPeer()->getNativeHandle()), host)
                || !GetWindow(host, GW_CHILD)) return -1;
#endif
            if (!firstEditor) {
                firstEditor = editor;
                window->closeButtonPressed();
                if (editor->isShowing()) return 5;
                menus->menuItemSelected(command, 5);
                return -1;
            }
            return editor == firstEditor ? 0 : 6;
        }
        void timerCallback() override {
            int result = inspect();
            if (result < 0 && juce::Time::getMillisecondCounterHiRes() < deadline) return;
            stopTimer();
            finish(result < 0 ? 7 : result);
        }
        bool browserVisible = false;
        juce::Rectangle<int> browserBounds;
        MainComponent studio;
        std::function<void(int)> finish;
        juce::MenuBarModel* menus = nullptr;
        juce::Component::SafePointer<juce::AudioProcessorEditor> firstEditor;
        int command = 0;
        double deadline = juce::Time::getMillisecondCounterHiRes() + 30000;
    };
    return std::make_unique<Probe>(std::move(finished));
}

int checkTrackView(const juce::File& fixture) {
    using namespace ostfstudio;
    using A = SurfaceAction;
    if (mackieAction(juce::MidiMessage::pitchWheel(8,16383)).index != 7
        || mackieAction(juce::MidiMessage::controllerEvent(1,16,65)).value != -1
        || mackieAction(juce::MidiMessage::pitchWheel(9,8192)).kind != A::none) return 95;
    auto note = [](int n) { return mackieAction(juce::MidiMessage::noteOn(1,n,juce::uint8(127))); };
    if (note(0x5e).kind != A::play || note(0x5f).kind != A::record || note(0x2f).kind != A::bankRight
        || note(0x13).kind != A::mute || note(0x13).index != 3
        || mackieAction(juce::MidiMessage::noteOff(1,0x5e)).kind != A::none) return 93;
    MidiInputRoute route{"selected-port", "Selected keyboard", 2};
    if (!route.accepts("selected-port", 2) || route.accepts("selected-port", 1) || route.accepts("other-port", 2)) return 101;
    route.enabled = false;
    if (route.accepts("selected-port", 2)) return 102;
    route = {};
    if (!route.accepts("other-port", 16) || route.accepts("other-port", 0) || route.accepts("other-port", 17)) return 103;
    PianoNoteGrid grid;
    PianoInspector colours(grid);
    colours.showLegend = true;
    colours.setSize(492, 285);
    for (int i = 0; i < static_cast<int>(pianoHarmony::Theme::count); ++i) {
        colours.theme = static_cast<pianoHarmony::Theme>(i);
        const auto legend = colours.createComponentSnapshot(colours.getLocalBounds());
        for (int pc = 0; pc < 12; ++pc)
            if (legend.getPixelAt(8 + 40 * pc, PianoInspector::controlsHeight + 20) != pianoHarmony::degreeColour(pc + 60, 0)) return 67;
        if (legend.getPixelAt(7, PianoInspector::controlsHeight + 67) != pianoHarmony::toneColour(pianoHarmony::Tone::root)) return 68;
    }
    double scrolled = 0;
    grid.setSize(200, 90);
    grid.viewportPan.scroll = [&](double fraction) { scrolled += fraction; };
    const auto when = juce::Time::getCurrentTime();
    const auto panEvent = [&](float x, int modifiers) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {x, 30}, modifiers,
            1.f, 0, 0, 0, 0, &grid, &grid, when, {100, 30}, when, 1, x != 100);
    };
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown(panEvent(100, left | juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier));
    grid.mouseDrag(panEvent(60, left));
    grid.mouseUp(panEvent(60, 0));
    if (std::abs(scrolled - .2) > .0001 || !grid.selection.isEmpty()) return 134;
    MainComponent main;
    main.setVisible(true);
    main.setSize(1450, 1200);
    auto* tracks = findControl<juce::ComboBox>(main, TRANS("Arrangement track"));
    juce::MenuBarComponent* menus = nullptr;
    for (auto* child : main.getChildren())
        if (auto* bar = dynamic_cast<juce::MenuBarComponent*>(child)) menus = bar;
    if (!main.getName().contains(TRANS("Unsaved project"))
        || !tracks || tracks->getNumItems() != 0 || !menus || !menus->getModel()) return 13;
    auto& model = *menus->getModel();
    const juce::StringArray names {TRANS("File"), TRANS("View"), TRANS("Track"), TRANS("Mode"), TRANS("Settings"), TRANS("External connections"), TRANS("Help")};
    if (model.getMenuBarNames() != names) return 38;
    const auto external = model.getMenuForIndex(5, {});
    juce::StringArray actions;
    for (juce::PopupMenu::MenuItemIterator it(external); it.next();) actions.add(it.getItem().text);
    if (!actions.contains(TRANS("Open Splice Sounds")) || !actions.contains("Melodyne...") || !actions.contains("Ableton Link")) return 121;
    auto* examples = findControl<juce::TextEditor>(main, TRANS("Example prompts"));
    auto* connection = main.findChildWithID("chatgpt-connection");
    if (!examples || !examples->isReadOnly() || !connection || !connection->isVisible()) return 122;
    const juce::StringArray modeNames{"AI Arrangement Studio", "AI Lyric Studio", "AI Vocal Studio"};
    const auto modes = model.getMenuForIndex(3, {});
    if (modes.getNumItems() != modeNames.size()) return 122;
    juce::StringArray displayedExamples;
    int previousMode = 0;
    for (juce::PopupMenu::MenuItemIterator it(modes); it.next();) {
        const auto& mode = it.getItem();
        if (!modeNames.contains(mode.text) || !mode.isEnabled) return 122;
        if (mode.isTicked) previousMode = mode.itemID;
        model.menuItemSelected(mode.itemID, 3);
        const auto selectedMenu = model.getMenuForIndex(3, {});
        for (juce::PopupMenu::MenuItemIterator selected(selectedMenu); selected.next();)
            if (selected.getItem().isTicked != (selected.getItem().itemID == mode.itemID)) return 122;
        const auto example = examples->getText();
        if (example.isEmpty() || displayedExamples.contains(example)) return 122;
        displayedExamples.add(example);
        auto* next = findControl<juce::TextButton>(main, TRANS("Next example"));
        auto* previous = findControl<juce::TextButton>(main, TRANS("Previous example"));
        if (!next || !previous) return 162;
        next->onClick();
        if (examples->getText().isEmpty() || examples->getText() == example) return 162;
        previous->onClick();
        if (examples->getText() != example) return 162;
    }
    if (!previousMode) return 122;
    model.menuItemSelected(previousMode, 3);
    if (!findControl<EditorClipLane>(main, TRANS("Clips"))) return 123;
    EditorClipLane exampleLane;
    exampleLane.clips = {{{1, 2}, "A"}, {{3, 5}, "B"}};
    if (exampleLane.at(1.5) != 1. || exampleLane.at(2.) || exampleLane.at(3.) != 3. || exampleLane.at(5.)) return 124;
    const auto help = model.getMenuForIndex(6, {});
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
    const auto renameMenu = model.getMenuForIndex(2, {});
    int renameId = 0;
    for (juce::PopupMenu::MenuItemIterator it(renameMenu); it.next();)
        if (it.getItem().text == TRANS("Rename track...")) renameId = it.getItem().itemID;
    if (!renameId) return 126;
    model.menuItemSelected(renameId, 2);
    auto* inlineName = dynamic_cast<juce::TextEditor*>(main.findChildWithID("inline-track-name"));
    if (!inlineName || !inlineName->isVisible()) return 127;
    const auto originalName = tracks->getText();
    inlineName->setText("Keep original", false);
    inlineName->onEscapeKey();
    if (inlineName->isVisible() || tracks->getText() != originalName) return 128;
    model.menuItemSelected(renameId, 2);
    inlineName->setText("Inline track", false);
    inlineName->onReturnKey();
    if (inlineName->isVisible() || tracks->getText() != "Inline track") return 129;
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
    icons.add(connection->getBounds());
    for (auto* child : main.getChildren()) {
        if (auto* button = dynamic_cast<ViewButton*>(child)) {
            if (button->getTooltip().isEmpty() || icons.intersectsRectangle(button->getBounds())
                || !main.getLocalBounds().contains(button->getBounds())) return 33;
            icons.add(button->getBounds());
        }
    }
    if (!click("Add melody track")) return 91;
    auto* melody = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!melody || melody->height() != 100) return 92;
    auto* arm = findControl<juce::TextButton>(main, tracks->getText() + " record arm");
    auto* input = findControl<juce::TextButton>(main, tracks->getText() + " MIDI input");
    if (!arm || !arm->isVisible() || !input || !input->isVisible()) return 105;
    arm->onClick();
    if (!arm->getToggleState()) return 106;
    arm->onClick();
    if (arm->getToggleState()) return 107;
    if (!click("Add chorus vocal track") || !click("Add drum / percussion track")
        || !click("Add audio track") || tracks->getNumItems() != 5) return 34;
    auto* audioEditor = dynamic_cast<AudioClipEditor*>(main.findChildWithID("audio-clip-editor"));
    if (!audioEditor || !audioEditor->isVisible() || audioEditor->getWidth() < 200) return 51;
    if (!audioEditor->recordRuler.chooseCycle || !audioEditor->recordRuler.cycleBars) return 67;
    audioEditor->recordRuler.chooseCycle(.12, .39);
    if (audioEditor->recordRuler.cycleBars().isEmpty()) return 68;
    audioEditor->channels.setSelectedId(2, juce::sendNotificationSync);
    const auto audioTrack = tracks->getSelectedId();
    tracks->setSelectedId(1, juce::sendNotificationSync);
    if (audioEditor->isVisible()) return 52;
    tracks->setSelectedId(audioTrack, juce::sendNotificationSync);
    if (audioEditor->channels.getSelectedId() != 2) return 69;
    if (!audioEditor->isVisible() || !main.isInterestedInFileDrag({"Splice sample.wav"})
        || main.isInterestedInFileDrag({"notes.mid"})) return 53;
    main.setSize(1450, 1200);
    const auto sample = fixture.getParentDirectory().getParentDirectory().getParentDirectory()
        .getChildFile("examples/demo-songs/assets/glass-lantern-chimes.wav");
    const auto drop = audioEditor->getBounds().getCentre();
    auto* pane = descendant<PluginBrowser>(main);
    if (!pane && !click("Plug-in Browser")) return 140;
    pane = descendant<PluginBrowser>(main);
    if (!pane) return 140;
    auto* assets = dynamic_cast<juce::Button*>(pane->findChildWithID("browser-samples"));
    auto* plugins = dynamic_cast<juce::Button*>(pane->findChildWithID("browser-plugins"));
    if (!assets || !plugins) return 141;
    assets->onClick();
    auto* files = descendant<juce::FileBrowserComponent>(*pane);
    if (!files || !files->isVisible() || !files->getDisplayComponent()) return 142;
    files->setRoot(sample.getParentDirectory());
    const juce::DragAndDropTarget::SourceDetails dragging("ostf-audio:" + sample.getFullPathName(),files,drop);
    auto* dropTarget = dynamic_cast<juce::DragAndDropTarget*>(static_cast<juce::Component*>(&main));
    if (!dropTarget || !dropTarget->isInterestedInDragSource(dragging)) return 143;
    dropTarget->itemDropped(dragging);
    plugins->onClick();
    if (files->isVisible() || !plugins->getToggleState()) return 144;
    const auto* imported = audioEditor->sourceClip();
    if (!imported || imported->localFile != sample || imported->durationTicks <= 0) return 54;
    for (auto* child : audioEditor->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b && (b->getButtonText() == TRANS("Warp (re-pitch)") || b->getButtonText() == TRANS("Loop"))) {
            if (!b->isEnabled()) return 55;
            b->setToggleState(true, juce::dontSendNotification);
            b->onClick();
        }
    if (!imported->timing.loop || !imported->timing.cycleTicks) return 56;
    auto* clipLane = findControl<EditorClipLane>(main, TRANS("Clips"));
    const auto audioGrid = audioEditor->recordRuler.getBounds();
    if (!clipLane || clipLane->clips.empty()
        || main.getLocalArea(&audioEditor->recordRuler, audioEditor->recordRuler.getLocalBounds()).getX() != clipLane->getX()
        || audioGrid.getWidth() != clipLane->getWidth() || audioEditor->channels.getRight() > audioGrid.getX()) return 132;
    auto* audioGrip = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    auto* upperRuler = audioGrip ? dynamic_cast<ArrangementRuler*>(audioGrip->getParentComponent()->findChildWithID("arrangement-ruler")) : nullptr;
    if (!upperRuler) return 138;
    const auto upperArea = main.getLocalArea(upperRuler, upperRuler->getLocalBounds()).withTrimmedLeft(upperRuler->headerWidth);
    if (upperArea.getX() != clipLane->getX() || upperArea.getRight() != clipLane->getRight()) return 139;
    const auto bars = audioEditor->recordRuler.visibleBars();
    const auto shown = bars.getIntersectionWith(clipLane->clips.front().bars);
    if (shown.isEmpty()) return 133;
    const auto editorPoint = juce::Point<float>(
        float(audioGrid.getX() + ((shown.getStart() + shown.getLength() * .25) - bars.getStart()) * audioGrid.getWidth() / bars.getLength()),
        float(std::max(audioGrid.getBottom() + 2, audioEditor->getHeight() / 2)));
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
    auto enableFitLanes = [&model] {
        juce::Array<int> toggled;
        for (const auto* caption : {"Show Theory Guide", "Chord tone fit", "Scale fit"}) {
            auto viewMenu = model.getMenuForIndex(1, {});
            for (juce::PopupMenu::MenuItemIterator it(viewMenu, true); it.next();)
                if (it.getItem().text == juce::translate(caption) && !it.getItem().isTicked) {
                    toggled.add(it.getItem().itemID);
                    model.menuItemSelected(it.getItem().itemID, 1);
                    break;
                }
        }
        return toggled;
    };
    const auto audioToggles = enableFitLanes();
    auto* firstAudio = findControl<TrackRowSize>(main, selected + " " + TRANS("track height"));
    auto* copiedAudio = findControl<TrackRowSize>(main, tracks->getText() + " " + TRANS("track height"));
    if (!firstAudio || !copiedAudio || copiedAudio->getY() - firstAudio->getY() != copiedAudio->height()) return 108;
    for (int i = audioToggles.size(); --i >= 0;) model.menuItemSelected(audioToggles[i], 1);
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
    {
        std::unique_ptr<OstfpPreview> source;
        juce::FileInputStream input(fixture);
        if (readOstfpPreview(input,source).failed()) return 96;
        auto it = std::find_if(source->pitchedNotes.begin(), source->pitchedNotes.end(), [](const auto& n){ return !n.empty(); });
        if (it == source->pitchedNotes.end()) return 97;
        const int index = int(it - source->pitchedNotes.begin());
        ImportResultRow importedRow("Drop test",*source,index);
        auto* dropRuler = busC->getParentComponent()->findChildWithID("arrangement-ruler");
        if (!dropRuler) return 98;
        const auto pos = main.getLocalPoint(dropRuler,juce::Point<int>(12,4));
        const auto count = tracks->getNumItems();
        // Resolve the receiving component from the arrangement position, just as JUCE
        // does when a row from a separate drag container is released over a child.
        struct Review : juce::Component, juce::DragAndDropContainer {} review;
        juce::Viewport viewport;
        juce::Component rows;
        rows.addAndMakeVisible(importedRow);
        viewport.setViewedComponent(&rows, false);
        review.addAndMakeVisible(viewport);
        if (!importedRow.getViewportIgnoreDragFlag()
            || juce::DragAndDropContainer::findParentDragContainerFor(&importedRow) != &review) return 99;
        juce::DragAndDropTarget::SourceDetails details("ostf-import-result", &importedRow, {});
        juce::DragAndDropTarget* receiver = nullptr;
        for (auto* hit = main.getComponentAt(pos); hit != nullptr; hit = hit->getParentComponent()) {
            auto* target = dynamic_cast<juce::DragAndDropTarget*>(hit);
            if (target && target->isInterestedInDragSource(details)) {
                receiver = target;
                details.localPosition = hit->getLocalPoint(&main, pos);
                break;
            }
        }
        if (!receiver) return 99;
        receiver->itemDragMove(details);
        receiver->itemDropped(details);
        if (tracks->getNumItems() != count+1 || tracks->getText()!=juce::String::fromUTF8(source->trackLanes[index].label)) return 100;
    }
    if (!main.openDocument(fixture)) return 1;
    {
        auto* divider = dynamic_cast<EditorDivider*>(main.findChildWithID("piano-divider"));
        auto menu = model.getMenuForIndex(1, {});
        int mixerId = 0, pianoId = 0;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
            if (it.getItem().text == TRANS("Mixer")) mixerId = it.getItem().itemID;
            if (it.getItem().text == TRANS("Piano Roll")) pianoId = it.getItem().itemID;
        }
        if (!divider || !mixerId || !pianoId) return 135;
        model.menuItemSelected(mixerId,1);
        if (!divider->isVisible() || !divider->resizeAt) return 136;
        const int y = divider->getY();
        divider->resizeAt(y + (y > main.getHeight()/2 ? -46 : 46));
        if (std::abs(divider->getY() - y) < 20) return 137;
        divider->resizeAt(y);
        model.menuItemSelected(pianoId,1);
    }
    auto* guitar = findControl<TrackRowSize>(main, "Lead Guitar track height");
    auto* bass = findControl<TrackRowSize>(main, "Bass track height");
    auto* master = findControl<TrackRowSize>(main, "Master track height");
    auto* scroll = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-vertical-scroll"));
    if (!guitar || !bass || !master || !scroll) return 2;
    auto reachable = [&](TrackRowSize& row) {
        return main.getComponentAt(main.getLocalPoint(&row, juce::Point<int>{10, 2})) == &row;
    };
    if (master->height() != TrackRowSize::minimum || bass->height() != 100) return 70;
    auto* tempo = dynamic_cast<TempoEditor*>(main.findChildWithID("tempo-editor"));
    auto* barRuler = guitar->getParentComponent()->findChildWithID("arrangement-ruler");
    auto* topRuler = dynamic_cast<ArrangementRuler*>(barRuler);
    if (!tempo || !topRuler || main.getName().contains(TRANS("Unsaved changes"))) return 75;
    const auto rulerArea = main.getLocalArea(barRuler, barRuler->getLocalBounds());
    for (int y = rulerArea.getBottom() + 2; y < main.getLocalPoint(guitar, juce::Point<int>{0, 0}).y && !tempo->isVisible(); y += 10) {
        const juce::Point<float> header{float(rulerArea.getX() + topRuler->headerWidth - 12), float(y)};
        main.mouseDown(gesture(&main, header, header));
    }
    if (!tempo->isVisible() || tracks->isVisible() || audioEditor->isVisible()) return 76;
    auto* promptPane = findControl<PromptGuide>(main, TRANS("Prompt guide"));
    auto* picker = findControl<juce::ComboBox>(main, TRANS("Analysis model"));
    auto* refresh = findControl<juce::TextButton>(main, TRANS("Refresh models"));
    auto* deck = dynamic_cast<TransportPanel*>(main.findChildWithID("transport"));
    if (!deck || !deck->click.getClickingTogglesState() || deck->countIn.getNumItems() != 9
        || !picker || !refresh || picker->getParentComponent() != &main || refresh->getParentComponent() != &main
        || connection->getY() < menus->getBottom() || connection->getBottom() > deck->getY()
        || connection->getX() < main.getWidth() / 2) return 140;
    const juce::Font heading(juce::FontOptions(15.f, juce::Font::bold));
    const auto reserved = juce::GlyphArrangement::getStringWidth(heading, TRANS("Degree") + " / " + TRANS("Progression"));
    if (!promptPane || !promptPane->isVisible() || promptPane->getWidth() < 100
        || promptPane->getRight() + reserved + 5 > topRuler->headerWidth
        || promptPane->getParentComponent() != topRuler->getParentComponent()) return 141;
    const auto columns = tempo->timelineColumns();
    if (tempo->getX() + columns.getStart() != rulerArea.getX() + topRuler->headerWidth
        || tempo->getX() + columns.getEnd() != rulerArea.getRight()) return 82;
    const juce::Point<float> rampStart{float(columns.getStart() + columns.getLength()/8), 100.f},
                            rampEnd{float(columns.getStart() + columns.getLength()/2), 75.f};
    tempo->mouseDown(gesture(tempo, rampStart, rampStart));
    tempo->mouseDrag(gesture(tempo, rampStart, rampEnd));
    tempo->mouseUp(gesture(tempo, rampStart, rampEnd));
    if (!main.getName().contains(TRANS("Unsaved changes"))) return 77;
    if (!main.keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0))
        || main.getName().contains(TRANS("Unsaved changes"))) return 78;
    const auto trackPoint = main.getLocalPoint(guitar, juce::Point<int>{12, -8}).toFloat();
    main.mouseDown(gesture(&main, trackPoint, trackPoint));
    if (tempo->isVisible() || !tracks->isVisible()) return 79;
    if (!reachable(*guitar)) return 3;
    const int bassY = bass->getY();
    const int fitHeight = bassY - guitar->getY() - bass->height();
    if (fitHeight != 0 && fitHeight != 16 && fitHeight != 32) return 72;
    const auto bassPosition = [&](int h) { return bassY + h - 100; };
    guitar->mouseDown(pointer(*guitar, 2));
    guitar->mouseDrag(pointer(*guitar, 82));
    if (!guitar->expanded() || guitar->height() != 100 + 80
        || bass->getY() != bassPosition(180) || bass->height() != 100) return 4;
    // The guitar enters at bar 11, just past the default ten-bar viewport.
    auto* horizontal = dynamic_cast<juce::ScrollBar*>(main.findChildWithID("arrangement-horizontal-scroll"));
    if (!horizontal || std::abs(horizontal->getCurrentRange().getLength() - 10.0) > 0.1) return 71;
    horizontal->setCurrentRangeStart(11.0, juce::sendNotificationSync);
    const auto image = main.createComponentSnapshot(main.getLocalBounds());
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    bool hasNotePreview = false;
    for (int y = 0; y < image.getHeight() && !hasNotePreview; ++y)
        for (int x = 425; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y) == juce::Colour(0xffb3ebdc)) { hasNotePreview = true; break; }
    if (!hasNotePreview) return 5;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (!guitar->compact() || bass->getY() != bassPosition(TrackRowSize::minimum)) return 6;
    guitar->mouseDoubleClick(pointer(*guitar, 2));
    if (guitar->height() != 100 + 80) return 7;
    main.setSize(790, 570);
    if (!scroll->isVisible()) return 8;
    scroll->setCurrentRangeStart(scroll->getMaximumRangeLimit(), juce::sendNotificationSync);
    auto* lastReturn = findControl<TrackRowSize>(main, "Return 1 track height");
    auto* ruler = master->getParentComponent()->findChildWithID("arrangement-ruler");
    if (promptPane->isVisible()) return 141;
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
    if (drums->height() != 79 || bass->height() != 100) return 25;
    if (!choose(TRANS("Timeline lanes"))) return 26;
    scroll->setCurrentRangeStart(0, juce::sendNotificationSync);
    const int beforeLanes = bass->getY();
    height->setValue(62, juce::sendNotificationSync);
    if (bass->getY() <= beforeLanes || bass->height() != 100) return 27;
    if (ruler->getHeight() != 62 || group->getLookAndFeel().getPopupMenuFont().getHeight() < 15.f) return 62;
    auto* basics = dynamic_cast<SongBasicsPanel*>(main.findChildWithID("song-basics-strip"));
    if (!basics || !basics->isVisible() || !basics->advanced || !basics->commit ||
        basics->getHeight() != SongBasicsPanel::stripHeight || basics->getBottom() > scroll->getY()) return 63;
    basics->commit(SongBasicsPanel::title, "Edited title", 0);
    if (!main.getName().contains("Edited title") || !main.getName().contains(TRANS("Unsaved changes"))) return 65;
    basics->commit(SongBasicsPanel::key, {}, 10 + 8 * 3 + 1);
    auto* keys = dynamic_cast<juce::ComboBox*>(basics->findChildWithID("song-base-key"));
    if (!keys || keys->getText() != "Eb minor") return 66;
    if (!choose(TRANS("All tracks"))) return 28;
    height->setValue(35, juce::sendNotificationSync);
    if (!guitar->compact() || !drums->compact() || !master->compact() || !bass->compact()) return 29;

    // Measure the space between two neighbouring pitched tracks with both fit rows visible.
    const auto toggled = enableFitLanes();
    if (!choose(TRANS("Arrangement tracks"))) return 73;
    for (int h : {100, 155, 42}) {
        height->setValue(h, juce::sendNotificationSync);
        if (guitar->height() != h || bass->height() != h || bass->getY() - guitar->getY() - h != 32) return 74;
    }
    for (int i = toggled.size(); --i >= 0;) model.menuItemSelected(toggled[i], 1);

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
    // A dropped AIFF must become a self-contained WAV asset with the same samples.
    juce::TemporaryFile aiffFile(".aiff");
    std::unique_ptr<juce::OutputStream> stream = aiffFile.getFile().createOutputStream();
    juce::AiffAudioFormat aiff;
    auto writer = aiff.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(44100)
        .withNumChannels(1).withBitsPerSample(16));
    juce::AudioBuffer<float> source(1,128);
    source.clear();
    source.setSample(0,32,.5f);
    if (!writer || !writer->writeFromAudioSampleBuffer(source,0,128)) return 145;
    writer.reset();
    if (!click("Add audio track")) return 146;
    const auto position = audioEditor->getBounds().getCentre();
    main.filesDropped({aiffFile.getFile().getFullPathName()},position.x,position.y);
    const auto* decoded = audioEditor->sourceClip();
    if (!decoded || decoded->embedded.isEmpty() || !decoded->path.endsWith(".wav")) return 147;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::MemoryInputStream(decoded->embedded,false),true));
    if (!reader || reader->sampleRate != 44100 || reader->lengthInSamples != 128 || reader->numChannels != 1
        || !reader->read(&source,0,128,0,true,false) || std::abs(source.getSample(0,32)-.5f) > .0001f) return 148;
    return 0;
}
