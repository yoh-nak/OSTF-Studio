#include "AnalyzerSpectrum.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ostfstudio::analyzer;
    Spectrum spectrum;
    spectrum.measure(0, fftSize, [](int i) {
        return .5f * static_cast<float>(std::sin(2 * std::numbers::pi * 83 * i / fftSize));
    });
    const auto& bins = spectrum.bins[0];
    if (std::max_element(bins.begin(), bins.end()) - bins.begin() != 83 || std::abs(bins[83] - .5f) > .002f)
        return 1;
    spectrum.measure(0, fftSize - 1, [](int) { return 1.f; });
    if (*std::max_element(bins.begin(), bins.end()) != 0)
        return 2;

    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setRateAndBufferSizeDetails(48000, 256);
    processor->prepareToPlay(48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());
    if (!editor)
        return 3;
    auto update = [] {
        juce::Thread::sleep(55);
        juce::Timer::callPendingTimersSynchronously();
    };
    auto signal = [&](int blocks, int offset) {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < blocks; ++block) {
            for (int i = 0; i < 256; ++i) {
                const double t = (offset + block * 256 + i) / 48000.;
                const auto sine = [&](double hz) { return std::sin(2 * std::numbers::pi * hz * t); };
                const double fundamental =
                    .31 * sine(130.81) + .13 * sine(261.63) + .08 * sine(392) + .03 * sine(1046.5);
                buffer.setSample(0, i, static_cast<float>(fundamental + .09 * sine(659.26) + .025 * sine(3136)));
                buffer.setSample(1, i, static_cast<float>(fundamental + .12 * sine(523.25) + .03 * sine(2093)));
            }
            processor->processBlock(buffer, midi);
        }
    };
    signal(620, 0);
    update();
    signal(1, 620 * 256);
    update();
    auto snap = [&] { return editor->createComponentSnapshot(editor->getLocalBounds()); };
    auto img = snap();
    int cyan = 0, amber = 0;
    for (int y = 180; y < img.getHeight(); ++y)
        for (int x = 25; x < img.getWidth() - 20; ++x) {
            const auto c = img.getPixelAt(x, y);
            if (c.getGreen() > 175 && c.getRed() + 50 < c.getGreen())
                ++cyan;
            if (c.getRed() > 180 && c.getGreen() + 28 < c.getRed())
                ++amber;
        }
    if (cyan < 200 || amber < 150) {
        std::cerr << "Missing visible L/R traces\n";
        return 4;
    }
    const auto png = juce::SystemStats::getEnvironmentVariable("OSTF_ANALYZER_SMOKE_PNG", {});
    if (png.isNotEmpty()) {
        juce::FileOutputStream out{juce::File(png)};
        if (!out.openedOk())
            return 5;
        out.setPosition(0);
        out.truncate();
        if (!juce::PNGImageFormat().writeImageToStream(img, out))
            return 6;
    }
    juce::TextButton* freeze = nullptr;
    juce::TextButton* reset = nullptr;
    for (auto* child : editor->getChildren()) {
        if (auto* button = dynamic_cast<juce::TextButton*>(child)) {
            if (button->getButtonText() == "Freeze")
                freeze = button;
            if (button->getButtonText() == "Reset meters")
                reset = button;
        }
    }
    if (!freeze || !reset)
        return 7;
    const auto wave =
        editor->getLocalBounds().withTrimmedTop(editor->getHeight() * 2 / 3).withWidth(editor->getWidth() / 2);
    auto sameWave = [&](const juce::Image& a, const juce::Image& b) {
        for (int y = wave.getY(); y < wave.getBottom(); ++y)
            for (int x = wave.getX(); x < wave.getRight(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                    return false;
        return true;
    };
    freeze->setToggleState(true, juce::sendNotificationSync);
    juce::AudioBuffer<float> silence(2, 256);
    juce::MidiBuffer midi;
    silence.clear();
    for (int block = 0; block < 40; ++block)
        processor->processBlock(silence, midi);
    update();
    if (!sameWave(img, snap()))
        return 8;
    freeze->setToggleState(false, juce::sendNotificationSync);
    update();
    processor->processBlock(silence, midi);
    update();
    if (sameWave(img, snap()))
        return 9;
    reset->onClick();
    processor->processBlock(silence, midi);
    update();
    editor.reset();
    processor->releaseResources();
    return 0;
}
