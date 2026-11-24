#include "../ui/ConsoleLookAndFeel.h"
#include "EditorProgramParameter.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <GMPreviewData.h>
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include <array>

namespace {
juce::CriticalSection& sharedBankLock() {
    static juce::CriticalSection lock;
    return lock;
}

void closeBank(tsf* bank) {
    const juce::ScopedLock lock(sharedBankLock());
    tsf_close(bank);
}

using Bank = std::unique_ptr<tsf, decltype(&closeBank)>;

// The sample and region tables are immutable and shared between factory-bank instances.

Bank copyFactoryBank() {
    const juce::ScopedLock lock(sharedBankLock());
    static const Bank factory([] {
        const auto module = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto file = module.getParentDirectory().getParentDirectory().getChildFile("Resources/FluidR3_GM.sf2");
        juce::MemoryBlock bytes;
        const auto length = file.getSize();
        return length > 0 && length <= 256 * 1024 * 1024 && file.loadFileAsData(bytes)
            ? tsf_load_memory(bytes.getData(), static_cast<int>(bytes.getSize())) : nullptr;
    }(), closeBank);
    return Bank(tsf_copy(factory.get()), closeBank);
}

class SoundModule final : public juce::AudioProcessor {
public:
    enum Control { volume, pan, expression, tuning, bendRange, sustain, mute, solo, controlCount };
    static constexpr std::array<int, controlCount> defaults {100, 64, 127, 0, 2, 0, 0, 0};
    static constexpr std::array<int, controlCount> minimum {0, 0, 0, -100, 0, 0, 0, 0};
    static constexpr std::array<int, controlCount> maximum {127, 127, 127, 100, 24, 1, 1, 1};

    SoundModule() : AudioProcessor(BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true)) {
        for (int channel = 0; channel < 16; ++channel)
            for (int control = 0; control < controlCount; ++control) setControl(channel, control, defaults[control]);
        for (int ch = 0; ch < 16; ++ch) {
            const auto n = juce::String(ch + 1);
            addParameter(editorPrograms_[ch] = new ostfstudio::EditorProgramParameter("gmProgram" + n, "MIDI Program Ch " + n, programs_[ch], 128));
        }
        resetBanks();
        loadFactoryBank();
    }

