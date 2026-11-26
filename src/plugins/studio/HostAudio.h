#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace ostfstudio {
// The VST wrapper supplies blocks to the same device callbacks used by Studio's
// transport, cell previews and mixer. This device never opens OS audio or MIDI.
struct HostAudio {
    std::function<void()> projectChanged;
    double rate = 44100;
    int block = 512;
    juce::CriticalSection lock;
    juce::AudioIODeviceCallback* callback = nullptr;
    static inline thread_local const juce::MidiBuffer* midi = nullptr;
    static inline thread_local int blockOffset = 0;

    void render(juce::AudioBuffer<float>& audio, const juce::MidiBuffer& messages,
                double hostRate) {
        audio.clear();
        const juce::ScopedTryLock guard(lock);
        if (!guard.isLocked() || !callback || hostRate != rate || audio.getNumChannels() != 2) return;
        const juce::ScopedValueSetter<const juce::MidiBuffer*> events(midi, &messages);
        const juce::ScopedValueSetter<int> offset(blockOffset, 0);
        for (int pos = 0; pos < audio.getNumSamples(); pos += block) {
            float* outputs[]{audio.getWritePointer(0, pos), audio.getWritePointer(1, pos)};
            blockOffset = pos;
            callback->audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2,
                std::min(block, audio.getNumSamples() - pos), {});
        }
    }
};

class DawOutput final : public juce::AudioIODevice {
public:
    explicit DawOutput(HostAudio& io) : AudioIODevice("DAW", "DAW"), host(io) {}
    ~DawOutput() override { close(); }
    juce::StringArray getOutputChannelNames() override { return {"Left", "Right"}; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return {host.rate}; }
    juce::Array<int> getAvailableBufferSizes() override { return {host.block}; }
    int getDefaultBufferSize() override { return host.block; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { opened = true; return {}; }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start(juce::AudioIODeviceCallback* cb) override {
        stop();
        if (!cb) return;
        cb->audioDeviceAboutToStart(this);
        const juce::ScopedLock guard(host.lock);
        host.callback = cb;
    }
    void stop() override {
        juce::AudioIODeviceCallback* old;
        { const juce::ScopedLock guard(host.lock); old = std::exchange(host.callback, nullptr); }
        if (old) old->audioDeviceStopped();
    }
    bool isPlaying() override { const juce::ScopedLock guard(host.lock); return host.callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return host.block; }
    double getCurrentSampleRate() override { return host.rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return juce::BigInteger(3); }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
private:
    HostAudio& host;
    bool opened = false;
};

class DawDeviceType final : public juce::AudioIODeviceType {
public:
    explicit DawDeviceType(HostAudio& io) : AudioIODeviceType("DAW"), host(io) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input) const override { return input ? juce::StringArray{} : juce::StringArray{"DAW"}; }
    int getDefaultDeviceIndex(bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice(juce::AudioIODevice* device, bool input) const override { return device && !input ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& output, const juce::String&) override {
        return output == "DAW" ? new DawOutput(host) : nullptr;
    }
private:
    HostAudio& host;
};
}
