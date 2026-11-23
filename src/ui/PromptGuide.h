#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class PromptGuide final : public juce::Component, private juce::Timer {
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
        for (auto* button : {&previous_, &next_}) {
            addAndMakeVisible(*button);
            button->setTitle(button == &previous_ ? TRANS("Previous example") : TRANS("Next example"));
            button->setTooltip(button->getTitle());
        }
        previous_.onClick = [this] { advance(-1); };
        next_.onClick = [this] { advance(1); };
        heading_.setInterceptsMouseClicks(false, false);
    }

    void setExamples(const juce::String& mode, const juce::String& first, const juce::String& second, const juce::String& third) {
        heading_.setText(TRANS("Prompt guide") + "  /  " + mode, juce::dontSendNotification);
        prompts_ = juce::StringArray{first, second, third};
        index_ = 0;
        showCurrent();
    }

    void visibilityChanged() override {
        restartCountdown();
    }

    void parentHierarchyChanged() override {
        restartCountdown();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff202a31));
        g.setColour(juce::Colour(0xff465760));
        g.drawRect(area, 1.f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(9, 4);
        const bool compact = area.getWidth() < 190;
        auto heading = area.removeFromTop(22);
        auto navigation = compact ? area.removeFromBottom(20) : heading.removeFromRight(56);
        next_.setBounds(navigation.removeFromRight(26));
        previous_.setBounds(navigation.removeFromRight(26));
        heading_.setBounds(heading);
        examples_.setBounds(area.reduced(5, 2));
    }

private:
    void restartCountdown() {
        if (isVisible() && prompts_.size() > 1) startTimer(15000);
        else stopTimer();
    }
    void showCurrent() {
        examples_.setText(prompts_[index_], false);
        examples_.setCaretPosition(0);
        restartCountdown();
    }
    void advance(int direction) {
        if (prompts_.isEmpty()) return;
        index_ = (index_ + direction + prompts_.size()) % prompts_.size();
        showCurrent();
    }
    void timerCallback() override {
        if (!isShowing()) return;
        if (examples_.getHighlightedRegion().isEmpty()) advance(1);
    }
    juce::StringArray prompts_;
    int index_ = 0;
    juce::TextButton previous_ {juce::String::charToString(0x2039)}, next_ {juce::String::charToString(0x203a)};
    juce::Label heading_;
    juce::TextEditor examples_;
};
