#include "../src/plugins/instruments/SingingScore.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct Head final : juce::AudioPlayHead {
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
};

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ostfstudio::singing;
    if (argc != 2) return 1;
    const auto source = juce::File(juce::String::fromUTF8(argv[1])).loadFileAsString();
    const auto parts = readParts(juce::JSON::parse(source));
    if (parts.empty() || parts.front().syllables.empty()) return 2;
    const auto kana = pronounce(juce::String::charToString(0x30ab) + juce::String::charToString(0x30bc));
    if (kana.size() != 2 || kana[0].vowel != 0 || kana[1].vowel != 3 || kana[1].onset != 1) return 3;
    std::unique_ptr<juce::AudioProcessor> singer(createPluginFilter());
    singer->setRateAndBufferSizeDetails(48000, 2048);
    singer->prepareToPlay(48000, 2048);
    juce::AudioBuffer<float> audio(2, 2048);
    juce::MidiBuffer midi;
    auto render = [&](int vowel) {
        singer->reset(); midi.clear();
        midi.addEvent(phoneMessage(1,60,{vowel,0}),0);
        midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(105)),0);
        singer->processBlock(audio,midi);
        return juce::AudioBuffer<float>(audio);
    };
    const auto a = render(0), i = render(1);
    double difference = 0;
    for (int n = 0; n < audio.getNumSamples(); ++n) difference += std::abs(a.getSample(0,n) - i.getSample(0,n));
    if (!std::isfinite(difference) || difference < 1) return 4;
    Head head;
    singer->setPlayHead(&head);
    juce::XmlElement state("OSTFSinger");
    state.setAttribute("part",0); state.addTextElement(source);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(state,blob);
    singer->setStateInformation(blob.getData(),int(blob.getSize()));
    blob.reset(); singer->getStateInformation(blob);
    std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());
    restored->setRateAndBufferSizeDetails(48000,2048);
    restored->prepareToPlay(48000,2048); restored->setPlayHead(&head);
    restored->setStateInformation(blob.getData(),int(blob.getSize()));
    head.position.setBpm(100); head.position.setPpqPosition(parts.front().syllables.front().start);
    head.position.setIsPlaying(true); restored->processBlock(audio,midi);
    if (!(audio.getMagnitude(0,0,2048) > .01f)) return 5;
    head.position.setIsPlaying(false); restored->processBlock(audio,midi);
    if (audio.getMagnitude(0,0,2048) != 0) return 6;
    head.position.setIsPlaying(true); head.position.setPpqPosition(-2); restored->processBlock(audio,midi);
    return audio.getMagnitude(0,0,2048) == 0 ? 0 : 7;
}
