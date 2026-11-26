#pragma once
#include "../ui/ConsoleLookAndFeel.h"
#include "../ui/StudioLanguage.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ostfstudio {
inline void setPluginControlColours(console::LookAndFeel& style, const console::Finish& face) {
    const juce::Colour panel(face.panel), background(face.background), accent(face.accent);
    style.setColour(juce::TextButton::buttonColourId, panel);
    style.setColour(juce::TextButton::buttonOnColourId, panel.interpolatedWith(accent,.36f));
    style.setColour(juce::ListBox::backgroundColourId, background);
    style.setColour(juce::TextEditor::backgroundColourId, background);
    style.setColour(juce::TextEditor::focusedOutlineColourId, accent);
}

class StudioPluginEditor : public juce::AudioProcessorEditor {
public:
    explicit StudioPluginEditor(juce::AudioProcessor& processor)
        : AudioProcessorEditor(processor), face(savedFinish()), style(face) {
        setPluginControlColours(style,face);
        setLookAndFeel(&style);
    }
    ~StudioPluginEditor() override { setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(face.background)); }

protected:
    const console::Finish& face;

private:
    static const console::Finish& savedFinish() {
        const auto options=viewSettingsOptions();
        if (!options.getDefaultFile().existsAsFile()) return console::finishFor(console::Theme::studioBlue);
        const juce::PropertiesFile settings(options);
        return console::finishFor(console::themeForKey(settings.getValue("workspaceTheme")));
    }
    console::LookAndFeel style;
};
}
