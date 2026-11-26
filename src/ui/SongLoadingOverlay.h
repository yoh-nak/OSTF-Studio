#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class SongLoadingOverlay final : public juce::Component {
public:
    SongLoadingOverlay() {
        setTitle(TRANS("Loading..."));
        indicator.setPercentageDisplay(false);
        addAndMakeVisible(indicator);
    }
    void show(const juce::String& filename) {
        setName(filename);
        setVisible(true);
        toFront(false);
        if (!isCurrentlyModal()) enterModalState(false);
        repaint();
        if (auto* peer = getPeer(); peer && isShowing()) peer->performAnyPendingRepaintsNow();
    }
    void dismiss() {
        if (isCurrentlyModal()) exitModalState(0);
        setVisible(false);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black.withAlpha(.43f));
        const auto panel = card().toFloat();
        g.setColour(juce::Colour(0xff203138));
        g.fillRoundedRectangle(panel, 8);
        g.setColour(juce::Colour(0xff718e91));
        g.drawRoundedRectangle(panel.reduced(.5f), 8, 1);
        auto text = card().reduced(20, 12);
        g.setColour(juce::Colour(0xffb4efe2));
        g.setFont(juce::FontOptions(18.f, juce::Font::bold));
        g.drawText(TRANS("Loading..."), text.removeFromTop(26), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffedf2f2));
        g.setFont(juce::FontOptions(14.f));
        g.drawText(getName(), text.removeFromTop(24), juce::Justification::centredLeft);
    }
    void resized() override { indicator.setBounds(card().reduced(20, 15).removeFromBottom(8)); }
private:
    juce::Rectangle<int> card() const {
        return getLocalBounds().withSizeKeepingCentre(std::min(360, getWidth()), 96);
    }
    double progress = -1.;
    juce::ProgressBar indicator{progress};
};
