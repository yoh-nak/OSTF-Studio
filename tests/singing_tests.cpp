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
        ~Cleanup() { wav.deleteFile(); oto.deleteFile(); folder.getChildFile("character.txt").deleteFile();
            folder.getChildFile("portrait.bmp").deleteFile(); folder.deleteFile(); }
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
    // A Japanese bank containing only a vowel still uses a single voiced source for /ma/.
    {
        juce::XmlElement xml("OSTFSinger");
        xml.setAttribute("voiceFolder",dir.getFullPathName());
        juce::MemoryBlock state; juce::AudioProcessor::copyXmlToBinary(xml,state);
        std::unique_ptr<juce::AudioProcessor> singer(createPluginFilter());
        singer->setStateInformation(state.getData(),int(state.getSize()));
        singer->setRateAndBufferSizeDetails(48000,1024); singer->prepareToPlay(48000,1024);
        juce::MidiBuffer notes;
        notes.addEvent(phoneMessage(1,69,{0,m}),0);
        notes.addEvent(juce::MidiMessage::noteOn(1,69,(juce::uint8)110),0);
        juce::AudioBuffer<float> audio(2,1024); singer->processBlock(audio,notes);
        juce::AudioBuffer<float> reference(2,1024);
        singer->reset(); notes.clear();
        notes.addEvent(phoneMessage(1,69,{0,vowelOnly}),0);
        notes.addEvent(juce::MidiMessage::noteOn(1,69,(juce::uint8)110),0);
        singer->processBlock(reference,notes);
        for(int i=520;i<1024;++i) {
            const auto expected=reference.getSample(0,i)*i/1200.f;
            if(std::abs(audio.getSample(0,i)-expected)>.002f) return false;
        }
    }
    // Shift-JIS 'ka' (82 A9). The alias, offset, consonant and negative cutoff all matter.
    const char line[] = "a.wav=\x82\xa9,20,60,-240,0,0\n";
    if (!oto.replaceWithData(line, sizeof(line) - 1)) return false;
    const unsigned char bmp[58] = {
        'B','M',58,0,0,0, 0,0,0,0, 54,0,0,0, 40,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,24,0,
        0,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 32,96,208,0
    };
    if (!dir.getChildFile("character.txt").replaceWithText("name=Test Voice\nimage=portrait.bmp")
        || !dir.getChildFile("portrait.bmp").replaceWithData(bmp, sizeof bmp)) return false;
    bank = VoiceBank::load(dir, error);
    if (!bank || bank->name != "Test Voice" || bank->portrait != dir.getChildFile("portrait.bmp")) return false;
    if (!bank || bank->find({0,vowelOnly}) || !bank->find({0,velar}) || bank->find({0,velar})->frames.size() > 5760) return false;
    juce::XmlElement xml("OSTFSinger");
    xml.setAttribute("voiceFolder", dir.getFullPathName());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    std::unique_ptr<juce::AudioProcessor> singer(createPluginFilter());
    singer->setStateInformation(state.getData(), int(state.getSize()));
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(singer->createEditorAndMakeActive());
        if (!editor) return false;
        bool hasPortrait = false;
        for (auto* child : editor->getChildren())
            if (auto* portrait = dynamic_cast<juce::ImageComponent*>(child); portrait && portrait->isVisible())
                hasPortrait = portrait->getImage().getPixelAt(0, 0) == juce::Colour(208,96,32);
        if (!hasPortrait) return false;
    }
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
    double energy=0., inPhase=0., quadrature=0.;
    double position = 0;
    const auto* reference = bank->find({0,velar});
    for (int b = 0; b < 200; ++b) {
        singer->processBlock(block, events);
        for (int i = 0; i < 256; ++i) {
            const float out = block.getSample(0, i);
            if (!std::isfinite(out)) return false;
            // The recorded consonant keeps its original timing even when the sung vowel is transposed.
            if (position < reference->consonantFrames) {
                const float expected = reference->read(position, reference->rate / 48000);
                if (b >= 3 && std::abs(out - expected * (110.f / 127)) > .002f) return false;
            }
            if (b >= 50 && prev <= 0 && out > 0) ++crossings;
            if (b >= 50) {
                const auto phase = juce::MathConstants<double>::twoPi * 440. * (b*256+i) / 48000.;
                inPhase += out*std::cos(phase); quadrature += out*std::sin(phase); energy += out*out;
            }
            prev = out;
        }
    }
    if (std::abs(crossings - 352)>3) return false;
    // A transposed pure vowel must remain a single tone; a crossing count alone misses added harmonics.
    const auto fundamentalFraction = 2.*(inPhase*inPhase + quadrature*quadrature)/(38400.*energy);
    if (!(fundamentalFraction > .994)) return false;
    events.clear();
    events.addEvent(juce::MidiMessage::allSoundOff(1),0);
    events.addEvent(phoneMessage(1,45,{0,velar}),0);
    events.addEvent(juce::MidiMessage::noteOn(1,45,juce::uint8(110)),0);
    prev=0; crossings=0;
    for(int b=0;b<200;++b) {
        singer->processBlock(block,events); events.clear();
        for(int i=0;i<256;++i) {
            const float value=block.getSample(0,i);
            if(b>=50 && prev<=0 && value>0) ++crossings;
            prev=value;
        }
    }
    return std::abs(crossings-88)<=3; // 0.8 s at A2, well below the recorded 200 Hz.

}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ostfstudio::singing;
    if (argc < 2 || argc > 4) return 1;
    const auto source = juce::File(juce::String::fromUTF8(argv[1])).loadFileAsString();
    const auto demoScore = juce::JSON::parse(source);
    const auto& scoreUnits = items(demoScore["lyrics"]["units"]);
    int englishUnits = 0;
    for (const auto& unit : scoreUnits) {
        const auto printed = unit["text"].toString();
        if (!printed.containsAnyOf("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")) continue;
        const auto reading = unit["pronunciation"].toString();
        if (reading.isEmpty()) return 40;
        for (auto c : reading) if (c < 0x30a1 || c > 0x30ff) return 40;
        ++englishUnits;
    }
    if (englishUnits < 100) return 40;
    const auto& phrases = items(demoScore["lyrics"]["phrases"]);
    if (std::none_of(phrases.begin(), phrases.end(), [](const auto& phrase) {
        return phrase["text"].toString() == "If the street lights all go out";
    })) return 41;
    const auto parts = readParts(demoScore);
    if (parts.empty() || parts.front().syllables.empty()) return 2;
    const auto kana = pronounce(juce::String::charToString(0x30ab) + juce::String::charToString(0x30bc));
    if (kana.size() != 2 || kana[0].vowel != 0 || kana[1].vowel != 3 || kana[1].onset != z) return 3;
    juce::String phrase;
    for (auto c : std::u32string_view(U"きゃっしーん")) phrase += juce::String::charToString(c);
    const auto morae = pronounce(phrase);
    if (morae.size() != 5 || morae[0].glide != 1 || morae[0].onset != velar || morae[1].onset != closure
        || morae[2].onset != sh || morae[3].vowel != 1 || morae[4].onset != moraN) return 8;
    const auto extended = lyricPhones(juce::String::fromUTF8("\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89"));
    if (extended.size() != 2 || extended[0].morae != 2 || extended[1].onset != d) return 36;
    const auto kanaEdges = articulationPositions(extended, .45);
    if (std::abs(kanaEdges[1] - 2./3) > 1.e-8) return 37;
    const auto sing = lyricPhones("sing");
    if (sing.size() < 3 || sing.front().vowel != 1) return 38;
    if (!testRecordedVoice()) return 9;
    const auto& factory = VoiceBank::factory();
    if (factory.sounds.size() < 90 || !factory.find({0,vowelOnly}) || !factory.find({1,sh})
        || !factory.find({3,m}) || !factory.find({4,n}) || !factory.find({4,velar,1})) return 16;
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
        midi.addEvent(phoneMessage(1,60,phone, phone.durationMs / 1000.),0);
        midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(105)),0);
        singer->processBlock(audio,midi);
        return juce::AudioBuffer<float>(audio);
    };
    auto accented = render({0,0,0,plain,0,65,250});
    auto unaccented = render({0,0,0,plain,0,1,250});
    if (!(accented.getRMSLevel(0,512,1536) > 1.04f * unaccented.getRMSLevel(0,512,1536))) return 30;
    // The same simple vowel must use the selected singer in either language.
    // The Japanese approximation of English AA differs only by its unstressed gain.
    const auto meiJapanese = render({0,vowelOnly,0,plain,0,0,250});
    auto distance = [](const auto& a,const auto& b,float gain) {
        double sum = 0;
        for (int i=0;i<a.getNumSamples();++i)
            sum += std::abs(a.getSample(0,i)*gain - b.getSample(0,i));
        return sum/a.getNumSamples();
    };
    if (distance(meiJapanese,unaccented,.94f) > .0001) return 31;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(singer->createEditorAndMakeActive());
        juce::ComboBox* choice = nullptr;
        for (auto* child : editor->getChildren())
            if (child->getTitle() == "Singer voice") choice = dynamic_cast<juce::ComboBox*>(child);
        if (!choice || choice->getSelectedId() != 1) return 32;
        choice->setSelectedId(2,juce::sendNotificationSync);
        const auto kalJapanese = render({0,vowelOnly,0,plain,0,0,250});
        const auto kalEnglish = render({0,0,0,plain,0,1,250});
        const auto peak = kalJapanese.getMagnitude(0,0,2048);
        const auto sameSingerError = distance(kalJapanese,kalEnglish,.94f);
        const auto otherSingerError = distance(meiJapanese,kalJapanese,1);
        if (peak < .008f || sameSingerError > .0001 || otherSingerError < .005) {
            std::cerr << "KAL16 vowel peak " << peak << ", same singer difference " << sameSingerError
                      << ", other singer difference " << otherSingerError << '\n';
            return 33;
        }
        juce::MemoryBlock recall;
        singer->getStateInformation(recall);
        choice->setSelectedId(1,juce::sendNotificationSync);
        singer->setStateInformation(recall.getData(),int(recall.getSize()));
        dynamic_cast<juce::ChangeBroadcaster&>(*singer).dispatchPendingMessages();
        if (choice->getSelectedId() != 2 || distance(render({0,0,0,plain,0,1,250}),kalEnglish,1) > .0001) return 34;
        choice->setSelectedId(1,juce::sendNotificationSync);
    }
    const auto word = lyricPhones("light");
    const auto city = lyricsFor(juce::JSON::parse(R"({"lyrics":{
        "phrases":[{"id":"line","text":"city"}],
        "units":[{"id":"first","phraseId":"line","text":"ci"},{"id":"last","phraseId":"line","text":"ty"}],
        "alignments":[{"unitIds":["first"],"noteIds":["a"]},{"unitIds":["last"],"noteIds":["b"]}]}})"));
    if (city.at("a").size() != 2 || city.at("b").size() != 2
        || (city.at("a").front().english & 63) != 31 || (city.at("b").front().english & 63) != 33) return 21;
    if (word.size() != 3 || (word[0].english & 63) != 25 || (word[1].english & 63) != 6
        || (word[2].english & 63) != 33 || !isPhone(phoneMessage(1, 60, word[2]))) return 18;
    const auto timed = phoneMessage(1,60,word[1],.730);
    if (!isPhone(timed) || timed.getSysExDataSize()!=14
        || timed.getSysExData()[12] + 128*timed.getSysExData()[13] != 730) return 22;
    // The single-source splice starts at the last rendered sample of the previous phone.
    const auto previous = render(word[1]);
    const auto lastSample = previous.getSample(0,previous.getNumSamples()-1);
    midi.addEvent(phoneMessage(1,60,{1,0}),0);
    singer->processBlock(audio,midi);
    if (std::abs(audio.getSample(0,0)-lastSample) > 1.e-5f) return 23;
    const auto cluster = lyricPhones("fast");
    const auto timing = articulationPositions(cluster, .5);
    if (cluster.size() != 4 || timing[1] <= 1. - timing[3] || std::abs(timing.back() - 1.) > 1.e-10) return 27;
    const auto edges = articulationPositions(word, .8);
    if (edges[2] - edges[1] < .8 || edges.back() != 1.) return 19;
    const auto vowel = render(word[1]), stop = render(word[2]);
    if (vowel.getMagnitude(0, 0, 2048) <= .01f || stop.getMagnitude(0, 0, 2048) <= .001f) return 20;
    const auto a = render({0,0}), i = render({1,0}), scooped = render({0,0,0,scoop,100});
    // The voiced onset is pitch-synchronous immediately, including a sustained
    // note played live without an encoded phone duration.
    const auto* vowelA = factory.find({0,vowelOnly});
    if (!vowelA || vowelA->pitchMarks.size()<3) return 17;
    const int cycle = juce::roundToInt(48000. / juce::MidiMessage::getMidiNoteInHertz(60));
    double periodicError=0, energy=0;
    for (int n=600+cycle; n<a.getNumSamples(); ++n) {
        const double x=a.getSample(0,n), d=x-a.getSample(0,n-cycle);
        periodicError+=d*d; energy+=x*x;
    }
    if (energy<.001 || periodicError>energy*.55) {std::cerr << "Onset energy " << energy << ", period residual " << periodicError << ", pitch marks " << vowelA->pitchMarks.size() << '\n'; return 17;}
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
        const bool bilingual = argc == 4 && juce::String(argv[3]).startsWith("bilingual-");
        const bool bridge = argc == 4 && juce::String(argv[3]) == "bridge-mei";
        if (bilingual || bridge) {
            state.setAttribute("builtinVoice",juce::String(argv[3]).fromFirstOccurrenceOf("-",false,false));
            juce::AudioProcessor::copyXmlToBinary(state,blob);
            restored->setStateInformation(blob.getData(),int(blob.getSize()));
        } else if (argc == 4) {
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
        double start = parts.front().syllables.front().start;
        if (bridge || bilingual) {
            const auto score = juce::JSON::parse(source);
            const auto sections = items(score["timeline"]["sections"]);
            const auto it = std::find_if(sections.begin(), sections.end(), [&](const auto& s){ return s["name"].toString() == (bridge ? "Bridge" : "Pre-Chorus"); });
            if (it == sections.end()) return 39;
            start = double((*it)["start"]) / double(score["ppq"]);
        }
        for (int pos = 0; pos < int(16 * 60 * 48000 / bpm); pos += audio.getNumSamples()) {
            head.position.setPpqPosition(start + pos * bpm / (60 * 48000));
            restored->processBlock(audio,midi);
            if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples())) return 13;
        }
    }
    return 0;
}
