#pragma once
#include "ui/MixerPanel.h"

// The knob and its return name stay together when a long send row is scrolled.
class TrackSendKnobs : public juce::Viewport {
public:
    TrackSendKnobs() {
        setViewedComponent(&row, false);
        setScrollBarsShown(false, true);
        setScrollBarThickness(6);
        setScrollOnDragMode(ScrollOnDragMode::never);
    }
    ~TrackSendKnobs() override { setViewedComponent(nullptr, false); }

    void sync(MixerPanel& mixer, size_t track, const juce::String& trackName) {
        const auto count = mixer.returnCount();
        const bool countChanged = count != controls.size();
        controls.resize(count);
        for (size_t i = 0; i < count; ++i) {
            auto& control = controls[i];
            if (!control) {
                control = std::make_unique<Control>();
                row.addAndMakeVisible(*control);
                control->knob.onValueChange = [this, i, slider = &control->knob] {
                    controls[i]->value.setText(juce::String(juce::roundToInt(slider->getValue()))+"%",juce::dontSendNotification);
                    if (onChange) onChange(i, static_cast<float>(slider->getValue() / 100.0));
                };
            }
            const auto name = mixer.outputName(ostfstudio::returnOutput(i));
            const auto title = trackName + " / " + TRANS("Send: ") + name;
            control->label.setText(name, juce::dontSendNotification);
            if (control->knob.getTitle() != title) {
                control->knob.setTitle(title);
                control->knob.setTooltip(title);
            }
            control->knob.setValue(100.0 * mixer.channel(track).returnSends[i], juce::dontSendNotification);
            control->value.setText(juce::String(juce::roundToInt(control->knob.getValue())) + "%",juce::dontSendNotification);
        }
        if (countChanged) resized();
    }
    std::function<void(size_t, float)> onChange;

    void resized() override {
        juce::Viewport::resized();
        const int count = std::max(1, static_cast<int>(controls.size()));
        const bool twoRows = count > 4 && getWidth() < count * 57 && getHeight() >= 42;
        const int columns = twoRows ? (count + 1) / 2 : count;
        const int slotWidth = std::clamp(getWidth() / columns, 48, 100);
        const int width = slotWidth * columns;
        const int height = std::max(0, getHeight() - (width > getWidth() ? getScrollBarThickness() : 0));
        row.setSize(width, height);
        const int cellHeight = twoRows ? height / 2 : height;
        const int offset = twoRows ? 0 : std::clamp(height-25,0,8);
        for (size_t i = 0; i < controls.size(); ++i)
            controls[i]->setBounds(int(i % columns)*slotWidth,int(i / columns)*cellHeight + int(i%2)*offset,slotWidth,cellHeight-offset);
    }

private:
    struct Control : juce::Component {
        Control() {
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            knob.getProperties().set("consoleHardware", true);
            knob.setRange(0, 100, 1);
            knob.setTextValueSuffix("%");
            knob.setDoubleClickReturnValue(true, 0);
            knob.setPopupDisplayEnabled(true, true, nullptr);
            label.setFont(juce::FontOptions(11.f));
            label.setBorderSize({});
            value.setBorderSize({});
            label.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(knob);
            addAndMakeVisible(label);
            value.setFont(juce::FontOptions(11.f));
            value.setInterceptsMouseClicks(false,false);
            addAndMakeVisible(value);
        }
        void resized() override {
            auto area = getLocalBounds();
            knob.setBounds(area.removeFromLeft(std::min({25, area.getHeight(), area.getWidth() - 31})));
            const bool sideBySide = getWidth() >= 74 && area.getHeight() < 25;
            const bool tall = sideBySide || area.getHeight() >= 24;
            value.setVisible(tall);
            if (sideBySide) value.setBounds(area.removeFromRight(24));
            else if (tall) value.setBounds(area.removeFromBottom(area.getHeight()/2));
            label.setBounds(area);
        }
        juce::Slider knob;
        juce::Label label,value;
    };
    juce::Component row;
    std::vector<std::unique_ptr<Control>> controls;
};
