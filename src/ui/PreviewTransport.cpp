#include "ui/PreviewTransport.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

PreviewTransport::~PreviewTransport() { stop(); }

bool PreviewTransport::supportsInstrument(const juce::AudioPluginInstance& instrument) {
    const auto outputs = instrument.getMainBusNumOutputChannels();
    const auto description = instrument.getPluginDescription();
    return description.isInstrument
        && (outputs == 1 || outputs == 2)
        && std::max(instrument.getTotalNumInputChannels(), instrument.getTotalNumOutputChannels()) <= 16;
}

bool PreviewTransport::supportsInsert(const juce::AudioPluginInstance& effect) {
    return !effect.getPluginDescription().isInstrument && effect.getTotalNumInputChannels() == 2
        && effect.getTotalNumOutputChannels() == 2;
}

juce::Result PreviewTransport::start(juce::AudioDeviceManager& devices, const ostfstudio::OstfpPreview& project,
                                    bool record, std::span<const TrackSetup> tracks, const Inserts& inserts) {
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
    const auto result = preparePlayback(project, tracks, inserts, device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    if (result.failed()) return result;
    recording_ = record;
    devices_ = &devices;
    devices.addAudioCallback(this);
    if (record) devices.addMidiInputDeviceCallback({}, this);
    return result;
}

juce::Result PreviewTransport::preparePlayback(const ostfstudio::OstfpPreview& project, std::span<const TrackSetup> tracks,
                                             const Inserts& inserts, double sampleRate, int blockSize) {
    stop();
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || blockSize < 1)
        return juce::Result::fail("Invalid audio configuration");
    sampleRate_ = sampleRate;
    bpm_ = project.previewBpm;
    samplesPerTick_ = sampleRate_ * 60.0 / (bpm_ * static_cast<double>(project.ppq));
    scoreTicks_ = static_cast<juce::int64>(project.bars.endBar - 1) * project.ticksPerBar;
    const auto total = static_cast<double>(scoreTicks_) * samplesPerTick_;
    if (!std::isfinite(total) || total < 1.0 || total > sampleRate_ * 3600.0
        || total > std::numeric_limits<int>::max() - 1)
        return juce::Result::fail("Preview transport supports songs up to one hour.");
    lastSample_ = static_cast<juce::int64>(std::ceil(total));
    int noteCount = 0;
    int outputChannels = 0;
    for (auto& slot : instruments_) {
        slot.timeline.clear();
        slot.wav.setSize(0, 0);
    }
    juce::WavAudioFormat wavFormat;
    for (const auto& audio : project.audio) {
        if (audio.track >= instruments_.size()) continue;
        auto stream = audio.embedded.isEmpty()
            ? std::unique_ptr<juce::InputStream>(audio.localFile.createInputStream().release())
            : std::make_unique<juce::MemoryInputStream>(audio.embedded, false);
        if (!stream) return juce::Result::fail("Missing WAV: " + audio.path);
        std::unique_ptr<juce::AudioFormatReader> reader(wavFormat.createReaderFor(stream.release(), true));
        if (!reader || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024
            || reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate <= 0)
            return juce::Result::fail("Unsupported WAV: " + audio.path);
        auto& slot = instruments_[audio.track];
        slot.wav.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        if (!reader->read(&slot.wav, 0, slot.wav.getNumSamples(), 0, true, true))
            return juce::Result::fail("Could not decode WAV: " + audio.path);
        slot.wavRate = reader->sampleRate;
        outputChannels = 2;
    }
    for (std::size_t track = 0; track < std::min(project.pitchedNotes.size(), instruments_.size()); ++track) {
        const auto setup = track < tracks.size() ? tracks[track] : TrackSetup{};
        const auto outputTrack = setup.outputTrack.value_or(track);
        auto* midi = outputTrack < std::min(tracks.size(), instruments_.size()) && tracks[outputTrack].instrument
            && tracks[outputTrack].instrument->acceptsMidi()
            ? &instruments_[outputTrack].timeline : nullptr;
        if (!midi) continue;
        for (const auto& change : project.programChanges[track]) {
            const auto at = static_cast<int>(std::llround(static_cast<double>(change.tick) * samplesPerTick_));
            midi->addEvent(juce::MidiMessage::programChange(change.channel.value_or(setup.midiChannel), change.program), at);
        }
        auto addNote = [&](juce::int64 start, juce::int64 end, int pitch, int velocity, int channel) {
            if (noteCount >= 20000) return false;
            const auto first = static_cast<int>(std::llround(static_cast<double>(start) * samplesPerTick_));
            const auto last = static_cast<int>(std::llround(static_cast<double>(end) * samplesPerTick_));
            if (last <= first) return true;
            ++noteCount;
            midi->addEvent(juce::MidiMessage::noteOn(channel, pitch, static_cast<juce::uint8>(velocity)), first);
            midi->addEvent(juce::MidiMessage::noteOff(channel, pitch), last);
            return true;
        };
        for (const auto& note : project.pitchedNotes[track])
            if (note.enabled && !addNote(note.startTick, note.endTick, note.pitch, note.velocity.value_or(92), setup.midiChannel))
                return juce::Result::fail("Preview transport supports up to 20,000 notes.");
        if (track < project.drumNotes.size())
            for (const auto& note : project.drumNotes[track])
                if (note.enabled && !addNote(note.startTick, note.endTick, note.pitch, note.velocity.value_or(92), 10))
                    return juce::Result::fail("Preview transport supports up to 20,000 notes.");
    }
    for (const auto& setup : tracks)
        if (setup.instrument)
            outputChannels = std::max({outputChannels, setup.instrument->getTotalNumInputChannels(),
                                       setup.instrument->getTotalNumOutputChannels()});
    for (const auto& chain : inserts) for (auto* effect : chain) if (effect) outputChannels = std::max(outputChannels, 2);
    pluginAudio_.setSize(0, 0);
    if (outputChannels > 0) {
        pluginAudio_.setSize(std::max(2, outputChannels), blockSize);
        for (auto& stem : stems_) stem.setSize(2, blockSize);
        pluginMidi_.ensureSize(1024 * 1024);
        for (std::size_t track = 0; track < std::min(tracks.size(), instruments_.size()); ++track) {
            const auto& setup = tracks[track];
            if (auto* instance = setup.instrument) {
                instance->setPlayHead(this);
                instance->setRateAndBufferSizeDetails(sampleRate_, blockSize);
                instance->prepareToPlay(sampleRate_, blockSize);
            }
            instruments_[track].instance = setup.instrument;
        }
        inserts_ = inserts;
        for (const auto& chain : inserts_) for (auto* effect : chain) if (effect) {
            effect->setPlayHead(this);
            effect->setRateAndBufferSizeDetails(sampleRate_, blockSize);
            effect->prepareToPlay(sampleRate_, blockSize);
        }
    }
    pressed_.fill(-1);
    recorded_.clear();
    sample_ = 0;
    recording_ = false;
    running_ = true;
    return juce::Result::ok();
}

