#include <juce_audio_processors/juce_audio_processors.h>
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include <array>

namespace {
class SoundModule final : public juce::AudioProcessor {
public:
    SoundModule() : AudioProcessor(BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true)) {}

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
        Bank next(tsf_load_memory(bytes.getData(), static_cast<int>(bytes.getSize())), tsf_close);
        if (!next || !tsf_set_max_voices(next.get(), 128)) return false;
        for (int channel = 0; channel < 16; ++channel)
            if (!tsf_channel_set_presetindex(next.get(), channel, 0)) return false;
        const juce::ScopedLock lock(bankLock_);
        tsf_set_output(next.get(), TSF_STEREO_INTERLEAVED, sampleRate_, -12.0f);
        bank_.swap(next);
        file_ = file;
        current_.fill(-1);
        return true;
    }

    juce::String bankName() const {
        const juce::ScopedLock lock(bankLock_);
        return bank_ ? file_.getFileName() : "No SoundFont loaded";
    }

    int program(int channel) const { return programs_[channel].load(); }
    void setProgram(int channel, int program) { programs_[channel] = program; }

    void prepareToPlay(double rate, int) override {
        const juce::ScopedLock lock(bankLock_);
        sampleRate_ = juce::roundToInt(rate);
        if (bank_) tsf_set_output(bank_.get(), TSF_STEREO_INTERLEAVED, sampleRate_, -12.0f);
    }
    void releaseResources() override { reset(); }
    void reset() override {
        const juce::ScopedLock lock(bankLock_);
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
        for (int i = 0; i < 16; ++i) state.setAttribute("p" + juce::String(i), program(i));
        copyXmlToBinary(state, block);
    }
    void setStateInformation(const void* data, int size) override {
        const auto state = getXmlFromBinary(data, size);
        if (!state || !state->hasTagName("OSTF_GM")) return;
        for (int i = 0; i < 16; ++i) setProgram(i, juce::jlimit(0, 127, state->getIntAttribute("p" + juce::String(i))));
        const auto path = state->getStringAttribute("bank");
        if (juce::File::isAbsolutePath(path) && loadBank(juce::File(path))) return;
        Bank previous(nullptr, tsf_close);
        const juce::ScopedLock lock(bankLock_);
        previous.swap(bank_);
        file_ = juce::File{};
    }

    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        audio.clear();
        const juce::ScopedTryLock lock(bankLock_);
        if (!lock.isLocked() || !bank_ || audio.getNumChannels() < 2) return;
        for (int channel = 0; channel < 16; ++channel) {
            const auto next = program(channel);
            if (next != current_[channel]) selectProgram(channel, next);
        }
        int rendered = 0;
        for (const auto event : midi) {
            const auto end = juce::jlimit(rendered, audio.getNumSamples(), event.samplePosition);
            render(audio, rendered, end);
            rendered = end;
            applyMidi(event);
        }
        render(audio, rendered, audio.getNumSamples());
    }

private:
    using Bank = std::unique_ptr<tsf, decltype(&tsf_close)>;
    void selectProgram(int channel, int program) {
        const auto preset = tsf_get_presetindex(bank_.get(), channel == 9 ? 128 : 0, program);
        current_[channel] = program;
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
        switch (status & 0xf0) {
            case 0x80: tsf_channel_note_off(bank, channel, first); break;
            case 0x90:
                if (second == 0) tsf_channel_note_off(bank, channel, first);
                else if (valid_[channel]) tsf_channel_note_on(bank, channel, first, second / 127.0f);
                break;
            case 0xb0: tsf_channel_midi_control(bank, channel, first, second); break;
            case 0xc0: setProgram(channel, first); selectProgram(channel, first); break;
            case 0xe0: tsf_channel_set_pitchwheel(bank, channel, first | (second << 7)); break;
            default: break;
        }
    }

    void render(juce::AudioBuffer<float>& audio, int start, int end) {
        while (start < end) {
            const auto count = std::min(512, end - start);
            tsf_render_float(bank_.get(), scratch_.data(), count, 0);
            for (int channel = 0; channel < 2; ++channel) {
                auto* out = audio.getWritePointer(channel, start);
                for (int i = 0; i < count; ++i) out[i] = scratch_[2 * i + channel];
            }
            start += count;
        }
    }

    mutable juce::CriticalSection bankLock_;
    Bank bank_ {nullptr, tsf_close};
    juce::File file_;
    std::array<std::atomic<int>, 16> programs_ {};
    std::array<int, 16> current_ {};
    std::array<bool, 16> valid_ {};
    std::array<float, 1024> scratch_ {};
    int sampleRate_ = 44100;
};

class SoundModuleEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SoundModuleEditor(SoundModule& module) : AudioProcessorEditor(module), module_(module) {
        addAndMakeVisible(load_);
        addAndMakeVisible(status_);
        addAndMakeVisible(channel_);
        addAndMakeVisible(program_);
        status_.setText(module_.bankName(), juce::dontSendNotification);
        for (int i = 1; i <= 16; ++i) channel_.addItem(i == 10 ? "Channel 10 (drums)" : "Channel " + juce::String(i), i);
        for (int i = 1; i <= 128; ++i) program_.addItem("Program " + juce::String(i), i);
        channel_.setSelectedId(1);
        channel_.onChange = [this] { timerCallback(); };
        program_.onChange = [this] {
            module_.setProgram(channel_.getSelectedId() - 1, program_.getSelectedId() - 1);
        };
        load_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Open GM SoundFont", juce::File{}, "*.sf2");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<SoundModuleEditor>(this)](const juce::FileChooser& picker) {
                    if (!safe || picker.getResult() == juce::File{}) return;
                    safe->status_.setText(safe->module_.loadBank(picker.getResult()) ? safe->module_.bankName()
                                                                                    : "Could not load SoundFont",
                                          juce::dontSendNotification);
                });
        };
        timerCallback();
        startTimerHz(5);
        setSize(380, 112);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        auto top = area.removeFromTop(32);
        load_.setBounds(top.removeFromLeft(134).reduced(2));
        status_.setBounds(top.reduced(2));
        auto row = area.removeFromTop(36);
        channel_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
        program_.setBounds(row.reduced(2));
    }

private:
    void timerCallback() override {
        const auto channel = channel_.getSelectedId() - 1;
        if (channel >= 0) program_.setSelectedId(module_.program(channel) + 1, juce::dontSendNotification);
    }

    SoundModule& module_;
    juce::TextButton load_ {"Load GM SoundFont"};
    juce::Label status_;
    juce::ComboBox channel_, program_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

juce::AudioProcessorEditor* SoundModule::createEditor() { return new SoundModuleEditor(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SoundModule(); }
