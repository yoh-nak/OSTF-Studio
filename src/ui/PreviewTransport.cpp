#include "ui/PreviewTransport.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

PreviewTransport::~PreviewTransport() { stop(); }

juce::Result PreviewTransport::start(juce::AudioDeviceManager& devices, const ostfstudio::OstfpPreview& project, bool record) {
    stop();
    auto* device = devices.getCurrentAudioDevice();
    if (!device || !device->isOpen() || device->getActiveOutputChannels().isZero())
        return juce::Result::fail("Select an active audio output in Settings first.");
    if (!std::isfinite(project.previewBpm) || project.previewBpm <= 0.0)
        return juce::Result::fail("Preview transport requires a single tempo.");
    if (record) {
        bool enabled = false;
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            enabled = enabled || devices.isMidiInputDeviceEnabled(input.identifier);
        if (!enabled) return juce::Result::fail("Enable a MIDI input in Settings before recording.");
    }
    sampleRate_ = device->getCurrentSampleRate();
    samplesPerTick_ = sampleRate_ * 60.0 / (project.previewBpm * static_cast<double>(project.ppq));
    scoreTicks_ = static_cast<juce::int64>(project.bars.endBar - 1) * project.ticksPerBar;
    const auto total = static_cast<double>(scoreTicks_) * samplesPerTick_;
    if (!std::isfinite(total) || total < 1.0 || total > sampleRate_ * 3600.0)
        return juce::Result::fail("Preview transport supports songs up to one hour.");
    lastSample_ = static_cast<juce::int64>(std::ceil(total));
    tones_.clear();
    for (std::size_t track = 0; track < project.pitchedNotes.size(); ++track) {
        if (track >= gains_.size()) break;
        for (const auto& note : project.pitchedNotes[track]) {
            if (tones_.size() >= 20000) return juce::Result::fail("Preview transport supports up to 20,000 pitched notes.");
            const auto first = static_cast<juce::int64>(std::llround(static_cast<double>(note.startTick) * samplesPerTick_));
            const auto end = static_cast<juce::int64>(std::llround(static_cast<double>(note.endTick) * samplesPerTick_));
            if (end > first) tones_.push_back({first, end, 440.0 * std::exp2((note.pitch - 69) / 12.0), track});
        }
    }
    std::sort(tones_.begin(), tones_.end(), [](const Tone& a, const Tone& b) { return a.first < b.first; });
    pressed_.fill(-1);
    recorded_.clear();
    sample_ = 0;
    recording_ = record;
    devices_ = &devices;
    running_ = true;
    devices.addAudioCallback(this);
    if (record) devices.addMidiInputDeviceCallback({}, this);
    return juce::Result::ok();
}

std::vector<ostfstudio::PitchedNote> PreviewTransport::stop() {
    running_ = false;
    if (devices_) {
        if (recording_) devices_->removeMidiInputDeviceCallback({}, this);
        devices_->removeAudioCallback(this);
        devices_ = nullptr;
    }
    const auto tick = positionTick();
    const std::scoped_lock lock(recordingMutex_);
    if (recording_) {
        for (std::size_t key = 0; key < pressed_.size(); ++key)
            if (pressed_[key] >= 0 && tick > pressed_[key])
                recorded_.push_back({pressed_[key], tick, static_cast<int>(key % 128)});
    }
    recording_ = false;
    sample_ = 0;
    return std::exchange(recorded_, {});
}

juce::int64 PreviewTransport::positionTick() const {
    if (samplesPerTick_ <= 0) return 0;
    return std::min(scoreTicks_, static_cast<juce::int64>(static_cast<double>(sample_.load()) / samplesPerTick_));
}

void PreviewTransport::setChannelGain(std::size_t track, ostfstudio::MixGains gains) {
    if (track < gains_.size()) {
        gains_[track].master = gains.master;
        gains_[track].cue = gains.cue;
    }
}

void PreviewTransport::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device->getCurrentSampleRate() != sampleRate_) running_ = false;
}

void PreviewTransport::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output, int channels,
                                                        int count, const juce::AudioIODeviceCallbackContext&) {
    for (int c = 0; c < channels; ++c)
        if (output[c]) juce::FloatVectorOperations::clear(output[c], count);
    if (!running_) return;
    const auto first = sample_.load();
    const auto end = std::min(lastSample_, first + count);
    for (const auto& tone : tones_) {
        if (tone.first >= end) break;
        const auto begin = std::max(first, tone.first);
        const auto finish = std::min(end, tone.end);
        const auto master = gains_[tone.track].master.load() * 0.075f;
        const auto cue = channels >= 4 ? gains_[tone.track].cue.load() * 0.075f : 0.0f;
        if (begin >= finish || (master == 0.0f && cue == 0.0f)) continue;
        const auto phaseStep = 2.0 * std::numbers::pi * tone.frequency / sampleRate_;
        for (auto sample = begin; sample < finish; ++sample) {
            const auto edge = std::min(sample - tone.first, tone.end - sample - 1);
            const auto envelope = static_cast<float>(std::min<juce::int64>(128, edge)) / 128.0f;
            const auto wave = static_cast<float>(std::sin(static_cast<double>(sample - tone.first) * phaseStep)) * envelope;
            for (int c = 0; c < std::min(2, channels); ++c)
                if (output[c]) output[c][sample - first] += wave * master;
            if (channels >= 4)
                for (int c = 2; c < 4; ++c)
                    if (output[c]) output[c][sample - first] += wave * cue;
        }
    }
    for (int c = 0; c < channels; ++c)
        if (output[c])
            for (int i = 0; i < count; ++i) output[c][i] = juce::jlimit(-1.0f, 1.0f, output[c][i]);
    sample_ = end;
    if (end == lastSample_) running_ = false;
}

void PreviewTransport::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (!running_ || (!message.isNoteOn() && !message.isNoteOff())) return;
    const auto tick = positionTick();
    const auto key = (message.getChannel() - 1) * 128 + message.getNoteNumber();
    const std::scoped_lock lock(recordingMutex_);
    auto& start = pressed_[static_cast<std::size_t>(key)];
    if (start >= 0 && tick > start && recorded_.size() < 10000)
        recorded_.push_back({start, tick, message.getNoteNumber()});
    start = message.isNoteOn() ? tick : -1;
}
