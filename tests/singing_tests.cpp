#include "../src/plugins/instruments/SingingScore.h"
#include "../src/plugins/instruments/VoiceBank.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <iostream>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct Head final : juce::AudioPlayHead {
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
};

static std::unique_ptr<juce::AudioFormatWriter> wavWriter(const juce::File& file, int rate) {
    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    if (!out) return {};
    return juce::WavAudioFormat{}.createWriterFor(out, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(1).withBitsPerSample(16));
}

static bool testRecordedVoice() {
    using namespace ostfstudio::singing;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    if (dir.createDirectory().failed()) return false;
    const auto wav = dir.getChildFile("a.wav"), oto = dir.getChildFile("oto.ini");
    struct Cleanup {
        juce::File wav, oto, folder;
        ~Cleanup() { wav.deleteFile(); oto.deleteFile(); folder.deleteFile(); }
    } cleanup{wav, oto, dir};
    juce::AudioBuffer<float> raw(1, 12000);
    for (int n = 0; n < raw.getNumSamples(); ++n)
        raw.setSample(0,n, float(.55 * std::sin(juce::MathConstants<double>::twoPi * 200 * n / 24000)));
    auto writer = wavWriter(wav, 24000);
    if (!writer || !writer->writeFromAudioSampleBuffer(raw, 0, raw.getNumSamples())) return false;
    writer.reset();
    juce::String error;
    auto bank = VoiceBank::load(dir, error);
    if (!bank || !bank->find({0,vowelOnly}) || std::abs(bank->find({0,vowelOnly})->period - 120) > 2) return false;
    // Shift-JIS 'ka' (82 A9). The alias, offset, consonant and negative cutoff all matter.
    const char line[] = "a.wav=\x82\xa9,20,60,-240,0,0\n";
    if (!oto.replaceWithData(line, sizeof(line) - 1)) return false;
    bank = VoiceBank::load(dir, error);
    if (!bank || bank->find({0,vowelOnly}) || !bank->find({0,velar}) || bank->find({0,velar})->frames.size() > 5760) return false;
    juce::XmlElement xml("OSTFSinger");
    xml.setAttribute("voiceFolder", dir.getFullPathName());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    std::unique_ptr<juce::AudioProcessor> singer(createPluginFilter());
    singer->setStateInformation(state.getData(), int(state.getSize()));
    singer->prepareToPlay(48000, 256);
    singer->getStateInformation(state);
    auto saved = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
    if (!saved || saved->getStringAttribute("voiceFolder") != dir.getFullPathName()) return false;
    juce::MidiBuffer events;
    events.addEvent(phoneMessage(1, 69, {0,velar}), 0);
    events.addEvent(juce::MidiMessage::noteOn(1, 69, juce::uint8(110)), 0);
    juce::AudioBuffer<float> block(2, 256);
    int crossings = 0;
    float prev = 0;
    double position = 0;
    const auto* reference = bank->find({0,velar});
    for (int b = 0; b < 200; ++b) {
        singer->processBlock(block, events);
        for (int i = 0; i < 256; ++i) {
            const float out = block.getSample(0, i);
            if (!std::isfinite(out)) return false;
            const float expected = reference->read(position, 440 * reference->period / 48000);
            if (b >= 3 && std::abs(out - std::tanh(expected * (110.f / 127))) > .002f) return false;
            if (b >= 50 && prev <= 0 && out > 0) ++crossings;
            prev = out;
        }
    }
    return std::abs(crossings - 352) <= 3; // 0.8 s of sustained A4 from the 200 Hz clip
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ostfstudio::singing;
    if (argc < 2 || argc > 4) return 1;
    const auto source = juce::File(juce::String::fromUTF8(argv[1])).loadFileAsString();
    const auto parts = readParts(juce::JSON::parse(source));
    if (parts.empty() || parts.front().syllables.empty()) return 2;
    const auto kana = pronounce(juce::String::charToString(0x30ab) + juce::String::charToString(0x30bc));
    if (kana.size() != 2 || kana[0].vowel != 0 || kana[1].vowel != 3 || kana[1].onset != z) return 3;
    juce::String phrase;
    for (auto c : std::u32string_view(U"きゃっしーん")) phrase += juce::String::charToString(c);
    const auto morae = pronounce(phrase);
    if (morae.size() != 5 || morae[0].glide != 1 || morae[0].onset != velar || morae[1].onset != closure
        || morae[2].onset != sh || morae[3].vowel != 1 || morae[4].onset != moraN) return 8;
    if (!testRecordedVoice()) return 9;
    bool hasScoop = false, hasVibrato = false, hasClosure = false;
    for (const auto& syllable : parts.front().syllables) {
        hasScoop |= syllable.phone.delivery == scoop && syllable.phone.intensity > 0;
        hasVibrato |= syllable.phone.delivery == vibrato && syllable.phone.intensity > 0;
        hasClosure |= syllable.phone.onset == closure;
    }
    if (!hasScoop || !hasVibrato || !hasClosure) return 10;
    std::unique_ptr<juce::AudioProcessor> singer(createPluginFilter());
    singer->setRateAndBufferSizeDetails(48000, 2048);
    singer->prepareToPlay(48000, 2048);
    juce::AudioBuffer<float> audio(2, 2048);
    juce::MidiBuffer midi;
    auto render = [&](Phone phone) {
        singer->reset(); midi.clear();
        midi.addEvent(phoneMessage(1,60,phone),0);
        midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(105)),0);
        singer->processBlock(audio,midi);
        return juce::AudioBuffer<float>(audio);
    };
    const auto a = render({0,0}), i = render({1,0}), scooped = render({0,0,0,scoop,100});
    double difference = 0;
    for (int n = 0; n < audio.getNumSamples(); ++n) difference += std::abs(a.getSample(0,n) - i.getSample(0,n));
    if (!std::isfinite(difference) || difference < 1) return 4;
    double inflection = 0;
    for (int n = 0; n < audio.getNumSamples(); ++n) inflection += std::abs(a.getSample(0,n) - scooped.getSample(0,n));
    if (!std::isfinite(inflection) || inflection < 1) return 15;
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
    if (audio.getMagnitude(0,0,2048) != 0) return 7;
    if (argc >= 3) {
        if (argc == 4) {
            juce::String error;
            const juce::File folder(juce::String::fromUTF8(argv[3]));
            auto bank = VoiceBank::load(folder, error);
            if (!bank || !bank->find({0,vowelOnly}) || !bank->find({0,velar})) return 14;
            std::cout << bank->sounds.size() << " Japanese CV samples loaded\n";
            state.setAttribute("voiceFolder", folder.getFullPathName());
            juce::AudioProcessor::copyXmlToBinary(state,blob);
            restored->setStateInformation(blob.getData(),int(blob.getSize()));
        }
        const auto file = juce::File(juce::String::fromUTF8(argv[2]));
        if (file.exists()) return 11;
        auto writer = wavWriter(file, 48000);
        if (!writer) return 11;
        const double bpm = double(juce::JSON::parse(source)["timeline"]["tempoChanges"][0]["bpm"]);
        if (!(bpm > 0)) return 12;
        head.position.setBpm(bpm);
        const auto start = parts.front().syllables.front().start;
        for (int pos = 0; pos < int(16 * 60 * 48000 / bpm); pos += audio.getNumSamples()) {
            head.position.setPpqPosition(start + pos * bpm / (60 * 48000));
            restored->processBlock(audio,midi);
            if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples())) return 13;
        }
    }
    return 0;
}
