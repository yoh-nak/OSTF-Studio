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
    return 0;
}
