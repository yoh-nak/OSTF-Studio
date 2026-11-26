#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/ui/StudioPaths.h"
#include <iostream>

namespace {
class PlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setTimeInSamples(sample);
        info.setTimeInSeconds(double(sample) / 48000.);
        info.setIsPlaying(playing);
        return info;
    }
    juce::int64 sample = 0;
    bool playing = true;
};

class PlaybackCheck final : private juce::Timer {
public:
    PlaybackCheck(juce::AudioPluginInstance& p, PlayHead& h) : plugin(p), head(h) {}
    bool run() {
        startTimer(10);
        juce::MessageManager::getInstance()->runDispatchLoop();
        return completed;
    }
private:
    void timerCallback() override {
        audio.clear();
        plugin.processBlock(audio, midi);
        const auto peak = audio.getMagnitude(0, audio.getNumSamples());
        if (head.playing) {
            head.sample += audio.getNumSamples();
            if (peak > .0001f) { sounded = true; head.playing = false; }
        } else if (++afterStop > 12 && peak == 0.f) {
            completed = sounded;
        }
        if (completed || ++blocks >= 1800) {
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }
    juce::AudioPluginInstance& plugin;
    PlayHead& head;
    juce::AudioBuffer<float> audio{2, 480};
    juce::MidiBuffer midi;
    int blocks = 0, afterStop = 0;
    bool sounded = false, completed = false;
};
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 3) return 1;
    const juce::File bundle(juce::String::fromUTF8(argv[1]));
    if (bundle.isDirectory()) {
        const auto binaries = bundle.findChildFiles(juce::File::findFiles, true,
            juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows ? "*.vst3" : "OSTF Studio");
        if (binaries.isEmpty() || studioBundleDirectory(binaries[0]) != bundle.getParentDirectory()) return 9;
    }
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, juce::String::fromUTF8(argv[1]));
    if (types.size() != 1 || !types[0]->isInstrument || types[0]->name != "OSTF Studio") return 2;
    juce::String error;
    auto first = format.createInstanceFromDescription(*types[0], 48000., 480, error);
    if (!first || !first->hasEditor()) return 3;
    juce::MemoryBlock song;
    if (!juce::File(juce::String::fromUTF8(argv[2])).loadFileAsData(song)) return 4;
    juce::XmlElement xml("VST3PluginState");
    xml.createNewChildElement("IComponent")->addTextElement(song.toBase64Encoding());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    first->setStateInformation(state.getData(), int(state.getSize()));
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(first->createEditorIfNeeded());
        if (!editor || editor->getWidth() < 700) return 5;
    }
    first->getStateInformation(state);
    if (state.getSize() < 1000) return 6;
    first.reset();
    auto recalled = format.createInstanceFromDescription(*types[0], 48000., 480, error);
    if (!recalled) return 7;
    PlayHead head;
    recalled->setPlayHead(&head);
    recalled->setStateInformation(state.getData(), int(state.getSize()));
    recalled->setRateAndBufferSizeDetails(48000., 480);
    recalled->prepareToPlay(48000., 480);
    PlaybackCheck playback(*recalled, head);
    const bool passed = playback.run();
    recalled->releaseResources();
    recalled->setPlayHead(nullptr);
    if (!passed) std::cerr << "Recalled Studio did not play and stop through the host stereo bus\n";
    return passed ? 0 : 8;
}