std::vector<ostfstudio::PitchedNote> PreviewTransport::stop() {
    running_ = false;
    if (devices_) {
        if (recording_) devices_->removeMidiInputDeviceCallback({}, this);
        devices_->removeAudioCallback(this);
        devices_ = nullptr;
    }
    for (auto& track : instruments_) {
        if (auto* instance = std::exchange(track.instance, nullptr)) {
            instance->reset();
            instance->releaseResources();
            instance->setPlayHead(nullptr);
        }
    }
    for (auto& chain : inserts_) for (auto*& slot : chain) if (auto* effect = std::exchange(slot, nullptr)) {
        effect->reset();
        effect->releaseResources();
        effect->setPlayHead(nullptr);
    }
    const auto tick = positionTick();
    const std::scoped_lock lock(recordingMutex_);
    if (recording_) {
        for (std::size_t key = 0; key < pressed_.size(); ++key)
            if (pressed_[key] >= 0 && tick > pressed_[key])
                recorded_.push_back({pressed_[key], tick, static_cast<int>(key % 128), pressedVelocity_[key]});
    }
    recording_ = false;
    sample_ = 0;
    return std::exchange(recorded_, {});
}

juce::int64 PreviewTransport::positionTick() const {
    if (samplesPerTick_ <= 0) return 0;
    return std::min(scoreTicks_, static_cast<juce::int64>(static_cast<double>(sample_.load()) / samplesPerTick_));
}

