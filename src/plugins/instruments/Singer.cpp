#include "SingingScore.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace {
using namespace ostfstudio::singing;

class Voice {
  public:
    int channel = -1, key = -1;
    void start(int ch, int pitch, float velocity, Phone p, double sr) {
        channel = ch;
        key = pitch;
        gain = velocity;
        phase = 0;
        envelope = 0;
        released = false;
        hz = juce::MidiMessage::getMidiNoteInHertz(pitch);
        for (auto& r : resonators)
            r = {};
        articulate(p, sr);
    }
    void articulate(Phone p, double sr) {
        constexpr float freq[5][3] = {
            {760, 1200, 2600}, {290, 2250, 3100}, {330, 850, 2300}, {490, 1880, 2650}, {490, 900, 2450}};
        constexpr float bandwidth[3] = {90, 120, 175};
        onset = p.onset;
        age = 0;
        for (int i = 0; i < 3; ++i) {
            const double pole = std::exp(-juce::MathConstants<double>::pi * bandwidth[i] / sr);
            auto& r = resonators[i];
            r.a = 2 * pole *
                  std::cos(juce::MathConstants<double>::twoPi * std::min(.44 * sr, double(freq[p.vowel][i])) / sr);
            r.b = -pole * pole;
            r.drive = (1 - pole) * (i == 0 ? 1.0 : .65);
        }
    }
    void stop() { released = true; }
    float sample(double sr) {
        if (key < 0)
            return 0;
        envelope = std::clamp(envelope + (released ? -95.0 : 110.0) / sr, 0.0, 1.0);
        if (released && envelope == 0) {
            key = channel = -1;
            return 0;
        }
        const double t = age++ / sr;
        phase += hz / sr;
        phase -= std::floor(phase);
        const double pulse = phase < .6 ? std::sin(phase / .6 * juce::MathConstants<double>::twoPi) : 0.0;
        noise = noise * 1664525u + 1013904223u;
        const double breath = (double(noise) / 2147483648.0 - 1.0);
        const auto attack = std::min(1.0, t / (onset == 1 ? .085 : .033));
        double input = pulse * ((onset == 1 || onset == 2) ? attack : 1.0) + breath * .02;
        if (onset == 1 || onset == 2)
            input += breath * (1 - attack) * .6;
        double out = 0;
        for (auto& r : resonators) {
            const auto v = r.drive * input + r.a * r.y1 + r.b * r.y2;
            r.y2 = r.y1;
            r.y1 = v;
            out += v;
        }
        return float(.45 * gain * envelope * std::tanh(out) * (onset == 3 ? .65 : 1.0));
    }

  private:
    struct Resonator {
        double a{}, b{}, drive{}, y1{}, y2{};
    };
    std::array<Resonator, 3> resonators{};
    double hz = 220, phase = 0, gain = 0, envelope = 0;
    unsigned noise = 1397;
    uint64_t age = 0;
    int onset = 0;
    bool released = false;
};
class Singer;
class Editor final : public juce::AudioProcessorEditor {
  public:
    explicit Editor(Singer&);
    void resized() override;

  private:
    Singer& singer;
    juce::TextButton open{"Load OSTF..."};
    juce::ComboBox parts;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    void refresh();
};
class Singer final : public juce::AudioProcessor {
  public:
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
    void getStateInformation(juce::MemoryBlock& b) override {
        juce::XmlElement xml("OSTFSinger");
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            xml.setAttribute("part", selected);
            xml.addTextElement(text);
        }
        copyXmlToBinary(xml, b);
    }
    void setStateInformation(const void* b, int n) override {
        if (auto xml = getXmlFromBinary(b, n); xml && xml->hasTagName("OSTFSinger")) {
            auto source = xml->getAllSubText();
            if (source.isNotEmpty())
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
                        voices[0].start(1, s.key, s.velocity / 127.f, s.phone, rate);
                    } else
                        voices[0].stop();
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
                    sample += v.sample(rate);
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
            articulation[p[4]][p[5]] = phone;
            for (auto& v : voices)
                if (v.channel == p[4] + 1 && v.key == p[5])
                    v.articulate(phone, rate);
        } else if (m.isNoteOn()) {
            auto* target = &voices[0];
            for (auto& v : voices) {
                if (v.key < 0 || (v.channel == m.getChannel() && v.key == m.getNoteNumber())) {
                    target = &v;
                    break;
                }
            }
            target->start(m.getChannel(), m.getNoteNumber(), m.getFloatVelocity(),
                          articulation[m.getChannel() - 1][m.getNoteNumber()], rate);
        } else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff()) {
            for (auto& v : voices)
                if (v.channel == m.getChannel() && (!m.isNoteOff() || v.key == m.getNoteNumber()))
                    v.stop();
        }
    }
    mutable juce::SpinLock lock;
    juce::String text;
    std::vector<Part> parts;
    int selected = -1, generation = 0, seen = -1, lastPart = -1, cursor = -1, lastIndex = -1;
    double rate = 48000, expected = -1;
    std::array<Voice, 16> voices{};
    std::array<std::array<Phone, 128>, 16> articulation{};
};
Editor::Editor(Singer& s) : AudioProcessorEditor(s), singer(s) {
    addAndMakeVisible(open);
    addAndMakeVisible(parts);
    addAndMakeVisible(status);
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
    setSize(480, 120);
}
void Editor::refresh() {
    int index;
    const auto names = singer.trackNames(index);
    parts.clear(juce::dontSendNotification);
    parts.addItem("Studio track / MIDI input", 1);
    parts.addItemList(names, 2);
    parts.setSelectedId(index + 2, juce::dontSendNotification);
    status.setText("Formant voice  /  OSTF lyrics and notes", juce::dontSendNotification);
}
void Editor::resized() {
    auto r = getLocalBounds().reduced(12);
    status.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);
    auto row = r.removeFromTop(32);
    open.setBounds(row.removeFromLeft(115));
    row.removeFromLeft(6);
    parts.setBounds(row);
}
} // namespace
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Singer; }
