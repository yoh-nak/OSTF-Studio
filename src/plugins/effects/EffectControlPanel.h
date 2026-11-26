#pragma once
#include "../StudioPluginEditor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace ostfstudio {
class EffectControlPanel final : public ostfstudio::StudioPluginEditor {
  public:
    EffectControlPanel(juce::AudioProcessor& processor, const std::vector<juce::AudioParameterFloat*>& params, bool graphic)
        : StudioPluginEditor(processor) {
        for (auto* p : params) {
            auto* slider = sliders.add(new juce::Slider);
            slider->setName(p->getName(64));
            slider->setTitle(slider->getName());
            slider->setSliderStyle(graphic ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 20);
            addAndMakeVisible(slider);
            auto* label = labels.add(new juce::Label({}, slider->getName()));
            label->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(label);
            attachments.push_back(std::make_unique<juce::SliderParameterAttachment>(*p, *slider));
        }
        columns = graphic ? 10 : (params.size() >= 12 ? 6 : std::min(5, sliders.size()));
        setSize(30 + columns * 110, 58 + ((sliders.size() + columns - 1) / columns) * (graphic ? 185 : 112));
    }
    void resized() override {
        auto area = getLocalBounds().reduced(12, 8).withTrimmedTop(40);
        const int rows = (sliders.size() + columns - 1) / columns;
        for (int i = 0; i < sliders.size(); ++i) {
            auto cell = juce::Rectangle<int>(area.getX() + area.getWidth() * (i % columns) / columns,
                                             area.getY() + area.getHeight() * (i / columns) / rows,
                                             area.getWidth() / columns, area.getHeight() / rows)
                            .reduced(3);
            labels[i]->setBounds(cell.removeFromTop(22));
            sliders[i]->setBounds(cell);
        }
    }
    void paint(juce::Graphics& g) override {
        StudioPluginEditor::paint(g);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(21.f, juce::Font::bold));
        g.drawText(getAudioProcessor()->getName(), 18, 5, getWidth() - 36, 34, juce::Justification::centredLeft);
    }

  private:
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;
    int columns = 1;
};
} // namespace ostfstudio
