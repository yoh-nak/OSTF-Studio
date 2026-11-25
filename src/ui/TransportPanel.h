#pragma once
#include "ConsoleDisplay.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TransportKey final : public juce::Button {
public:
    enum Action { home, rewind, play, stop, record, forward, follow, metronome, end };
    TransportKey(Action action, const juce::String& caption) : Button(caption), action_(action) {
        setTooltip(caption);
        setTitle(caption);
    }
private:
    Action action_;
    void paintButton(juce::Graphics& g, bool hover, bool down) override {
        auto key = getLocalBounds().toFloat().reduced(2);
        const bool on = getToggleState();
        const juce::Colour ink(action_ == record ? 0xfffa7469 : action_ == play || ((action_ == follow || action_ == metronome) && on) ? 0xff78e4bd : 0xffdae4e7);
        g.setColour(juce::Colour(0xff101419));
        g.fillRoundedRectangle(key.translated(0, 2), 4);
        if (down) key.translate(0, 1);
        const auto face = juce::Colour(on ? 0xff364953 : hover ? 0xff444f58 : 0xff343c43);
        g.setGradientFill({face.brighter(0.18f), key.getTopLeft(), face.darker(0.28f), key.getBottomLeft(), false});
        g.fillRoundedRectangle(key, 4);
        g.setColour(on || hasKeyboardFocus(false) ? ink : juce::Colour(0xff626b72));
        g.drawRoundedRectangle(key.reduced(0.5f), 4, 1);
        if (action_ == metronome) key = key.withWidth(key.getWidth() * .5f);
        auto symbol = juce::Rectangle<float>(13, 13).withCentre({key.getCentreX(), key.getCentreY() - 6});
        g.setColour(ink.withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
        if (action_ == record) g.fillEllipse(symbol);
        else if (action_ == stop) g.fillRoundedRectangle(symbol.reduced(1), 1.5f);
        else if (action_ == play) {
            juce::Path arrow;
            arrow.addTriangle(symbol.getX(), symbol.getY(), symbol.getRight(), symbol.getCentreY(), symbol.getX(), symbol.getBottom());
            g.fillPath(arrow);
        } else if (action_ == metronome) {
            const auto x = symbol.getX(), y = symbol.getY();
            juce::Path outline;
            outline.startNewSubPath(x + 4, y + 1); outline.lineTo(x + 8, y + 1);
            outline.lineTo(x + 12, y + 13); outline.lineTo(x + 1, y + 13); outline.closeSubPath();
            g.strokePath(outline, juce::PathStrokeType(1.5f));
            g.drawLine(x + 6, y + 10, x + 13, y + 2, 1.5f);
        } else if (action_ == follow) {
            const auto x = symbol.getX(), y = symbol.getY();
            juce::Path head;
            head.addTriangle(x, y, x + 8, y, x + 4, y + 4);
            g.fillPath(head);
            g.drawLine(x + 4, y + 3, x + 4, y + 13, 1.5f);
            g.drawArrow({x + 6, y + 7, x + 16, y + 7}, 1.5f, 7, 4);
        } else {
            const bool right = action_ == forward || action_ == end;
            const float mid = symbol.getCentreX(), y = symbol.getY(), h = symbol.getHeight();
            const auto triangle = [&](float left) {
                juce::Path p;
                const float base = right ? left : left + 7;
                p.addTriangle(base, y, right ? left + 7 : left, y + h / 2, base, y + h);
                g.fillPath(p);
            };
            if (action_ == home || action_ == end) {
                triangle(mid - 4);
                g.fillRect(action_ == home ? mid - 7 : mid + 5, y, 2.0f, h);
            } else {
                triangle(mid - 8);
                triangle(mid);
            }
        }
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawFittedText(getName(), key.toNearestInt().removeFromBottom(19), juce::Justification::centred, 1);
    }
};