    const juce::String getName() const override { return "OSTF GM Sound Module"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    bool loadBank(const juce::File& file) {
        juce::MemoryBlock bytes;
        if (!file.hasFileExtension("sf2") || file.getSize() <= 0 || file.getSize() > 256 * 1024 * 1024
            || !file.loadFileAsData(bytes)) return false;
        return loadBankBytes(bytes.getData(), static_cast<int>(bytes.getSize()), file);
    }

    bool loadFactoryBank() {
        auto factory = copyFactoryBank();
        if (factory) return installBank(std::move(factory), {}, "FluidR3 GM");
        return loadBankBytes(GMPreviewData::StudioPreview_sf2, GMPreviewData::StudioPreview_sf2Size, {});
    }

    bool loadBankBytes(const void* bytes, int size, const juce::File& file) {
        return installBank(Bank(tsf_load_memory(bytes, size), closeBank), file,
                           file == juce::File{} ? "Studio Preview GM" : file.getFileName());
    }

    bool installBank(Bank next, const juce::File& file, const juce::String& name) {
        if (!next || !tsf_set_max_voices(next.get(), 128)) return false;
        for (int channel = 0; channel < 16; ++channel)
            if (!tsf_channel_set_presetindex(next.get(), channel, 0)) return false;
        juce::StringArray names, kits;
        for (int i = 0; i < tsf_get_presetcount(next.get()); ++i)
            names.add(juce::String::fromUTF8(tsf_get_presetname(next.get(), i)));
        for (int i = 0; i < 128; ++i)
            kits.add(names[tsf_get_presetindex(next.get(), 128, i)]);
        const juce::ScopedLock lock(bankLock_);
        tsf_set_output(next.get(), TSF_STEREO_INTERLEAVED, sampleRate_, -12.0f);
        bank_.swap(next);
        file_ = file;
        {
            const juce::ScopedLock metadata(metadataLock_);
            presetNames_.swapWith(names);
            kitNames_.swapWith(kits);
            bankName_ = name;
            for (int ch = 0; ch < 16; ++ch) selectProgram(ch, program(ch));
        }
        return true;
    }

    juce::String bankName() const {
        const juce::ScopedLock lock(metadataLock_);
        return bankName_.isEmpty() ? "No SoundFont loaded" : bankName_;
    }

    void chooseProgram(int ch, int index) {
        editorPrograms_[ch]->choose(index);
        const juce::ScopedLock lock(bankLock_);
        if (bank_) selectProgram(ch, program(ch));
    }
    float peakLevel(int ch) const { return peaks_[ch].load(std::memory_order_relaxed); }
    int program(int channel) const { return programs_[channel].load(); }
    void setProgram(int channel, int program) { programs_[channel] = program; }
    int control(int channel, int id) const { return controls_[channel][id].load(); }
    void setControl(int channel, int id, int value) {
        controls_[channel][id] = juce::jlimit(minimum[id], maximum[id], value);
    }
    bool hasActivity(int channel) const {
        const auto last = activity_[channel].load();
        return (soundingMask_.load() & (1u << channel)) != 0 || (last != 0 && juce::Time::getMillisecondCounter() - last < 350);
    }
    juce::String presetName(int channel) const {
        const juce::ScopedLock lock(metadataLock_);
        return presetNames_[displayPreset_[channel].load()];
    }
    bool isDrum(int ch) const {
        return drumMode_[ch].load();
    }
    juce::String drumKitName(int program) const {
        const juce::ScopedLock lock(metadataLock_);
        return kitNames_[program];
    }

    void prepareToPlay(double rate, int) override {
        const juce::ScopedLock lock(bankLock_);
        sampleRate_ = juce::roundToInt(rate);
        if (bank_) tsf_set_output(bank_.get(), TSF_STEREO_INTERLEAVED, sampleRate_, -12.0f);
    }
    void releaseResources() override { reset(); }
    void reset() override {
        const juce::ScopedLock lock(bankLock_);
        soundingMask_ = 0;
        for (auto& peak : peaks_) peak = 0;
        for (auto& stamp : activity_) stamp = 0;
        if (!bank_) return;
        for (int channel = 0; channel < 16; ++channel) tsf_channel_sounds_off_all(bank_.get(), channel);
        // TinySoundFont's sounds-off uses a short release. Consume it before restarting.
        for (int remaining = sampleRate_ / 40; remaining > 0;) {
            const auto count = std::min(512, remaining);
            tsf_render_float(bank_.get(), scratch_.data(), count, 0);
            remaining -= count;
        }
    }

    void getStateInformation(juce::MemoryBlock& block) override {
        const juce::ScopedLock lock(bankLock_);
        juce::XmlElement state("OSTF_GM");
        state.setAttribute("bank", file_.getFullPathName());
        state.setAttribute("bankName", bankName_);
        state.setAttribute("masterVolume", masterVolume_);
        state.setAttribute("masterFine", masterFine_);
        state.setAttribute("masterCoarse", masterCoarse_);
        for (int i = 0; i < 16; ++i) {
            state.setAttribute("p" + juce::String(i), program(i));
            auto* part = state.createNewChildElement("part");
            part->setAttribute("channel", i);
            part->setAttribute("bankMsb", active_[i].msb);
            part->setAttribute("bankLsb", active_[i].lsb);
            part->setAttribute("nextMsb", pending_[i].msb);
            part->setAttribute("nextLsb", pending_[i].lsb);
            part->setAttribute("coarse", rpn_[i].coarse);
            part->setAttribute("bendCents", rpn_[i].bendCents);
            for (int id = 0; id < controlCount; ++id) part->setAttribute("c" + juce::String(id), control(i, id));
        }
        copyXmlToBinary(state, block);
    }
    void setStateInformation(const void* data, int size) override {
        const auto state = getXmlFromBinary(data, size);
        if (!state || !state->hasTagName("OSTF_GM")) return;
        {
            const juce::ScopedLock lock(bankLock_);
            resetBanks();
            masterVolume_ = juce::jlimit(0, 16383, state->getIntAttribute("masterVolume", 16383));
            masterFine_ = juce::jlimit(0, 16383, state->getIntAttribute("masterFine", 8192));
            masterCoarse_ = juce::jlimit(0, 127, state->getIntAttribute("masterCoarse", 64));
            for (int i = 0; i < 16; ++i) {
                setProgram(i, juce::jlimit(0, 127, state->getIntAttribute("p" + juce::String(i))));
                for (int id = 0; id < controlCount; ++id) setControl(i, id, defaults[id]);
            }
            for (const auto* part : state->getChildIterator()) {
                const int channel = part->getIntAttribute("channel", -1);
                if (!part->hasTagName("part") || channel < 0 || channel >= 16) continue;
                auto value = [&](const char* name, int fallback) { return juce::jlimit(0, 127, part->getIntAttribute(name, fallback)); };
                active_[channel] = {value("bankMsb", channel == 9 ? 120 : 121), value("bankLsb", 0)};
                pending_[channel] = {value("nextMsb", active_[channel].msb), value("nextLsb", active_[channel].lsb)};
                rpn_[channel].coarse = value("coarse", 64);
                rpn_[channel].bendCents = value("bendCents", 0);
                for (int id = 0; id < controlCount; ++id)
                    setControl(channel, id, part->getIntAttribute("c" + juce::String(id), defaults[id]));
            }
        }
        const auto path = state->getStringAttribute("bank");
        if (juce::File::isAbsolutePath(path) && loadBank(juce::File(path))) return;
        {
            const juce::ScopedLock lock(bankLock_);
            if (bank_ && file_ == juce::File{}) {
                for (int ch = 0; ch < 16; ++ch) selectProgram(ch, program(ch));
                return;
            }
        }
        loadFactoryBank();
    }

    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        audio.clear();
        const juce::ScopedTryLock lock(bankLock_);
        if (!lock.isLocked() || !bank_ || audio.getNumChannels() < 2) return;
        for (int channel = 0; channel < 16; ++channel) {
            const auto next = program(channel);
            if (next != current_[channel]) selectProgram(channel, next);
            applyControls(channel);
        }
        blockPeaks_.fill(0.f);
        int rendered = 0;
        for (const auto event : midi) {
            const auto end = juce::jlimit(rendered, audio.getNumSamples(), event.samplePosition);
            render(audio, rendered, end);
            rendered = end;
            applyMidi(event);
        }
        render(audio, rendered, audio.getNumSamples());
        unsigned sounding = 0;
        for (int i = 0; i < bank_->voiceNum; ++i) {
            const auto& voice = bank_->voices[i];
            if (voice.playingPreset >= 0 && juce::isPositiveAndBelow(voice.playingChannel, 16))
                sounding |= 1u << voice.playingChannel;
        }
        soundingMask_ = sounding;
        const float decay = std::exp(-5.f * audio.getNumSamples() / sampleRate_);
        for (int ch = 0; ch < 16; ++ch)
            peaks_[ch].store(std::max(blockPeaks_[ch], peakLevel(ch) * decay), std::memory_order_relaxed);
    }

private:
    struct BankAddress { int msb{}, lsb{}; };
    void resetBanks() {
        for (int i = 0; i < 16; ++i) pending_[i] = active_[i] = {i == 9 ? 120 : 121, 0};
        current_.fill(-1);
        rpn_.fill({});
    }