void PreviewTransport::setChannelMix(std::size_t track, const ostfstudio::ChannelMix& mix, bool audible, double cueDb) {
    if (track >= gains_.size()) return;
    auto& channel = gains_[track];
    channel.level = audible ? ostfstudio::gainFromDb(mix.volumeDb) : 0.0f;
    channel.cue = mix.cue ? ostfstudio::gainFromDb(cueDb) : 0.0f;
    channel.pan = mix.pan;
    channel.bus = std::clamp(mix.outputBus + 1, 0, 2);
    for (std::size_t i = 0; i < channel.sends.size(); ++i) channel.sends[i] = mix.returnSends[i];
}

void PreviewTransport::setOutputMix(const ostfstudio::MixerRouting& mix) {
    outputGains_[0] = mix.outputMuted[0] ? 0.0f : ostfstudio::gainFromDb(mix.masterDb);
    for (std::size_t i = 0; i < mix.busDb.size(); ++i) outputGains_[1 + i] = mix.outputMuted[1 + i] ? 0.0f : ostfstudio::gainFromDb(mix.busDb[i]);
    for (std::size_t i = 0; i < mix.returnDb.size(); ++i)
        outputGains_[3 + i] = i < mix.returnCount && !mix.outputMuted[3 + i] ? ostfstudio::gainFromDb(mix.returnDb[i]) : 0.0f;
}

void PreviewTransport::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device->getCurrentSampleRate() != sampleRate_
        || (pluginAudio_.getNumChannels() > 0 && device->getCurrentBufferSizeSamples() > pluginAudio_.getNumSamples()))
        running_ = false;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> PreviewTransport::getPosition() const {
    PositionInfo position;
    const auto sample = sample_.load();
    position.setTimeInSamples(sample);
    position.setTimeInSeconds(static_cast<double>(sample) / sampleRate_);
    position.setPpqPosition(static_cast<double>(sample) * bpm_ / (sampleRate_ * 60.0));
    position.setBpm(bpm_);
    position.setIsPlaying(running_);
    return position;
}

void PreviewTransport::processEffects(const Chain& effects, juce::AudioBuffer<float>& audio, int count) {
    juce::AudioBuffer<float> stereo(audio.getArrayOfWritePointers(), 2, count);
    for (auto* effect : effects) {
        if (!effect) continue;
        pluginMidi_.clear();
        effect->processBlock(stereo, pluginMidi_);
    }
}

