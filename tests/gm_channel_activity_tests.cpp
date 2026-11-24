#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/ui/InstrumentProgramWatch.h"
#include "ui/PreviewTransport.h"
#include <bitset>
#include <iostream>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
std::bitset<16> litChannels(juce::AudioProcessorEditor& editor) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    std::bitset<16> lit;
    for (int ch = 0; ch < 16; ++ch) {
        const int x = 16 + (2 * ch + 1) * (image.getWidth() - 32) / 32;
        for (int y = 0; y < image.getHeight(); ++y) {
            if (pixels.getPixelColour(x, y) == juce::Colour(0xff69dfb4)) {
                lit.set(ch);
                break;
            }
        }
    }
    return lit;
}

int checkDemoMeters(const juce::File& file) {
    juce::FileInputStream input(file);
    std::unique_ptr<ostfstudio::OstfpPreview> score;
    if (ostfstudio::readOstfpPreview(input, score).failed() || !score->savedMix) return 10;
    std::vector<PreviewTransport::TrackSetup> routes(score->trackLanes.size());
    std::vector<std::unique_ptr<juce::AudioProcessor>> instruments;
    std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;
    const auto& mix = *score->savedMix;
    PreviewTransport transport;
    for (size_t i = 0; i < score->sourceTrackCount(); ++i) {
        routes[i].outputTrack = mix.midiRoutes[i].destination;
        routes[i].midiChannel = mix.midiRoutes[i].channel;
        if (mix.instruments[i].isObject()) {
            if (mix.instruments[i]["name"].toString() != "OSTF GM Sound Module") return 11;
            instruments.emplace_back(createPluginFilter());
            routes[i].instrument = instruments.back().get();
            editors.emplace_back(instruments.back()->createEditorIfNeeded());
            if (!editors.back()) return 12;
        }
        transport.setChannelMix(i, mix.tracks[i], true, 0);
    }
    if (editors.size() != 2) return 13;
    transport.setOutputMix(mix.routing);
    if (transport.preparePlayback(*score, routes, {}, 44100, 512).failed()) return 14;
    // The first chorus (bar 17) includes all eight parts.
    transport.seekToTick(16LL * 4 * score->ppq);
    std::array<std::bitset<16>, 2> expected, seen;
    for (int ch : {1, 2, 5, 7, 8}) expected[0].set(ch - 1);
    for (int ch : {3, 6, 10}) expected[1].set(ch - 1);
    juce::AudioBuffer<float> block(2, 512);
    bool simultaneous = false;
    for (int frame = 0; frame < 1600; ++frame) {
        transport.renderBlock(block.getArrayOfWritePointers(), 2, 512);
        if (frame % 12 != 0) continue;
        std::array<std::bitset<16>, 2> lit;
        for (size_t i = 0; i < editors.size(); ++i) {
            lit[i] = litChannels(*editors[i]);
            if ((lit[i] & ~expected[i]).any()) return 15;
            seen[i] |= lit[i];
        }
        simultaneous |= lit[0].count() >= 2 && lit[1].count() >= 2;
        if (block.getMagnitude(0, 512) > 0.001f && simultaneous && seen == expected) return 0;
    }
    std::cerr << "GM meter masks (Ch 16..1): " << seen[0] << " / " << seen[1]
              << ", simultaneous: " << simultaneous << '\n';
    return 16;
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 2) return 9;
    if (auto result = checkDemoMeters(juce::File(argv[1]))) return result;
    std::unique_ptr<juce::AudioProcessor> gm(createPluginFilter());
    gm->setRateAndBufferSizeDetails(48000,256);
    gm->prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(gm->createEditorIfNeeded());
    auto shows = [&](juce::String text) {
        for (auto* c : editor->getChildren())
            if (auto* l = dynamic_cast<juce::Label*>(c); l && l->getText().contains(text)) return true;
        return false;
    };
    std::vector<std::pair<int,int>> changes;
    ostfstudio::InstrumentProgramWatch watcher(*gm, [] { return true; }, [] { return true; },
        [&](int ch, int program) { changes.emplace_back(ch, program); });
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::programChange(2, 17), 0);
    juce::AudioBuffer<float> block(2,256);
    for (int ch : {2,4}) events.addEvent(juce::MidiMessage::noteOn(ch, 60 + ch, juce::uint8(100)), 0);
    gm->processBlock(block, events);
    events.clear();
    juce::Thread::sleep(400);
    gm->processBlock(block, events);
    juce::Timer::callPendingTimersSynchronously();
    if (!shows("EDIT CH 1") || !shows("Playing Ch: 2, 4")) return 1;
    if (litChannels(*editor) != std::bitset<16>(0b1010)) return 6;
    if (!changes.empty()) return 3;
    juce::ComboBox* picker = nullptr;
    for (auto* child : editor->getChildren()) if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) picker = combo;
    if (!picker) return 4;
    picker->setSelectedId(36, juce::sendNotificationSync);
    juce::Thread::sleep(100);
    juce::Timer::callPendingTimersSynchronously();
    if (changes != std::vector<std::pair<int,int>>{{1,35}}) return 5;
    gm->reset();
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    return shows("Playing Ch: -") ? 0 : 2;
}