    int presetIndex(int channel, int program) const {
        const auto address = active_[channel];
        const bool drums = (channel == 9 || channel == 10) && address.msb == 120;
        int bank = drums ? 128 : address.lsb;
        if (!drums && file_ == juce::File{} && address.msb == 121 && address.lsb != 0) {
            // Match GM2 variations to their FluidR3 SF2 banks. Missing variations use the base timbre.
            bank = 0;
            if (address.lsb == 1) {
                switch (program) {
                    case 4: case 5: case 16: case 17: case 24: case 25: case 28: case 30: case 31:
                    case 38: case 39: case 40: case 115: case 116: case 117: case 118: bank = 8; break;
                }
            } else if (address.lsb == 2 && program == 25) bank = 16;
            else if (address.lsb == 2 && program == 80) bank = 8;
        }
        auto preset = tsf_get_presetindex(bank_.get(), bank, program);
        if (preset < 0) preset = tsf_get_presetindex(bank_.get(), drums ? 128 : 0, drums ? 0 : program);
        return preset;
    }

    void systemOn() {
        resetBanks();
        masterVolume_ = 16383;
        masterFine_ = 8192;
        masterCoarse_ = 64;
        for (int ch = 0; ch < 16; ++ch) {
            tsf_channel_sounds_off_all(bank_.get(), ch);
            tsf_channel_midi_control(bank_.get(), ch, 121, 0);
            tsf_channel_set_pitchwheel(bank_.get(), ch, 8192);
            for (int id = 0; id <= sustain; ++id) setControl(ch, id, defaults[id]);
            setProgram(ch, 0);
            selectProgram(ch, 0);
            applyControls(ch);
        }
    }