void PreviewTransport::renderInstruments(float* const* output, int channels, int count, int first, int end) {
    for (auto& stem : stems_) stem.clear(0, count);
    const auto audible = end - first;
    for (std::size_t track = 0; track < instruments_.size(); ++track) {
        auto& slot = instruments_[track];
        if (!slot.instance && slot.wav.getNumSamples() == 0) continue;
        const auto channelCount = slot.instance ? std::max({2, slot.instance->getTotalNumInputChannels(),
                                                            slot.instance->getTotalNumOutputChannels()}) : 2;
        if (channelCount < 1 || channelCount > pluginAudio_.getNumChannels()) continue;
        juce::AudioBuffer<float> block(pluginAudio_.getArrayOfWritePointers(), channelCount, count);
        pluginMidi_.clear();
        pluginMidi_.addEvents(slot.timeline, first, end - first, -first);
        block.clear();
        if (slot.instance) {
            juce::AudioBuffer<float> pluginBlock(block.getArrayOfWritePointers(),
                std::max(slot.instance->getTotalNumInputChannels(), slot.instance->getTotalNumOutputChannels()), count);
            slot.instance->processBlock(pluginBlock, pluginMidi_);
            if (slot.instance->getMainBusNumOutputChannels() == 1) block.copyFrom(1, 0, block, 0, 0, count);
        }
        if (slot.wav.getNumSamples() > 0) {
            for (int c = 0; c < 2; ++c) {
                const auto* source = slot.wav.getReadPointer(std::min(c, slot.wav.getNumChannels() - 1));
                auto* dest = block.getWritePointer(c);
                for (int i = 0; i < audible; ++i) {
                    const auto frame = (first + i) * slot.wavRate / sampleRate_;
                    if (frame >= slot.wav.getNumSamples()) break;
                    const auto pos = static_cast<int>(frame);
                    const auto next = std::min(pos + 1, slot.wav.getNumSamples() - 1);
                    dest[i] += source[pos] + (source[next] - source[pos]) * static_cast<float>(frame - pos);
                }
            }
        }
        processEffects(inserts_[outputCount + track], block, count);
        const auto& mix = gains_[track];
        for (int channel = 0; channel < 2; ++channel) {
            const auto balance = ostfstudio::stereoBalance(mix.pan.load(), channel == 1);
            const auto gain = mix.level.load() * balance;
            const auto* data = block.getReadPointer(channel);
            stems_[static_cast<std::size_t>(mix.bus.load())].addFrom(channel, 0, data, audible, gain);
            for (std::size_t r = 0; r < mix.sends.size(); ++r) {
                const auto send = mix.sends[r].load();
                if (send != 0.0f) stems_[3 + r].addFrom(channel, 0, data, audible, gain * send);
            }
            if (channels >= 4 && output[channel + 2])
                juce::FloatVectorOperations::addWithMultiply(output[channel + 2], data, mix.cue.load() * balance, audible);
        }
    }
    for (std::size_t i = 1; i < stems_.size(); ++i) {
        const auto gain = outputGains_[i].load();
        processEffects(inserts_[i], stems_[i], count);
        if (gain == 0.0f) continue;
        for (int c = 0; c < 2; ++c) stems_[0].addFrom(c, 0, stems_[i], c, 0, audible, gain);
    }
    processEffects(inserts_[0], stems_[0], count);
    for (int c = 0; c < std::min(2, channels); ++c)
        if (output[c]) juce::FloatVectorOperations::addWithMultiply(output[c], stems_[0].getReadPointer(c), outputGains_[0].load(), audible);
}

void PreviewTransport::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output, int channels,
                                                        int count, const juce::AudioIODeviceCallbackContext&) {
    renderBlock(output, channels, count);
}

void PreviewTransport::renderBlock(float* const* output, int channels, int count) {
    for (int c = 0; c < channels; ++c)
        if (output[c]) juce::FloatVectorOperations::clear(output[c], count);
    if (!running_) return;
    const auto first = sample_.load();
    const auto end = std::min(lastSample_, first + count);
    if (pluginAudio_.getNumChannels() > 0) {
        if (count > pluginAudio_.getNumSamples()) { running_ = false; return; }
        renderInstruments(output, channels, count, static_cast<int>(first), static_cast<int>(end));
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
        recorded_.push_back({start, tick, message.getNoteNumber(), pressedVelocity_[static_cast<std::size_t>(key)]});
    start = message.isNoteOn() ? tick : -1;
    if (message.isNoteOn()) pressedVelocity_[static_cast<std::size_t>(key)] = message.getVelocity();
}
