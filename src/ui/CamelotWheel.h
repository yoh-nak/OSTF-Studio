#pragma once
#include "ui/SongMusic.h"
#include <juce_gui_basics/juce_gui_basics.h>

class CamelotWheel final : public juce::Component {
public:
    explicit CamelotWheel(int selected) : selection(selected) {
        setTitle(TRANS("Camelot wheel"));
        setComponentID("camelot-wheel");
        setSize(336, 336);
    }
    void select(int code) { selection = code; repaint(); }
    static juce::Colour colour(int code) {
        const int number = (code - 1) % 12 + 1;
        return juce::Colour::fromHSV(std::fmod(.71f + number / 12.f, 1.f), code <= 12 ? .53f : .35f, .95f, 1.f);
    }
    static juce::String label(int code) {
        return juce::String((code - 1) % 12 + 1) + (code <= 12 ? "A" : "B");
    }
    void paint(juce::Graphics& g) override {
        const auto centre = getLocalBounds().toFloat().getCentre();
        constexpr float step = juce::MathConstants<float>::twoPi / 12.f;
        for (int code = 1; code <= 24; ++code) {
            const bool minor = code <= 12;
            const int number = (code - 1) % 12 + 1;
            const float angle = number * step;
            const float inner = minor ? 61.f : 112.f, outer = minor ? 111.f : 163.f;
            juce::Path sector;
            sector.addPieSegment(centre.x - outer, centre.y - outer, 2 * outer, 2 * outer,
                                 angle - step / 2 + .004f, angle + step / 2 - .004f, inner / outer);
            g.setColour(colour(code)); g.fillPath(sector);
            if (code == selection) {
                g.setColour(juce::Colour(0xff15232d)); g.strokePath(sector, juce::PathStrokeType(4.f));
                g.setColour(juce::Colours::white); g.strokePath(sector, juce::PathStrokeType(1.5f));
            }
            const auto at = centre.getPointOnCircumference((inner + outer) * .5f, angle);
            const auto pc = ((number - 8) * 7 + (minor ? 9 : 0) + 120) % 12;
            const auto key = ostfstudio::chromaticNames()[pc] + (minor ? "m" : "");
            const juce::Rectangle<int> box(int(at.x) - 21, int(at.y) - 15, 42, 30);
            g.setColour(juce::Colour(0xff132029));
            g.setFont(juce::FontOptions(13.f, juce::Font::bold));
            g.drawText(label(code), box.withHeight(15), juce::Justification::centred);
            g.setFont(12.f);
            g.drawText(key, box.withTrimmedTop(15), juce::Justification::centred);
        }
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(17.f,juce::Font::bold));
        auto middle = getLocalBounds().withSizeKeepingCentre(112, 66);
        g.drawText(selection ? label(selection) : "Camelot", middle.removeFromTop(27), juce::Justification::centred);
        g.setFont(12.f);
        g.drawText(TRANS("A  Minor"), middle.removeFromTop(18), juce::Justification::centred);
        g.drawText(TRANS("B  Major"), middle, juce::Justification::centred);
    }
private:
    int selection;
};