    void applyControls(int channel) {
        bool anySolo = false;
        for (int part = 0; part < 16; ++part) anySolo = anySolo || control(part, solo) != 0;
        const bool audible = control(channel, mute) == 0 && (!anySolo || control(channel, solo) != 0);
        const auto level = control(channel, volume) * control(channel, expression) / 16129.0f;
        tsf_channel_set_volume(bank_.get(), channel, audible ? level * level : 0.0f);
        tsf_channel_set_pan(bank_.get(), channel, control(channel, pan) / 127.0f);
        tsf_channel_set_tuning(bank_.get(), channel, control(channel, tuning) / 100.0f
                               + (rpn_[channel].coarse - 64) + (masterCoarse_ - 64) + (masterFine_ - 8192) / 8192.f);
        tsf_channel_set_pitchrange(bank_.get(), channel, control(channel, bendRange) + rpn_[channel].bendCents / 100.f);
        tsf_channel_set_sustain(bank_.get(), channel, control(channel, sustain));
    }

    void selectProgram(int channel, int program) {
        const auto preset = presetIndex(channel, program);
        current_[channel] = program;
        displayPreset_[channel] = preset;
        drumMode_[channel] = (channel == 9 || channel == 10) && active_[channel].msb == 120;
        valid_[channel] = preset >= 0;
        if (preset >= 0) tsf_channel_set_presetindex(bank_.get(), channel, preset);
    }