class TransportPanel final : public juce::Component {
public:
    TransportKey home{TransportKey::home, TRANS("Home")}, rewind{TransportKey::rewind, TRANS("Rewind")},
                 forward{TransportKey::forward, TRANS("Forward")}, end{TransportKey::end, TRANS("Song end")}, play{TransportKey::play, TRANS("Play")},
                 stop{TransportKey::stop, TRANS("Stop")}, record{TransportKey::record, TRANS("Record")},
                 follow{TransportKey::follow, TRANS("Follow")};
    TransportKey click{TransportKey::metronome, TRANS("Metronome")};
    juce::ComboBox countIn;
    static constexpr int maxCountInBars = 8;
    TransportPanel() {
        addAndMakeVisible(click);
        click.setClickingTogglesState(true);
        click.addAndMakeVisible(countIn);
        countIn.setLookAndFeel(&countStyle_);
        countIn.setJustificationType(juce::Justification::centred);
        countIn.setComponentID("metronome-count-in");
        countIn.setTitle(TRANS("Count-in"));
        countIn.setTooltip(TRANS("Count-in before recording or live playback"));
        countIn.addItem(TRANS("Off"), 1);
        for (int n = 1; n <= maxCountInBars; ++n)
            countIn.addItem(juce::String(n) + " " + (n == 1 ? TRANS("bar") : TRANS("bars")), n + 1);
        for (auto* b : {&home, &rewind, &forward, &end, &play, &stop, &record, &follow}) addAndMakeVisible(*b);
        home.setComponentID("return-to-start");
        rewind.setComponentID("rewind");
        end.setComponentID("go-to-end");
        end.setTooltip(TRANS("Go to end of song"));
        forward.setComponentID("fast-forward");
        follow.setComponentID("follow-playhead");
        home.setTooltip(TRANS("Return to start of song"));
        rewind.setTooltip(TRANS("Back one bar. Hold to rewind."));
        forward.setTooltip(TRANS("Forward one bar. Hold to fast-forward."));
        for (auto* b : {&rewind, &forward}) {
            b->setTriggeredOnMouseDown(true);
            b->setRepeatSpeed(400, 160, 75);
        }
        follow.setTooltip(TRANS("Follow the playhead in the arrangement and piano roll"));
        follow.setClickingTogglesState(true);
        follow.setToggleState(true, juce::dontSendNotification);
        constexpr const char* ids[] = {"bar-beat-tick", "timecode", "tempo", "meter", "key", "scale", "chord", "section"};
        for (int i = 0; i < fieldCount; ++i) {
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
    static int panelHeight(int width) { return width < 1400 ? 101 : 65; }
    static juce::String musicalPosition(const ostfstudio::OstfpPreview& song, juce::int64 tick, ostfstudio::MeterPoint meter) {
        const auto withinBar = tick % song.ticksPerBar;
        const double beatLength = song.ppq * 4.0 / meter.denominator;
        const auto beat = static_cast<int>(std::floor(withinBar / beatLength));
        const auto sub = static_cast<juce::int64>(std::floor(withinBar - beat * beatLength));
        return padded(1 + tick / song.ticksPerBar, 3) + "." + padded(beat + 1, 2) + "." + padded(sub, 3);
    }
    void setPosition(const ostfstudio::OstfpPreview& song, juce::int64 tick, double seconds, int shift = 0, double rate = 1, double countInSeconds = 0.) {
        const auto meter = ostfstudio::activeMeter(song.meters, double(tick), {0, song.meterBeats, song.beatUnit});
        auto set = [&](int index, juce::String text) {
            if (text.isEmpty()) text = juce::String::charToString(0x2014);
            if (readouts_[index].getText() != text) {
                readouts_[index].setText(text, juce::dontSendNotification);
                readouts_[index].setTooltip(captions_[index] + "  " + text);
            }
        };
        set(0, musicalPosition(song,tick,meter));
        if (seconds < 0) seconds = song.clock.secondsAt(tick) / rate;
        const auto frames = static_cast<juce::int64>(countInSeconds>0 ? std::ceil(countInSeconds*30.) : std::floor(seconds * 30.0 + 1e-8));
        set(1, (countInSeconds>0 ? "-" : "") + padded(frames / 108000, 2) + ":" + padded(frames / 1800 % 60, 2) + ":"
               + padded(frames / 30 % 60, 2) + ":" + padded(frames % 30, 2));
        set(2, rate == 1 ? song.contextAt(ostfstudio::TimelineLane::tempo, tick) : juce::String(song.clock.bpmAt(tick) * rate, 2));
        set(3, juce::String(meter.numerator) + "/" + juce::String(meter.denominator));
        set(4, ostfstudio::shiftedLabel(song.contextAt(ostfstudio::TimelineLane::key, tick), shift));
        set(5, ostfstudio::shiftedLabel(song.contextAt(ostfstudio::TimelineLane::scale, tick), shift));
        set(6, ostfstudio::shiftedLabel(song.contextAt(ostfstudio::TimelineLane::chord, tick), shift));
        set(7, song.contextAt(ostfstudio::TimelineLane::section, tick));
    }
    ~TransportPanel() override { countIn.setLookAndFeel(nullptr); }
    void resized() override {
        auto r = getLocalBounds().reduced(9, 7);
        const bool stacked = getHeight() > 70;
        auto top = stacked ? r.removeFromTop(46) : r;
        for (auto* b : {&home, &rewind, &forward, &end, &play, &stop, &record, &follow})
            b->setBounds(top.removeFromLeft(getWidth() < 800 ? (b == &record ? 50 : 38) : (b == &record ? 74 : 48)));
        click.setBounds(top.removeFromLeft(126));
        countIn.setBounds(click.getLocalBounds().removeFromRight(63).reduced(1, 5));
        top.removeFromLeft(5);
        const int clockWidth = stacked ? top.getWidth() / 2 : juce::jlimit(122, 158, getWidth() / 8);
        for (int i = 0; i < 2; ++i) fields_[i] = top.removeFromLeft(clockWidth).reduced(1, 0);
        auto rest = stacked ? r.withTrimmedTop(4) : top.withTrimmedLeft(6);
        const int weights[] = {12, 9, 17, 21, 15, 26};
        const auto width = rest.getWidth();
        for (int i = 2; i < fieldCount; ++i)
            fields_[i] = rest.removeFromLeft(i == fieldCount - 1 ? rest.getWidth() : width * weights[i - 2] / 100).reduced(1, 0);
        for (int i = 0; i < fieldCount; ++i)
            readouts_[i].setBounds(fields_[i].reduced(7, 2).withTrimmedTop(14));
    }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setGradientFill({juce::Colour(0xff39434b), bounds.getTopLeft(), juce::Colour(0xff242b32), bounds.getBottomLeft(), false});
        g.fillRoundedRectangle(bounds, 6);
        g.setColour(juce::Colour(0xff4b565f));
        g.drawRoundedRectangle(bounds, 6, 1);
        g.setColour(juce::Colour(0xffc7d2dc));
        g.setFont(juce::FontOptions(11.f));
        for (int i = 0; i < fieldCount; ++i) {
            auto rect = fields_[i];
            ostfstudio::console::drawLcd(g,rect);
            rect.reduce(7, 2);
            auto caption = rect.removeFromTop(14);
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.setColour(juce::Colour(0xff9fb2b4));
            g.drawFittedText(captions_[i], caption, juce::Justification::centredLeft, 1);
        }
    }
private:
    static juce::String padded(juce::int64 n, int width) { return juce::String(n).paddedLeft('0', width); }
    static constexpr int fieldCount = 8;
    const std::array<juce::String, fieldCount> captions_{TRANS("Bar / Beat / Tick"), TRANS("Timecode / 30 fps"),
        TRANS("Tempo (BPM)"), TRANS("Meter"), TRANS("Key"), TRANS("Scale"), TRANS("Chord"), TRANS("Section")};
    std::array<juce::Label, fieldCount> readouts_;
    struct CountStyle final : juce::LookAndFeel_V4 {
        void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& combo) override {
            g.setColour(juce::Colour(0xff61757e));
            g.drawVerticalLine(0, 4.f, float(h - 4));
            g.setColour(combo.hasKeyboardFocus(true) ? juce::Colours::white : juce::Colour(0xffa9c9ce));
            juce::Path arrow;
            arrow.addTriangle(float(w - 12), h * .45f, float(w - 4), h * .45f, float(w - 8), h * .57f);
            g.fillPath(arrow);
        }
        void positionComboBoxText(juce::ComboBox&, juce::Label& label) override {
            auto bounds = label.getParentComponent()->getLocalBounds();
            label.setBounds(bounds.withTrimmedRight(13).withTrimmedLeft(2));
            label.setFont(juce::FontOptions(12.f));
        }
    } countStyle_;
    std::array<juce::Rectangle<int>, fieldCount> fields_;
};
