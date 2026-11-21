#pragma once
#include <JuceHeader.h>

class MainComponent final : public juce::Component {
public:
    MainComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    void inspectOSTF();
    void layoutDemo(juce::Graphics& g, juce::Rectangle<int> area);
    juce::TextButton openButton_ {"Inspect .ostf JSON"};
    juce::TextButton aboutButton_ {"About"};
    juce::Label statusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
