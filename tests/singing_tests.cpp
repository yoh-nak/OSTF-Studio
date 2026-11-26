#include "../src/plugins/instruments/SingingScore.h"
#include "../src/plugins/instruments/VoiceBank.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <complex>
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

// The source vowel has a 200 Hz fundamental and a strong vocal-tract resonance at
// 1.6 kHz. Moving the sung note must move the harmonic spacing while leaving the
// resonance in the same band. A pure-tone pitch test cannot detect this failure.
static bool testVowelResonance(const juce::File& folder) {
    using namespace ostfstudio::singing;
    juce::AudioBuffer<float> raw(1, 12000);
    raw.clear();
    for (int h = 1; h <= 24; ++h) {
        const double amplitude = h == 1 ? .12 : .16 * std::exp(-.5 * std::pow((h * 200. - 1600.) / 260., 2));
        for (int n = 0; n < raw.getNumSamples(); ++n)
            raw.addSample(0, n, float(amplitude * std::sin(juce::MathConstants<double>::twoPi * h * n / 120.)));
    }
    auto writer = wavWriter(folder.getChildFile("a.wav"), 24000);
    if (!writer || !writer->writeFromAudioSampleBuffer(raw, 0, raw.getNumSamples())) return false;
    writer.reset();
    juce::XmlElement state("OSTFSinger");
    state.setAttribute("voiceFolder", folder.getFullPathName());
    juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary(state, bytes);
    std::unique_ptr<juce::AudioProcessor> singer(createPluginFilter());
    singer->setStateInformation(bytes.getData(), int(bytes.getSize()));
    bool passed = true;
    for (int key : {45, 69}) {
        singer->prepareToPlay(48000, 256);
        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer notes;
        notes.addEvent(phoneMessage(1, key, {0, vowelOnly}), 0);
        notes.addEvent(juce::MidiMessage::noteOn(1, key, juce::uint8(100)), 0);
        std::array<std::complex<double>, 36> bins{};
        const auto pitch = juce::MidiMessage::getMidiNoteInHertz(key);
        for (int b = 0; b < 100; ++b) {
            singer->processBlock(block, notes); notes.clear();
            if (b < 25) continue;
            for (int n = 0; n < 256; ++n) {
                const auto fundamental = std::polar(1., -juce::MathConstants<double>::twoPi * pitch * (b * 256 + n) / 48000.);
                auto harmonic = fundamental;
                for (auto& bin : bins) { bin += double(block.getSample(0,n)) * harmonic; harmonic *= fundamental; }
            }
        }
        const auto peak = std::max_element(bins.begin() + 1, bins.end(), [](auto a, auto b){ return std::norm(a) < std::norm(b); });
        const auto resonance = (1 + std::distance(bins.begin(), peak)) * pitch;
        std::cout << "Input resonance 1600 Hz, sung F0 " << pitch << " Hz, output peak " << resonance << " Hz\n";
        passed &= std::abs(*peak) > 1. && std::abs(resonance - 1600.) <= pitch * .5 + 65.;
    }
    return passed;
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
    const auto* reference = bank->find({0,velar});
    for (int b = 0; b < 200; ++b) {
        singer->processBlock(block, events);
        events.clear();
        for (int i = 0; i < 256; ++i) {
            const float out = block.getSample(0, i);
            if (!std::isfinite(out)) return false;
            // Only the unvoiced portion keeps its original waveform; OTO may mark voiced material as consonant.
            double position=(b*256+i)*reference->rate/48000.;
            if (position < reference->voicedStart) {
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
    if (std::abs(crossings-88)>3 || !oto.deleteFile() || !wav.deleteFile()) return false;
    return testVowelResonance(dir);

}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ostfstudio::singing;
    if (argc < 2 || argc > 4) return 1;
    // A bright upper partial above output Nyquist must not fold back into a second tone.
    RecordedMora bright;
    bright.period=64; bright.loopStart=256; bright.loopEnd=1024;
    for (int i=0;i<2048;++i) bright.frames.push_back(float(std::cos(juce::MathConstants<double>::twoPi*.375*i)));
    bright.preparePitchMarks();
    double foldedEnergy=0;
    for (int i=200;i<600;++i) {const auto x=bright.cycleSample(i,4.); foldedEnergy+=x*x;}
    if (foldedEnergy>0.1) return 40;
    RecordedMora nasal;
    nasal.period=80; nasal.loopStart=2000; nasal.loopEnd=3200; nasal.consonantFrames=1600;
    for(int i=0;i<4000;++i) nasal.frames.push_back(i<400 ? 0.f : float(.3*std::sin(juce::MathConstants<double>::twoPi*i/80)));
    nasal.preparePitchMarks();
    if(nasal.voicedStart<320 || nasal.voicedStart>560) return 41;
    if(nasal.vowelFirst>1750 || nasal.vowelLast<3600) return 42;
    RecordedMora recordedCV;
    recordedCV.rate=48000; recordedCV.voicedStart=480; recordedCV.vowelFirst=2400; recordedCV.vowelLast=17000;
    // A 40 ms recorded mouth opening finishes at 40 ms for both long notes
    // and live input. On a 20 ms note it reaches the vowel by 10 ms.
    for (double length : {0., .25, 1.})
        if (std::abs(recordedCV.sungPosition(.020,length,true)-1440.)>.001
            || std::abs(recordedCV.sungPosition(.040,length,true)-2400.)>.001) return 53;
    if (std::abs(recordedCV.sungPosition(.010,.020,true)-2400.)>.001
        || recordedCV.sungPosition(0.,1.,false)!=2400. || recordedCV.sungPosition(1.,1.,true)!=17000.) return 53;
    RecordedMora fricative;
    fricative.rate=24000; fricative.period=120; fricative.consonantFrames=1080;
    fricative.loopStart=4500; fricative.loopEnd=5940;
    unsigned noise=1;
    for (int i=0;i<9000;++i) {
        noise=noise*1664525u+1013904223u;
        fricative.frames.push_back(i<2400 ? float((double(noise)/2147483648.-1.)*.15)
            : float(.4*std::sin(juce::MathConstants<double>::twoPi*i/120)));
    }
    fricative.preparePitchMarks();
    std::cout << "100 ms frication, detected voicing at " << 1000.*fricative.voicedStart/fricative.rate << " ms\n";
    if (std::abs(fricative.voicedStart-2400)>240) return 50;
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
    const auto holds=lyricsFor(juce::JSON::parse(R"({"lyrics":{
      "units":[{"id":"a","text":"\u3048"},{"id":"n","text":"\u3093"},{"id":"h","text":"\u30fc"}],
      "alignments":[{"unitIds":["a"],"noteIds":["1","5"]},{"unitIds":["n"],"noteIds":["2"]},
                    {"unitIds":["h"],"noteIds":["3","4","6"]}]},
      "tracks":[{"clips":[{"start":0,"notes":[{"id":"1","start":0},{"id":"2","start":1},{"id":"3","start":2},
                   {"id":"4","start":3},{"id":"5","start":4},{"id":"6","start":5}]}]}]})"));
    if (holds.at("3").front().onset!=moraN || holds.at("4").front().onset!=moraN
        || holds.at("6").front().onset!=vowelOnly || holds.at("6").front().vowel!=3) return 52;
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
    // An isolated English /s/ or /t/ is borrowed from a Japanese CV. The
    // following recorded vowel must not become an extra pitched syllable.
    auto renderPhone = [&](Phone phone, int key, double duration=.16) {
        singer->reset();
        midi.clear();
        midi.addEvent(phoneMessage(1,key,phone,duration),0);
        midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(105)),0);
        juce::AudioBuffer<float> rendered(2,7680);
        for (int pos=0; pos<rendered.getNumSamples(); pos+=256) {
            juce::AudioBuffer<float> slice(rendered.getArrayOfWritePointers(),2,pos,256);
            singer->processBlock(slice,midi);
            midi.clear();
        }
        return rendered;
    };
    for (int consonant : {31,33}) {
        Phone phone;
        phone.english=consonant;
        const auto low=renderPhone(phone,45), high=renderPhone(phone,69);
        double difference=0;
        for (int i=0; i<low.getNumSamples(); ++i)
            difference+=std::pow(low.getSample(0,i)-high.getSample(0,i),2);
        difference=std::sqrt(difference/low.getNumSamples());
        std::cout << "Mei English " << (consonant==31 ? "/s/" : "/t/") << " pitch-dependent RMS " << difference << '\n';
        if (low.getRMSLevel(0,0,7680)<.001f || difference>.0001) return 48;
    }
    const auto japaneseCV=renderPhone({0,sibilant},69);
    if (japaneseCV.getRMSLevel(0,5760,1920)<.01f) return 49;
    // A short Japanese syllable must reach its pitched vowel while the key is held.
    const auto shortLow=renderPhone({1,sh},55,.045), shortHigh=renderPhone({1,sh},67,.045);
    double shortPitchDifference=0.;
    for (int frame=1440;frame<2160;++frame) shortPitchDifference+=std::pow(shortLow.getSample(0,frame)-shortHigh.getSample(0,frame),2);
    shortPitchDifference=std::sqrt(shortPitchDifference/720.);
    std::cout<<"Mei 45 ms shi, final 15 ms difference between pitches: "<<shortPitchDifference<<'\n';
    if (shortPitchDifference<.003) return 54;
    // At the end of an English nasal the mouth is still closed. Changing the
    // following vowel hint should not turn a held /m/, /n/ or /ng/ into it.
    const auto* nasalSample=factory.find({2,moraN});
    std::cout << "Mei recorded nasal: " << (nasalSample ? "available" : "absent") << '\n';
    bool closedNasals=nasalSample!=nullptr;
    for (int arpabet : {26,27,28}) {
        Phone beforeA{0}, beforeI{1};
        beforeA.english=beforeI.english=arpabet;
        const auto a=renderPhone(beforeA,60), i=renderPhone(beforeI,60);
        double difference=0;
        for (int frame=5760;frame<7680;++frame)
            difference+=std::pow(a.getSample(0,frame)-i.getSample(0,frame),2);
        difference=std::sqrt(difference/1920);
        std::cout << "Nasal " << arpabet << " sustained context difference " << difference
                  << ", level " << a.getRMSLevel(0,5760,1920) << '\n';
        closedNasals &= difference<.0005 && a.getRMSLevel(0,5760,1920)>.001f;
    }
    if (!closedNasals) return 51;
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
    std::cout << "Factory A vowel, loop / voiced source (samples): "
              << vowelA->loopEnd - vowelA->period - vowelA->loopStart << " / "
              << vowelA->vowelLast - vowelA->vowelFirst << '\n';
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
