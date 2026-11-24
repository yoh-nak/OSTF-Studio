#include "SingingScore.h"
#include "VoiceBank.h"
#include "../../core/VocalDelivery.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace {
using namespace ostfstudio::singing;

class Voice {
  public:
    int channel = -1, key = -1;
    void start(int ch, int pitch, float velocity, Phone p, double sr, const VoiceBank* bank) {
        channel = ch; key = pitch; gain = velocity;
        phase = envelope = 0; released = false;
        hz = juce::MidiMessage::getMidiNoteInHertz(pitch);
        resonators = {};
        articulate(p, sr, bank);
    }
    void articulate(Phone p, double sr, const VoiceBank* bank) {
        constexpr float freq[5][3] = {
            {760, 1200, 2600}, {290, 2250, 3100}, {330, 1250, 2350}, {490, 1880, 2650}, {490, 900, 2450}};
        constexpr float bandwidth[3] = {90, 120, 175};
        phone = p;
        age = 0;
        recorded = bank ? bank->find(p) : nullptr;
        vowelSample = false;
        if (bank && !recorded && p.onset != closure && p.onset != moraN) {
            recorded = bank->find({p.vowel, vowelOnly});
            vowelSample = recorded != nullptr;
        }
        position = 0;
        const int onset = p.onset;
        nasal = onset == m || onset == n || onset == moraN;
        stop = onset == velar || onset == g || onset == t || onset == d || onset == pStop || onset == b;
        fricative = onset == sibilant || onset == sh || onset == z || onset == j || onset == ch || onset == ts
            || onset == h || onset == f || onset == v;
        voiced = onset == g || onset == d || onset == b || onset == z || onset == j || onset == v;
        double first = freq[p.vowel][0], second = freq[p.vowel][1];
        if (p.glide == 1 || onset == y || onset == sh || onset == j || onset == ch) { first = 305; second = 2200; }
        else if (p.glide == 2 || onset == w || onset == b || onset == pStop || onset == m) { first = 280; second = 800; }
        else if (nasal || onset == r || onset == t || onset == d) { first = 280; second = 1570; }
        else if (onset == velar || onset == g) { first = 380; second = 1800; }
        const double initial[3] = {first, second, freq[p.vowel][2]};
        transition = 1 - std::exp(-1 / (.017 * sr));
        for (int i = 0; i < 3; ++i) {
            const double pole = std::exp(-juce::MathConstants<double>::pi * bandwidth[i] / sr);
            auto coefficient = [&](double f) { return 2 * pole * std::cos(juce::MathConstants<double>::twoPi * std::min(.44 * sr, f) / sr); };
            auto& res = resonators[i];
            res.a = coefficient(initial[i]); res.target = coefficient(freq[p.vowel][i]);
            res.b = -pole * pole; res.drive = (1 - pole) * (i == 0 ? 1.0 : .65);
        }
    }
    void stopNote() { released = true; }
    float sample(double sr, double bendRatio, float expression) {
        envelope = std::clamp(envelope + (released ? -95.0 : 110.0) / sr, 0.0, 1.0);
        if (released && envelope == 0) { key = channel = -1; return 0; }
        const double time = age++ / sr;
        if (phone.onset == closure) return 0;
        const double strength = phone.intensity / 127.0;
        double cents = 0;
        if (phone.delivery == vibrato) cents = vibratoCents(time,strength);
        if (phone.delivery == scoop) cents = scoopCents(time,strength);
        const auto pitch = hz * bendRatio * (cents == 0 ? 1 : std::exp2(cents / 1200));
        const auto level = gain * envelope * expression;
        const float recording = recorded ? recorded->read(position, pitch * recorded->period / sr) : 0.f;
        if (recorded && !vowelSample) return float(level * recording);
        const double step = std::min(.45, pitch / sr);
        phase += step; phase -= std::floor(phase);
        // A band-limited glottal edge excites the higher formants as well as the fundamental.
        double edge = 2 * phase - 1;
        if (phase < step) { const double t = phase/step; edge -= 2*t-t*t-1; }
        else if (phase > 1-step) { const double t = (phase-1)/step; edge -= t*t+2*t+1; }
        const double pulse = .82*edge + .18*std::sin(phase*juce::MathConstants<double>::twoPi);
        noise = noise * 1664525u + 1013904223u;
        const double white = double(noise) / 2147483648.0 - 1.0;
        const double hiss = white - previousNoise;
        previousNoise = white;
        const auto onset = phone.onset;
        const double attack = fricative ? .075 : stop ? .042 : nasal ? .065 : .035;
        const double opened = std::min(1.0, time / attack);
        double source = pulse + white * .012, unfiltered = 0;
        if (fricative || stop) {
            const double burst = stop ? (time < .014 ? 0 : std::exp(-95 * (time - .014))) : 1 - opened;
            source = pulse * (opened + (voiced ? .40 : 0) * (1 - opened)) + white * burst * .38;
            unfiltered = (onset == h || onset == f ? white * .10 : hiss * (onset == sh || onset == j ? .13 : .19)) * burst;
        }
        if (nasal) {
            const double nasalMix = onset == moraN ? 1 : 1 - opened;
            source *= 1 - .88 * nasalMix;
            unfiltered += nasalMix * .46 * std::sin(juce::MathConstants<double>::twoPi * phase);
        }
        if (onset == r && time > .009 && time < .022) source *= .10;
        double out = 0;
        for (auto& res : resonators) {
            res.a += (res.target - res.a) * transition;
            const double value = res.drive * source + res.a * res.y1 + res.b * res.y2;
            res.y2 = res.y1; res.y1 = value; out += value;
        }
        double tone = .45 * std::tanh(out) + unfiltered;
        if (vowelSample) tone += opened * (recording - tone);
        return float(level * tone);
    }
  private:
    struct Resonator { double a{}, target{}, b{}, drive{}, y1{}, y2{}; };
    std::array<Resonator, 3> resonators{};
    const RecordedMora* recorded = nullptr;
    Phone phone;
    double hz{}, phase{}, gain{}, envelope{}, position{}, transition{}, previousNoise{};
    unsigned noise = 1397;
    uint64_t age = 0;
    bool released = false, nasal = false, stop = false, voiced = false, fricative = false, vowelSample = false;
};
class Singer;
class Editor final : public juce::AudioProcessorEditor {
  public:
    explicit Editor(Singer&);
    void resized() override;

