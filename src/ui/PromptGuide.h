#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class PromptGuide final : public juce::Component {
public:
    PromptGuide() {
        setTitle(TRANS("Prompt guide"));
        addAndMakeVisible(heading_);
        heading_.setText(TRANS("Prompt guide"), juce::dontSendNotification);
        heading_.setFont(juce::FontOptions(14.f, juce::Font::bold));
        heading_.setColour(juce::Label::textColourId, juce::Colour(0xff8fe1db));
        addAndMakeVisible(examples_);
        examples_.setTitle(TRANS("Example prompts"));
        examples_.setMultiLine(true);
        examples_.setReadOnly(true);
        examples_.setCaretVisible(false);
        examples_.setFont(juce::FontOptions(14.f));
        examples_.setIndents(0, 0);
        examples_.setBorder({});
        for (auto colour : {juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId,
                            juce::TextEditor::focusedOutlineColourId, juce::TextEditor::shadowColourId})
            examples_.setColour(colour, juce::Colours::transparentBlack);
        heading_.setInterceptsMouseClicks(false, false);
    }

    void setExamples(const juce::String& mode, const juce::String& first, const juce::String& second) {
        heading_.setText(TRANS("Prompt guide") + "  /  " + mode, juce::dontSendNotification);
        examples_.setText(first + "\n\n" + second, false);
        examples_.setCaretPosition(0);
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff202a31));
        g.setColour(juce::Colour(0xff465760));
        g.drawRect(area, 1.f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(9, 4);
        heading_.setBounds(area.removeFromTop(22));
        examples_.setBounds(area.reduced(5, 2));
    }

private:
    juce::Label heading_;
    juce::TextEditor examples_;
};