    void applyMidi(juce::MidiMessageMetadata event) {
        if (event.numBytes < 2) return;
        const int status = event.data[0];
        const int channel = status & 15;
        const int first = event.data[1];
        const int second = event.numBytes >= 3 ? event.data[2] : 0;
        auto* bank = bank_.get();
        if (status == 0xf0 && event.data[event.numBytes - 1] == 0xf7 && event.numBytes >= 6
            && (event.data[2] == 0x7f || event.data[2] == 0)) {
            if (event.numBytes == 6 && first == 0x7e && event.data[3] == 9
                && (event.data[4] == 1 || event.data[4] == 3)) systemOn();
            if (event.numBytes == 8 && first == 0x7f && event.data[3] == 4) {
                const int value = (event.data[5] & 127) | ((event.data[6] & 127) << 7);
                if (event.data[4] == 1) masterVolume_ = value;
                if (event.data[4] == 3 || event.data[4] == 4) {
                    if (event.data[4] == 3) masterFine_ = value;
                    else masterCoarse_ = value >> 7;
                    for (int i = 0; i < 16; ++i) applyControls(i);
                }
            }
            return;
        }
        switch (status & 0xf0) {
            case 0x80: tsf_channel_note_off(bank, channel, first); break;
            case 0x90:
                if (second == 0) tsf_channel_note_off(bank, channel, first);
                else if (valid_[channel]) {
                    activity_[channel] = juce::Time::getMillisecondCounter();
                    tsf_channel_note_on(bank, channel, first, second / 127.0f);
                }
                break;
            case 0xb0:
                switch (first) {
                    case 0: pending_[channel].msb = second; return;
                    case 32: pending_[channel].lsb = second; return;
                    case 101: rpn_[channel].msb = second; return;
                    case 100: rpn_[channel].lsb = second; return;
                    case 98: case 99: rpn_[channel].msb = rpn_[channel].lsb = 127; return;
                    case 6: case 38: {
                        auto& edit = rpn_[channel];
                        if (edit.msb != 0) return;
                        if (edit.lsb == 0) {
                            if (first == 6) setControl(channel, bendRange, second);
                            else edit.bendCents = std::min(99, second);
                        } else if (edit.lsb == 1) {
                            edit.fine = first == 6 ? (second << 7) | (edit.fine & 127) : (edit.fine & 16256) | second;
                            setControl(channel, tuning, juce::roundToInt((edit.fine - 8192) * 100.0 / 8192));
                        } else if (edit.lsb == 2 && first == 6) edit.coarse = second;
                        break;
                    }
                    case 7: setControl(channel, volume, second); break;
                    case 10: setControl(channel, pan, second); break;
                    case 11: setControl(channel, expression, second); break;
                    case 64: setControl(channel, sustain, second >= 64); break;
                    case 121:
                        setControl(channel, expression, defaults[expression]);
                        setControl(channel, sustain, 0);
                        rpn_[channel].msb = rpn_[channel].lsb = 127;
                        tsf_channel_set_pitchwheel(bank, channel, 8192);
                        tsf_channel_midi_control(bank, channel, first, second);
                        break;
                    default:
                        tsf_channel_midi_control(bank, channel, first, second);
                        return;
                }
                applyControls(channel);
                break;
            case 0xc0:
                active_[channel] = pending_[channel];
                // Older GM scores often send bank 0 to every part, including the standard drum channel.
                if (channel == 9 && active_[channel].msb == 0) active_[channel].msb = 120;
                setProgram(channel, first);
                selectProgram(channel, first);
                break;
            case 0xe0: tsf_channel_set_pitchwheel(bank, channel, first | (second << 7)); break;
            default: break;
        }
    }

    void render(juce::AudioBuffer<float>& audio, int start, int end) {
        const float master = masterVolume_ / 16383.f;
        while (start < end) {
            const auto count = std::min(512, end - start);
            unsigned used = 0;
            for (int i = 0; i < bank_->voiceNum; ++i) {
                auto& voice = bank_->voices[i];
                if (voice.playingPreset < 0 || !juce::isPositiveAndBelow(voice.playingChannel, 16)) continue;
                const int ch = voice.playingChannel;
                auto* stereo = partAudio_[ch].data();
                if (!(used & (1u << ch))) std::fill_n(stereo, count * 2, 0.f);
                used |= 1u << ch;
                tsf_voice_render(bank_.get(), &voice, stereo, count);
            }
            for (int ch = 0; ch < 16; ++ch) if (used & (1u << ch)) {
                const auto* stereo = partAudio_[ch].data();
                for (int side = 0; side < 2; ++side) {
                    auto* out = audio.getWritePointer(side, start);
                    for (int i = 0; i < count; ++i) {
                        const float sample = stereo[2 * i + side] * master;
                        blockPeaks_[ch] = std::max(blockPeaks_[ch], std::abs(sample));
                        out[i] += sample;
                    }
                }
            }
            start += count;
        }
    }

