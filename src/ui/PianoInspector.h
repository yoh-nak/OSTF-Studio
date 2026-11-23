#pragma once
#include "ui/PianoNoteGrid.h"
#include "ui/PianoHarmonyColours.h"

class PianoInspector final : public juce::Component {
public:
    explicit PianoInspector(PianoNoteGrid& grid) : grid_(grid) {
        setComponentID("piano-inspector");
        for (auto* button : {&quantize, &shape}) addAndMakeVisible(button);
        quantize.setButtonText(TRANS("Quantize selection"));
        shape.setButtonText(TRANS("Shape velocity"));
        quantize.setComponentID("quantize-selection");
        shape.setComponentID("shape-velocity");
        quantize.setTooltip(TRANS("Move selected note starts toward the nearest grid line. Durations are preserved."));
        shape.setTooltip(TRANS("Set a velocity ramp between the first and last selected onsets."));
        snap.addItem(TRANS("Relative snap"), 1);
        snap.addItem(TRANS("Absolute snap"), 2);
        snap.setSelectedId(1, juce::dontSendNotification);
        snap.setTooltip(TRANS("Hold Shift during a drag for free timing."));
        snap.onChange = [this, &grid] { grid.absoluteSnap = snap.getSelectedId() == 2; };
        addAndMakeVisible(snap);
        auto control = [this](juce::Slider& slider, juce::Label& label, juce::String title, int min, int max, int initial) {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setRange(min, max, 1);
            slider.setValue(initial);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
            slider.setTitle(title);
            label.setText(title, juce::dontSendNotification);
            addAndMakeVisible(label);
            addAndMakeVisible(slider);
        };
        control(strength, strengthLabel, TRANS("Strength"), 0, 100, 100);
        strength.setTextValueSuffix("%");
        control(start, startLabel, TRANS("Start velocity"), 1, 127, 65);
        control(end, endLabel, TRANS("End velocity"), 1, 127, 105);
        pencil.setButtonText(TRANS("Pencil"));
        pencil.setComponentID("piano-pencil");
        pencil.setTooltip(TRANS("Click to draw, drag for length, or Alt/Option-drag to repeat on the grid. Shift-drag selects notes."));
        pencil.onClick = [this] {
            grid_.pencil = pencil.getToggleState();
            if (grid_.isShowing()) grid_.grabKeyboardFocus();
        };
        grid.togglePencil = [this] { pencil.setToggleState(!pencil.getToggleState(), juce::sendNotificationSync); };
        triplets.setButtonText(TRANS("Split into triplets"));
        triplets.setComponentID("split-triplets");
        triplets.setTooltip(TRANS("Repeat each selected note three times within its original length."));
        triplets.onClick = [this] { grid_.splitIntoTriplets(); };
        direction.addItem(TRANS("Low to high"), 1);
        direction.addItem(TRANS("High to low"), 2);
        direction.addItem(TRANS("Alternate strokes"), 3);
        direction.setSelectedId(1, juce::dontSendNotification);
        direction.setTooltip(TRANS("Pitch order within each selected chord."));
        control(spread, spreadLabel, TRANS("Spread"), 1, 100, 12);
        spread.setTextValueSuffix("%");
        spread.setTooltip(TRANS("Time from first to last note, as a percentage of a quarter note."));
        strum.setButtonText(TRANS("Strum chords"));
        strum.setComponentID("strum-chords");
        strum.setTooltip(TRANS("Stagger selected notes that start together. Note ends stay in place."));
        strum.onClick = [this] {
            auto stroke = direction.getSelectedId() == 2 ? PianoNoteGrid::Stroke::highFirst
                        : direction.getSelectedId() == 3 ? PianoNoteGrid::Stroke::alternate : PianoNoteGrid::Stroke::lowFirst;
            grid_.strumSelection(stroke, int(spread.getValue()));
        };
        addAndMakeVisible(pencil);
        addAndMakeVisible(triplets);
        addAndMakeVisible(direction);
        addAndMakeVisible(strum);
        quantize.onClick = [this, &grid] { grid.quantizeSelection(int(strength.getValue())); };
        shape.onClick = [this, &grid] { grid.shapeVelocity(int(start.getValue()), int(end.getValue())); };
    }
    bool showLegend = false;
    pianoHarmony::Theme theme = pianoHarmony::Theme::studioBlue;
    static constexpr int controlsHeight = 174;
    void updateActions(bool hasGrid, int selected) {
        quantize.setEnabled(hasGrid && selected > 0);
        shape.setEnabled(selected > 0);
        triplets.setEnabled(selected > 0);
        strum.setEnabled(!grid_.percussion && selected > 1);
    }
    void paint(juce::Graphics& g) override {
        if (!showLegend) return;
        using namespace pianoHarmony;
        auto area = getLocalBounds().withTrimmedTop(controlsHeight).reduced(6, 0);
        g.setColour(juce::Colour(0xffe3e8ee));
        g.setFont(juce::FontOptions(13.f, juce::Font::bold));
        g.drawText(TRANS("Degree colours"), area.removeFromTop(19), juce::Justification::centredLeft);
        auto swatches = area.removeFromTop(21);
        for (int pc = 0; pc < 12; ++pc) {
            auto item = swatches.removeFromLeft(swatches.getWidth() / (12 - pc)).reduced(1, 0);
            g.setColour(degreeColour(pc, 0, theme)); g.fillRect(item);
            g.setColour(juce::Colour(0xff182634));
            g.drawFittedText(ostfstudio::TheoryGuide::interval(pc, 0), item, juce::Justification::centred, 1);
        }
        g.setColour(juce::Colour(0xffe3e8ee));
        g.drawText(TRANS("Note roles"), area.removeFromTop(21), juce::Justification::centredLeft);
        const Tone roles[] {Tone::root, Tone::guide, Tone::fifth, Tone::chord, Tone::tension, Tone::avoid, Tone::outside, Tone::unknown};
        for (int row = 0; row < 2; ++row) {
            auto line = area.removeFromTop(21);
            for (int col = 0; col < 4; ++col) {
                const auto role = roles[4 * row + col];
                auto item = line.removeFromLeft(line.getWidth() / (4 - col));
                g.setColour(toneColour(role)); g.fillRect(item.removeFromLeft(6).reduced(0, 4));
                g.setColour(juce::Colours::white);
                g.drawFittedText(toneName(role), item.reduced(3, 0), juce::Justification::centredLeft, 1);
            }
        }
    }
    void resized() override {
        auto area = getLocalBounds().reduced(4, 2);
        auto left = area.removeFromLeft(area.getWidth() / 2).withTrimmedRight(5);
        area.removeFromLeft(5);
        auto sliderRow = [](juce::Rectangle<int>& column, juce::Label& label, juce::Slider& slider) {
            auto row = column.removeFromTop(28);
            label.setBounds(row.removeFromLeft(82));
            slider.setBounds(row);
        };
        snap.setBounds(left.removeFromTop(28).reduced(1, 2));
        sliderRow(left, strengthLabel, strength);
        sliderRow(area, startLabel, start);
        sliderRow(area, endLabel, end);
        quantize.setBounds(left.removeFromTop(28).reduced(1, 2));
        shape.setBounds(area.removeFromTop(28).reduced(1, 2));
        pencil.setBounds(left.removeFromTop(28).reduced(1, 2));
        triplets.setBounds(area.removeFromTop(28).reduced(1, 2));
        direction.setBounds(left.removeFromTop(28).reduced(1, 2));
        strum.setBounds(area.removeFromTop(28).reduced(1, 2));
        auto spreadRow = left.getUnion(area);
        sliderRow(spreadRow, spreadLabel, spread);
    }
private:
    PianoNoteGrid& grid_;
    juce::ToggleButton pencil;
    juce::ComboBox direction;
    juce::Slider spread;
    juce::Label spreadLabel;
    juce::TextButton triplets, strum;
    juce::ComboBox snap;
    juce::Slider strength, start, end;
    juce::Label strengthLabel, startLabel, endLabel;
    juce::TextButton quantize, shape;
};

class EditorDivider final : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<void(int)> resizeAt;
    explicit EditorDivider(bool across = false) : across_(across) {
        setComponentID(across ? "piano-column-divider" : "piano-divider");
        setMouseCursor(across ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
        setTooltip(across ? TRANS("Drag to resize the inspector and track headers.") : TRANS("Drag to resize the arrangement and piano roll."));
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff81929e));
        g.fillRoundedRectangle(getLocalBounds().withSizeKeepingCentre(across_ ? 3 : 48, across_ ? 32 : 3).toFloat(), 1.5f);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const auto p = e.getEventRelativeTo(getParentComponent()).getPosition();
        if (resizeAt) resizeAt(across_ ? p.x : p.y);
    }
private:
    bool across_;
};