  private:
    Singer& singer;
    juce::TextButton open{"Load OSTF..."};
    juce::TextButton importVoice{"Load voice folder..."}, factory{"Use built-in voice"};
    juce::ComboBox parts;
    juce::Label status, voiceStatus;
    juce::ImageComponent portrait;
    std::unique_ptr<juce::FileChooser> chooser;
    void refresh();
};
class Singer final : public juce::AudioProcessor {
  public:
    const bool studioHost = juce::File::getSpecialLocation(juce::File::hostApplicationPath)
        .getFileNameWithoutExtension().equalsIgnoreCase("OSTF Studio");
    Singer() : AudioProcessor(BusesProperties().withOutput("Voice", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "OSTF Formant Singer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return .02; }
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::mono() ||
               l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Formant voice"; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new Editor(*this); }
    void prepareToPlay(double r, int) override {
        rate = r;
        reset();
    }
    void releaseResources() override {}
    void reset() override {
        voices = {};
        articulation = {};
        bend.fill(1.0);
        expression.fill(1.0f);
        lastPart = -1;
        lastIndex = -1;
        expected = -1;
    }
    bool load(const juce::String& source, int index) {
        auto next = readParts(juce::JSON::parse(source));
        if (next.empty())
            return false;
        juce::String nextText = source;
        const juce::SpinLock::ScopedLockType guard(lock);
        parts.swap(next);
        text.swapWith(nextText);
        selected = juce::jlimit(-1, int(parts.size()) - 1, index);
        ++generation;
        return true;
    }
    void select(int index) {
        const juce::SpinLock::ScopedLockType guard(lock);
        selected = juce::jlimit(-1, int(parts.size()) - 1, index);
        ++generation;
    }
    juce::StringArray trackNames(int& index) const {
        const juce::SpinLock::ScopedLockType guard(lock);
        juce::StringArray names;
        for (const auto& part : parts)
            names.add(part.name);
        index = selected;
        return names;
    }
    bool setVoice(const juce::File& directory, juce::String& error, bool recalling = false) {
        auto next = directory == juce::File{} ? nullptr : VoiceBank::load(directory, error);
        if (!next && directory != juce::File{} && !recalling) return false;
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            bank.swap(next);
            requestedVoice = directory;
            ++generation;
        }
        if (!recalling) updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
        return true;
    }
    juce::String voiceName() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        if (bank) return bank->name + "  /  " + juce::String(int(bank->sounds.size())) + " recorded morae";
        if (requestedVoice != juce::File{}) return "Voice folder unavailable: " + requestedVoice.getFileName() + "  /  using built-in voice";
        return factoryBank.name + "  /  MMDAgent";
    }
    juce::File voicePortrait() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return bank ? bank->portrait : juce::File{};
    }
    void getStateInformation(juce::MemoryBlock& b) override {
        juce::XmlElement xml("OSTFSinger");
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            xml.setAttribute("part", selected);
            if (requestedVoice != juce::File{}) xml.setAttribute("voiceFolder", requestedVoice.getFullPathName());
            xml.addTextElement(text);
        }
        copyXmlToBinary(xml, b);
    }
    void setStateInformation(const void* b, int n) override {
        if (auto xml = getXmlFromBinary(b, n); xml && xml->hasTagName("OSTFSinger")) {
            const auto path = xml->getStringAttribute("voiceFolder");
            juce::String error;
            setVoice(juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File{}, error, true);
            auto source = xml->getAllSubText();
            if (source.isNotEmpty() && !studioHost)
                load(source, xml->getIntAttribute("part", -1));
            else {
                const juce::SpinLock::ScopedLockType guard(lock);
                parts.clear();
                text.clear();
                selected = -1;
                ++generation;
            }
        }
    }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals quiet;
        b.clear();
        const juce::SpinLock::ScopedTryLockType guard(lock);
        if (!guard.isLocked()) {
            midi.clear();
            return;
        }
        if (seen != generation) {
            reset();
            seen = generation;
        }
        const auto* sequence = selected >= 0 ? &parts[size_t(selected)].syllables : nullptr;
        double beat = 0, step = 0;
        if (sequence) {
            auto* head = getPlayHead();
            const auto pos = head ? head->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
            if (!pos || !pos->getIsPlaying() || !pos->getPpqPosition() || !pos->getBpm()) {
                reset();
                midi.clear();
                return;
            }
            beat = *pos->getPpqPosition();
            step = *pos->getBpm() / (60.0 * rate);
            if (lastPart != selected || std::abs(beat - expected) > step * 2) {
                reset();
                lastPart = selected;
                auto it = std::upper_bound(sequence->begin(), sequence->end(), beat,
                                           [](double at, const auto& s) { return at < s.start; });
                cursor = static_cast<int>(it - sequence->begin()) - 1;
            }
        }
        auto ev = midi.cbegin();
        const auto end = midi.cend();
        for (int i = 0; i < b.getNumSamples(); ++i) {
            if (sequence) {
                while (cursor + 1 < int(sequence->size()) && (*sequence)[size_t(cursor + 1)].start <= beat)
                    ++cursor;
                const int active = cursor >= 0 && beat < (*sequence)[size_t(cursor)].end ? cursor : -1;
                if (active != lastIndex) {
                    if (active >= 0) {
                        const auto& s = (*sequence)[size_t(active)];
                        if (s.continuesNote && lastIndex >= 0 && lastIndex + 1 == active)
                            voices[0].articulate(s.phone, rate, activeBank());
                        else voices[0].start(1, s.key, s.velocity / 127.f, s.phone, rate, activeBank());
                    } else
                        voices[0].stopNote();
                    lastIndex = active;
                }
                beat += step;
            } else
                while (ev != end && (*ev).samplePosition <= i) {
                    handle((*ev).getMessage());
                    ++ev;
                }
            float sample = 0;
            for (auto& v : voices)
                if (v.key >= 0)
                    sample += v.sample(rate, bend[v.channel - 1], expression[v.channel - 1]);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.setSample(ch, i, std::tanh(sample));
        }
        expected = beat;
        midi.clear();
    }

  private:
    void handle(const juce::MidiMessage& m) {
        if (isPhone(m)) {
            const auto* p = m.getSysExData();
            Phone phone{p[6], p[7]};
            if (m.getSysExDataSize() == 11) { phone.glide = p[8]; phone.delivery = p[9]; phone.intensity = p[10]; }
            articulation[p[4]][p[5]] = phone;
            for (auto& v : voices)
                if (v.channel == p[4] + 1 && v.key == p[5])
                    v.articulate(phone, rate, activeBank());
        } else if (m.isNoteOn()) {
            auto* target = &voices[0];
            for (auto& v : voices) {
                if (v.key < 0 || (v.channel == m.getChannel() && v.key == m.getNoteNumber())) {
                    target = &v;
                    break;
                }
            }
            target->start(m.getChannel(), m.getNoteNumber(), m.getFloatVelocity(),
                          articulation[m.getChannel() - 1][m.getNoteNumber()], rate, activeBank());
        } else if (m.isPitchWheel()) {
            bend[m.getChannel() - 1] = std::exp2((m.getPitchWheelValue() - 8192) / (8192.0 * 6));
        } else if (m.isController() && m.getControllerNumber() == 11) {
            expression[m.getChannel() - 1] = m.getControllerValue() / 127.f;
        } else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff()) {
            for (auto& v : voices)
                if (v.channel == m.getChannel() && (!m.isNoteOff() || v.key == m.getNoteNumber()))
                    v.stopNote();
        }
    }
    mutable juce::SpinLock lock;
    juce::String text;
    std::vector<Part> parts;
    int selected = -1, generation = 0, seen = -1, lastPart = -1, cursor = -1, lastIndex = -1;
    double rate = 48000, expected = -1;
    std::array<Voice, 16> voices{};
    const VoiceBank& factoryBank = VoiceBank::factory();
    std::unique_ptr<VoiceBank> bank;
    const VoiceBank* activeBank() const { return bank ? bank.get() : &factoryBank; }
    juce::File requestedVoice;
    std::array<double, 16> bend{};
    std::array<float, 16> expression{};
    std::array<std::array<Phone, 128>, 16> articulation{};
};
juce::Image loadPortrait(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > 8*1024*1024) return {};
    if (!file.hasFileExtension("bmp")) return juce::ImageFileFormat::loadFrom(file);
    // UTAU banks often supply an uncompressed Windows bitmap.
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes) || bytes.getSize() < 54) return {};
    const auto* p = static_cast<const juce::uint8*>(bytes.getData());
    const auto u32 = [&](int at) { return juce::ByteOrder::littleEndianInt(p+at); };
    const int w = int(u32(18)), h = int(u32(22)), bits = juce::ByteOrder::littleEndianShort(p+28);
    if (p[0]!='B' || p[1]!='M' || u32(14)<40 || u32(30)!=0 || (bits!=24 && bits!=32)
        || w<=0 || w>2048 || h==0 || h < -2048 || h>2048) return {};
    const int stride = (w*(bits/8)+3)&~3, height = std::abs(h);
    const size_t offset = u32(10);
    if (offset < 54 || offset > bytes.getSize() || size_t(height)*stride > bytes.getSize()-offset) return {};
    juce::Image img(juce::Image::RGB,w,height,false);
    juce::Image::BitmapData dest(img,juce::Image::BitmapData::writeOnly);
    for (int y=0;y<height;++y) {
        auto* src = p+offset+size_t(h>0 ? height-1-y : y)*stride;
        for (int x=0;x<w;++x,src+=bits/8) dest.setPixelColour(x,y,juce::Colour(src[2],src[1],src[0]));
    }
    return img;
}
Editor::Editor(Singer& s) : AudioProcessorEditor(s), singer(s) {
    addChildComponent(open);
    addChildComponent(parts);
    open.setVisible(!singer.studioHost);
    parts.setVisible(!singer.studioHost);
    addChildComponent(portrait);
    addAndMakeVisible(status);
    addAndMakeVisible(importVoice);
    addAndMakeVisible(factory);
    addAndMakeVisible(voiceStatus);
    refresh();
    parts.onChange = [this] { singer.select(parts.getSelectedId() - 2); };
    open.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Open an OSTF score", juce::File(), "*.ostf");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<Editor>(this)](const auto& fc) {
                                 if (!safe || !fc.getResult().existsAsFile())
                                     return;
                                 if (fc.getResult().getSize() > 32 * 1024 * 1024 ||
                                     !safe->singer.load(fc.getResult().loadFileAsString(), 0)) {
                                     safe->status.setText("Select an OSTF score under 32 MB with pitched notes.",
                                                          juce::dontSendNotification);
                                     return;
                                 }
                                 safe->refresh();
                             });
    };
    importVoice.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Choose a Japanese CV voice folder (kana WAV files or oto.ini)");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<Editor>(this)](const auto& fc) {
                if (!safe || !fc.getResult().isDirectory()) return;
                juce::String error;
                if (safe->singer.setVoice(fc.getResult(), error)) safe->refresh();
                else safe->voiceStatus.setText(error, juce::dontSendNotification);
            });
    };
    factory.onClick = [this] { juce::String error; singer.setVoice({}, error); refresh(); };
    setSize(560, singer.studioHost ? 145 : 180);
}
void Editor::refresh() {
    int index;
    const auto names = singer.trackNames(index);
    parts.clear(juce::dontSendNotification);
    parts.addItem("Studio track / MIDI input", 1);
    parts.addItemList(names, 2);
    parts.setSelectedId(index + 2, juce::dontSendNotification);
    status.setText(singer.studioHost ? "Studio track  /  singing the track's notes and lyrics" :
        "MIDI input or OSTF score playback", juce::dontSendNotification);
    voiceStatus.setText(singer.voiceName(), juce::dontSendNotification);
    auto image = loadPortrait(singer.voicePortrait());
    portrait.setImage(image, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    portrait.setVisible(image.isValid());
    resized();
}
void Editor::resized() {
    auto r = getLocalBounds().reduced(12);
    if (portrait.isVisible()) { portrait.setBounds(r.removeFromLeft(94)); r.removeFromLeft(8); }
    status.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);
    auto row = r.removeFromTop(singer.studioHost ? 0 : 32);
    open.setBounds(row.removeFromLeft(115));
    row.removeFromLeft(6);
    parts.setBounds(row);
    r.removeFromTop(7);
    voiceStatus.setBounds(r.removeFromTop(30));
    auto voices = r.removeFromTop(30);
    importVoice.setBounds(voices.removeFromLeft(160));
    voices.removeFromLeft(6);
    factory.setBounds(voices.removeFromLeft(156));
}
} // namespace
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Singer; }