    mutable juce::CriticalSection bankLock_;
    Bank bank_ {nullptr, closeBank};
    juce::File file_;
    juce::CriticalSection metadataLock_;
    juce::StringArray presetNames_, kitNames_;
    std::array<std::atomic<int>,16> displayPreset_{};
    std::array<std::atomic<bool>,16> drumMode_{};
    std::atomic<unsigned> soundingMask_{0};
    juce::String bankName_;
    std::array<std::atomic<int>, 16> programs_ {};
    std::array<ostfstudio::EditorProgramParameter*, 16> editorPrograms_{};
    std::array<std::array<float, 1024>, 16> partAudio_{};
    std::array<float, 16> blockPeaks_{};
    std::array<std::atomic<float>, 16> peaks_{};
    std::array<std::array<std::atomic<int>, controlCount>, 16> controls_ {};
    std::array<std::atomic<juce::uint32>, 16> activity_ {};
    std::array<BankAddress, 16> active_{}, pending_{};
    struct RegisteredParameter { int msb = 127, lsb = 127, fine = 8192, coarse = 64, bendCents = 0; };
    std::array<RegisteredParameter, 16> rpn_{};
    int masterVolume_ = 16383, masterFine_ = 8192, masterCoarse_ = 64;
    std::array<int, 16> current_ {};
    std::array<bool, 16> valid_ {};
    std::array<float, 1024> scratch_ {};
    int sampleRate_ = 44100;
};

class SoundModuleEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SoundModuleEditor(SoundModule& module) : AudioProcessorEditor(module), module_(module) {
        setLookAndFeel(&consoleStyle_);
        for (auto* item : std::array<juce::Component*, 7>{&bank_, &load_, &factory_, &program_, &preset_, &part_, &heading_})
            addAndMakeVisible(*item);
        heading_.setText("OSTF GM Sound Module", juce::dontSendNotification);
        heading_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        for (int i = 0; i < 16; ++i) {
            auto& button = channels_[i];
            button.setTitle("Select MIDI channel " + juce::String(i + 1));
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263541));
            button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff27756e));
            button.onClick = [this, i] { selected_ = i; refreshPrograms(); timerCallback(); };
            addAndMakeVisible(button);
        }
        constexpr const char* labels[]{"Volume", "Pan", "Expression", "Tuning", "Bend range", "Sustain", "Mute", "Solo"};
        for (int id = 0; id < SoundModule::controlCount; ++id) {
            auto& control = controls_[id];
            control.label.setText(labels[id], juce::dontSendNotification);
            control.label.setJustificationType(juce::Justification::centred);
            control.slider.setTitle(labels[id]);
            control.slider.setRange(SoundModule::minimum[id], SoundModule::maximum[id], 1);
            control.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 18);
            if (id == SoundModule::tuning) control.slider.setTextValueSuffix(" cents");
            if (id == SoundModule::bendRange) control.slider.setTextValueSuffix(" st");
            control.slider.setDoubleClickReturnValue(true, SoundModule::defaults[id]);
            control.slider.onValueChange = [this, id] { module_.setControl(selected_, id, static_cast<int>(controls_[id].slider.getValue())); };
            control.toggle.setButtonText(labels[id]);
            control.toggle.onClick = [this, id] { module_.setControl(selected_, id, controls_[id].toggle.getToggleState()); };
            if (id < SoundModule::sustain) {
                addAndMakeVisible(control.label);
                addAndMakeVisible(control.slider);
            } else {
                addAndMakeVisible(control.toggle);
            }
        }
        program_.onChange = [this] {
            if (program_.getSelectedId() > 0) module_.chooseProgram(selected_, program_.getSelectedId() - 1);
        };
        load_.onClick = [this] { chooseBank(); };
        factory_.onClick = [this] { module_.loadFactoryBank(); refreshPrograms(); timerCallback(); };
        refreshPrograms();
        timerCallback();
        startTimerHz(12);
        setSize(780, 493);
    }

    ~SoundModuleEditor() override { stopTimer(); setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff14212b));
        g.setColour(juce::Colour(0xff0d3030));
        g.fillRoundedRectangle(display_.toFloat(), 9.0f);
        g.setColour(juce::Colour(0xff407b74));
        g.drawRoundedRectangle(display_.toFloat().reduced(.5f), 9.0f, 1.0f);
        g.setColour(juce::Colour(0xff0c1c22));
        g.fillRoundedRectangle(meters_.toFloat(), 5.f);
        for (int ch = 0; ch < 16; ++ch) {
            auto strip = meters_.withLeft(meters_.getX() + ch * meters_.getWidth() / 16)
                                .withRight(meters_.getX() + (ch + 1) * meters_.getWidth() / 16).reduced(5, 4);
            g.setFont(12.f);
            g.setColour(juce::Colour(ch == selected_ ? 0xffffca76 : 0xff9bb9ba));
            g.drawText(juce::String(ch + 1), strip.removeFromBottom(17), juce::Justification::centred);
            const float db = juce::Decibels::gainToDecibels(module_.peakLevel(ch), -54.f);
            for (int i = 0; i < 12; ++i) {
                g.setColour(juce::Colour(db > -54.f + i * 4.5f ? (i >= 10 ? 0xffffbd62 : 0xff69dfb4) : 0xff22383b));
                g.fillRect(strip.getX(), strip.getBottom() - (i + 1) * 4, strip.getWidth(), 3);
            }
        }
        g.setColour(juce::Colour(0xff21323d));
        g.fillRoundedRectangle(panel_.toFloat(), 9.0f);
    }

    void paintOverChildren(juce::Graphics& g) override {
        for (int i = 0; i < 16; ++i) {
            const auto bounds = channels_[i].getBounds().toFloat().reduced(1);
            const auto top = channels_[i].getBounds().getTopRight();
            if (module_.hasActivity(i)) {
                g.setColour(juce::Colour(0xff87ffc6));
                g.drawRoundedRectangle(bounds, 5.f, 2.f);
            }
            g.setColour(juce::Colour(module_.hasActivity(i) ? 0xff87ffc6 : 0xff405059));
            g.fillEllipse(static_cast<float>(top.x - 12), static_cast<float>(top.y + 6), 5.0f, 5.0f);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16);
        auto top = area.removeFromTop(36);
        load_.setBounds(top.removeFromRight(114).reduced(3));
        factory_.setBounds(top.removeFromRight(112).reduced(3));
        heading_.setBounds(top);
        area.removeFromTop(7);
        display_ = area.removeFromTop(60);
        auto display = display_.reduced(12, 5);
        auto upper = display.removeFromTop(24);
        part_.setBounds(upper.removeFromLeft(165));
        bank_.setBounds(upper);
        preset_.setBounds(display.removeFromRight(265));
        program_.setBounds(display);
        area.removeFromTop(5);
        meters_ = area.removeFromTop(78);
        area.removeFromTop(9);
        for (int row = 0; row < 2; ++row) {
            auto line = area.removeFromTop(64);
            const int width = line.getWidth() / 8;
            for (int i = 0; i < 8; ++i) channels_[row * 8 + i].setBounds(line.removeFromLeft(width).reduced(3, 4));
        }
        area.removeFromTop(8);
        panel_ = area;
        area.reduce(10, 4);
        auto toggles = area.removeFromRight(132).reduced(10, 4);
        for (int id = SoundModule::sustain; id < SoundModule::controlCount; ++id)
            controls_[id].toggle.setBounds(toggles.removeFromTop(29));
        const int width = area.getWidth() / SoundModule::sustain;
        for (int id = 0; id < SoundModule::sustain; ++id) {
            auto cell = area.removeFromLeft(width).reduced(4);
            controls_[id].label.setBounds(cell.removeFromTop(22));
            controls_[id].slider.setBounds(cell);
        }
    }

