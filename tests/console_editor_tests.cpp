#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/plugins/effects/CompressorListen.h"
#include "../src/plugins/StudioPluginEditor.h"
#include <cmath>
#include <iostream>

juce::AudioProcessor* makeStudioEffect(const char*, const char*);

static double luminance(juce::Colour colour) {
    const auto linear = [](double channel) {
        return channel <= .04045 ? channel / 12.92 : std::pow((channel + .055) / 1.055, 2.4);
    };
    return .2126 * linear(colour.getFloatRed()) + .7152 * linear(colour.getFloatGreen())
         + .0722 * linear(colour.getFloatBlue());
}

int main(int argc, char** argv) {
    ostfstudio::CompressorListen meter;
    for (float magnitude : {0.f, .1f, .5f}) {
        meter.state.store(meter.requested);
        meter.beginBlock(1000);
        for (int i = 0; i < 3000; ++i) meter.measure(magnitude);
        if (magnitude == 0 ? meter.state.load() != meter.noSignal :
            meter.state.load() != meter.ready || std::abs(meter.threshold(4) - (magnitude == .1f ? -24.f : -10.f)) > .01f) return 8;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ostfstudio::console;
    if (themeForKey("unknown")!=Theme::studioBlue) return 9;
    for (const auto& finish : finishes) {
        if (&finishFor(themeForKey(finish.key))!=&finish) return 9;
        LookAndFeel style(finish);
        const auto contrast = [&](int foreground, int background) {
            auto a=luminance(style.findColour(foreground)), b=luminance(style.findColour(background));
            return (std::max(a,b)+.05)/(std::min(a,b)+.05);
        };
        if (contrast(juce::ComboBox::textColourId,juce::ComboBox::backgroundColourId)<4.5
            || contrast(juce::PopupMenu::textColourId,juce::PopupMenu::backgroundColourId)<4.5
            || contrast(juce::PopupMenu::highlightedTextColourId,juce::PopupMenu::highlightedBackgroundColourId)<4.5) return 10;
    }
    const juce::PropertiesFile settings(ostfstudio::viewSettingsOptions());
    const auto& finish=finishFor(themeForKey(settings.getValue("workspaceTheme")));
    for (const auto* type : {"Reverb", "Chorus", "EQ", "Compressor", "Limiter", "Phaser", "Flanger", "Distortion", "Amp"}) {
        const auto title = juce::String("OSTF ") + type;
        std::unique_ptr<juce::AudioProcessor> processor(makeStudioEffect(type, title.toRawUTF8()));
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());
        if (!editor) return 1;
        const auto& style=editor->getLookAndFeel();
        if (style.findColour(LookAndFeel::accentColourId)!=juce::Colour(finish.accent)
            || style.findColour(juce::TextButton::buttonColourId)!=juce::Colour(finish.panel)) return 9;
        int index = 0;
        for (auto* child : editor->getChildren()) {
            auto* slider = dynamic_cast<juce::Slider*>(child);
            if (!slider) continue;
            auto* param = dynamic_cast<juce::RangedAudioParameter*>(processor->getParameters()[index++]);
            if (!param || slider->getName() != param->getName(64) || !slider->isTextBoxEditable()) return 2;
            const bool fader = slider->getName() == "Wet %" || slider->getName() == "Ceiling dB" || slider->getName() == "Threshold dB";
            if (slider->getSliderStyle() != (fader ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag)) return 3;
            param->setValueNotifyingHost(.33f);
            if (std::abs(slider->getValue() - param->convertFrom0to1(.33f)) > .01f) return 4;
            const auto target = param->convertFrom0to1(.72f);
            slider->setValue(target, juce::sendNotificationSync);
            if (std::abs(param->getValue() - param->convertTo0to1(target)) > .00001f
                || !slider->isDoubleClickReturnEnabled()
                || std::abs(slider->getDoubleClickReturnValue() - param->convertFrom0to1(param->getDefaultValue())) > .0001f) {
                std::cerr << title << " / " << slider->getName() << ": parameter or reset value mismatch\n";
                return 5;
            }
            param->setValueNotifyingHost(param->getDefaultValue());
        }
        if (index != 3) return 6;
        if (argc == 2 && juce::String(type) == "Chorus") {
            juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
            if (!stream.openedOk() || !stream.setPosition(0) || stream.truncate().failed()
                || !juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.5f), stream))
                return 7;
        }
    }
    std::cout << "All nine rack editors passed parameter and control checks.\n";
}
