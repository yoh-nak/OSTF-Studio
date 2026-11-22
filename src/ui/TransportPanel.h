#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TransportKey final : public juce::Button {
public:
    enum Action { play, stop, record };
    TransportKey(Action action, const juce::String& caption) : Button(caption), action_(action) {
        setTooltip(caption);
        setTitle(caption);
    }
private:
    Action action_;
    void paintButton(juce::Graphics& g, bool hover, bool down) override {
        auto key = getLocalBounds().toFloat().reduced(2);
        const bool on = getToggleState();
        const juce::Colour ink(action_ == record ? 0xfffa7469 : action_ == play ? 0xff78e4bd : 0xffdae4e7);
        g.setColour(juce::Colour(0xff101419));
        g.fillRoundedRectangle(key.translated(0, 2), 4);
        if (down) key.translate(0, 1);
        const auto face = juce::Colour(on ? 0xff364953 : hover ? 0xff444f58 : 0xff343c43);
        g.setGradientFill({face.brighter(0.18f), key.getTopLeft(), face.darker(0.28f), key.getBottomLeft(), false});
        g.fillRoundedRectangle(key, 4);
        g.setColour(on || hasKeyboardFocus(false) ? ink : juce::Colour(0xff626b72));
        g.drawRoundedRectangle(key.reduced(0.5f), 4, 1);
        auto symbol = juce::Rectangle<float>(13, 13).withCentre({key.getCentreX(), key.getCentreY() - 6});
        g.setColour(ink);
        if (action_ == record) g.fillEllipse(symbol);
        else if (action_ == stop) g.fillRoundedRectangle(symbol.reduced(1), 1.5f);
        else {
            juce::Path arrow;
            arrow.addTriangle(symbol.getX(), symbol.getY(), symbol.getRight(), symbol.getCentreY(), symbol.getX(), symbol.getBottom());
            g.fillPath(arrow);
        }
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawFittedText(getName(), key.toNearestInt().removeFromBottom(19), juce::Justification::centred, 1);
    }
};

class TransportPanel final : public juce::Component {
public:
    TransportKey play{TransportKey::play, TRANS("Play")}, stop{TransportKey::stop, TRANS("Stop")},
                 record{TransportKey::record, TRANS("Record MIDI")};
    TransportPanel() {
        for (auto* b : {&play, &stop, &record}) addAndMakeVisible(*b);
        constexpr const char* ids[] = {"bar-beat-tick", "timecode", "tempo", "meter", "key", "scale", "chord"};
        for (int i = 0; i < 7; ++i) {
            auto& readout = readouts_[i];
            readout.setComponentID(ids[i]);
            readout.setTitle(captions_[i]);
            readout.setBorderSize({});
            auto font = juce::FontOptions(i < 2 ? 20.0f : 16.0f, juce::Font::bold);
            if (i < 2) font = font.withName(juce::Font::getDefaultMonospacedFontName());
            readout.setFont(font);
            readout.setColour(juce::Label::textColourId, juce::Colour(i == 0 || i == 6 ? 0xffffca81 : 0xff98e4cd));
            readout.setMinimumHorizontalScale(0.75f);
            addAndMakeVisible(readout);
        }
        stop.setToggleState(true, juce::dontSendNotification);
        setComponentID("transport");
    }
    static int panelHeight(int width) { return width < 940 ? 101 : 65; }
    void setPosition(const ostfstudio::OstfpPreview& song, juce::int64 tick, double seconds) {
        const auto withinBar = tick % song.ticksPerBar;
        const double beatLength = song.ppq * 4.0 / song.beatUnit;
        const auto beat = static_cast<int>(std::floor(withinBar / beatLength));
        const auto sub = static_cast<juce::int64>(std::floor(withinBar - beat * beatLength));
        auto set = [&](int index, juce::String text) {
            if (text.isEmpty()) text = juce::String::charToString(0x2014);
            if (readouts_[index].getText() != text) {
                readouts_[index].setText(text, juce::dontSendNotification);
                readouts_[index].setTooltip(captions_[index] + "  " + text);
            }
        };
        set(0, padded(1 + tick / song.ticksPerBar, 3) + "." + padded(beat + 1, 2) + "." + padded(sub, 3));
        if (seconds < 0) seconds = song.clock.secondsAt(tick);
        const auto frames = static_cast<juce::int64>(std::floor(seconds * 30.0 + 1e-8));
        set(1, padded(frames / 108000, 2) + ":" + padded(frames / 1800 % 60, 2) + ":"
               + padded(frames / 30 % 60, 2) + ":" + padded(frames % 30, 2));
        set(2, song.contextAt(ostfstudio::TimelineLane::tempo, tick));
        set(3, juce::String(song.meterBeats) + "/" + juce::String(song.beatUnit));
        set(4, song.contextAt(ostfstudio::TimelineLane::key, tick));
        set(5, song.contextAt(ostfstudio::TimelineLane::scale, tick));
        set(6, song.contextAt(ostfstudio::TimelineLane::chord, tick));
    }
    void resized() override {
        auto r = getLocalBounds().reduced(9, 7);
        const bool stacked = getHeight() > 70;
        auto top = stacked ? r.removeFromTop(46) : r;
        for (auto* b : {&play, &stop, &record}) b->setBounds(top.removeFromLeft(b == &record ? 70 : 49));
        top.removeFromLeft(9);
        const int clockWidth = stacked ? top.getWidth() / 2 : juce::jlimit(122, 158, getWidth() / 8);
        for (int i = 0; i < 2; ++i) fields_[i] = top.removeFromLeft(clockWidth).reduced(1, 0);
        auto rest = stacked ? r.withTrimmedTop(4) : top.withTrimmedLeft(6);
        const int weights[] = {16, 13, 23, 28, 20};
        const auto width = rest.getWidth();
        for (int i = 2; i < 7; ++i)
            fields_[i] = rest.removeFromLeft(i == 6 ? rest.getWidth() : width * weights[i - 2] / 100).reduced(1, 0);
        for (int i = 0; i < 7; ++i)
            readouts_[i].setBounds(fields_[i].reduced(7, 2).withTrimmedTop(14));
    }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setGradientFill({juce::Colour(0xff39434b), bounds.getTopLeft(), juce::Colour(0xff242b32), bounds.getBottomLeft(), false});
        g.fillRoundedRectangle(bounds, 6);
        g.setColour(juce::Colour(0xff4b565f));
        g.drawRoundedRectangle(bounds, 6, 1);
        for (int i = 0; i < 7; ++i) {
            auto rect = fields_[i];
            g.setColour(juce::Colour(0xff111c20));
            g.fillRoundedRectangle(rect.toFloat(), 3);
            rect.reduce(7, 2);
            auto caption = rect.removeFromTop(14);
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.setColour(juce::Colour(0xff9fb2b4));
            g.drawFittedText(captions_[i], caption, juce::Justification::centredLeft, 1);
        }
    }
private:
    static juce::String padded(juce::int64 n, int width) { return juce::String(n).paddedLeft('0', width); }
    const std::array<juce::String, 7> captions_{TRANS("Bar / Beat / Tick"), TRANS("Timecode / 30 fps"),
        TRANS("Tempo (BPM)"), TRANS("Meter"), TRANS("Key"), TRANS("Scale"), TRANS("Chord")};
    std::array<juce::Label, 7> readouts_;
    std::array<juce::Rectangle<int>, 7> fields_;
};