private:
    void refreshPrograms() {
        showingDrums_ = module_.isDrum(selected_);
        program_.clear(juce::dontSendNotification);
        for (int i = 0; i < 128; ++i) {
            const auto name = showingDrums_ ? module_.drumKitName(i) : juce::String(juce::MidiMessage::getGMInstrumentName(i));
            if (name.isNotEmpty()) program_.addItem(juce::String(i + 1).paddedLeft('0', 3) + "  " + name, i + 1);
        }
        program_.setTitle("GM program for the selected MIDI channel");
    }

    void chooseBank() {
        chooser_ = std::make_unique<juce::FileChooser>("Open GM SoundFont", juce::File{}, "*.sf2");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<SoundModuleEditor>(this)](const juce::FileChooser& picker) {
                if (!safe || picker.getResult() == juce::File{}) return;
                if (!safe->module_.loadBank(picker.getResult()))
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "SoundFont", "Could not load SoundFont.");
                safe->refreshPrograms();
                safe->timerCallback();
            });
    }

    void timerCallback() override {
        if (showingDrums_ != module_.isDrum(selected_)) refreshPrograms();
        for (int i = 0; i < 16; ++i) {
            auto name = module_.presetName(i);
            const auto tooltip = name;
            if (name.length() > 15) name = name.substring(0, 13) + "...";
            channels_[i].setButtonText(juce::String(i + 1).paddedLeft('0', 2) + "\n" + name);
            channels_[i].setTooltip(tooltip);
            channels_[i].setToggleState(i == selected_, juce::dontSendNotification);
        }
        part_.setText("EDIT CH " + juce::String(selected_ + 1) + (showingDrums_ ? " / PERCUSSION" : ""),
                      juce::dontSendNotification);
        juce::StringArray sounding;
        for (int i = 0; i < 16; ++i) if (module_.hasActivity(i)) sounding.add(juce::String(i + 1));
        bank_.setText(module_.bankName() + "     Playing Ch: " + (sounding.isEmpty() ? "-" : sounding.joinIntoString(", ")), juce::dontSendNotification);
        const auto preset = module_.presetName(selected_);
        preset_.setText(preset.isNotEmpty() ? "Bank patch: " + preset : "Patch unavailable", juce::dontSendNotification);
        program_.setSelectedId(module_.program(selected_) + 1, juce::dontSendNotification);
        for (int id = 0; id < SoundModule::controlCount; ++id) {
            auto& control = controls_[id];
            if (!control.slider.isMouseButtonDown()) control.slider.setValue(module_.control(selected_, id), juce::dontSendNotification);
            control.toggle.setToggleState(module_.control(selected_, id) != 0, juce::dontSendNotification);
        }
        repaint();
    }

    struct PartControl { juce::Label label; juce::Slider slider; juce::ToggleButton toggle; };
    ostfstudio::console::LookAndFeel consoleStyle_ {ostfstudio::console::finishFor(ostfstudio::console::Theme::harmonic)};
    SoundModule& module_;
    bool showingDrums_ = false;
    std::array<juce::TextButton, 16> channels_;
    std::array<PartControl, SoundModule::controlCount> controls_;
    juce::Label heading_, bank_, part_, preset_;
    juce::ComboBox program_;
    juce::TextButton load_ {"Load SoundFont"}, factory_ {"Factory bank"};
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Rectangle<int> display_, meters_, panel_;
    int selected_ = 0;
};

juce::AudioProcessorEditor* SoundModule::createEditor() { return new SoundModuleEditor(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SoundModule(); }
