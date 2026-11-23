#include "ui/PreviewTransport.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

PreviewTransport::~PreviewTransport() { stop(); }

namespace {
// Reconstruct the state immediately before the first sample of the new block.
// Events on that sample are still delivered normally from the timeline.
juce::MidiBuffer midiAtSeek(const juce::MidiBuffer& timeline, int sample) {
    std::array<int, 16> programs;
    programs.fill(-1);
    std::array<std::array<juce::uint8, 128>, 16> sounding{};
    for (auto event : timeline) {
        if (event.samplePosition > sample) break;
        const auto msg = event.getMessage();
        const auto channel = msg.getChannel() - 1;
        if (msg.isProgramChange() && event.samplePosition < sample)
            programs[channel] = msg.getProgramChangeNumber();
        if (msg.isNoteOff() || msg.isNoteOn()) {
            auto& velocity = sounding[channel][msg.getNoteNumber()];
            velocity = msg.isNoteOn() && event.samplePosition < sample ? msg.getVelocity() : 0;
        }
    }
    juce::MidiBuffer result;
    for (int ch = 0; ch < 16; ++ch) {
        result.addEvent(juce::MidiMessage::allSoundOff(ch + 1), 0);
        result.addEvent(juce::MidiMessage::allNotesOff(ch + 1), 0);
        if (programs[ch] >= 0) result.addEvent(juce::MidiMessage::programChange(ch + 1, programs[ch]), 0);
        for (int pitch = 0; pitch < 128; ++pitch)
            if (const auto velocity = sounding[ch][pitch])
                result.addEvent(juce::MidiMessage::noteOn(ch + 1, pitch, velocity), 0);
    }
    return result;
}
}

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
                                    bool record, std::span<const TrackSetup> tracks, const Inserts& inserts, juce::int64 startTick) {
    stop();
    auto* device = devices.getCurrentAudioDevice();
    if (!device || !device->isOpen() || device->getActiveOutputChannels().isZero())
        return juce::Result::fail("Select an active audio output in Settings first.");
    if (record) {
        bool enabled = false;
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            enabled = enabled || devices.isMidiInputDeviceEnabled(input.identifier);
        if (!enabled) return juce::Result::fail("Enable a MIDI input in Settings before recording.");
    }
    const auto result = preparePlayback(project, tracks, inserts, device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    if (result.failed()) return result;
    if (startTick > 0) seekToTick(startTick);
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
    if (project.clock.empty()) return juce::Result::fail("The song needs an initial tempo.");
    clock_ = project.clock;
    ppq_ = project.ppq;
    meterBeats_ = project.meterBeats;
    beatUnit_ = project.beatUnit;
    scoreTicks_ = project.playbackEndTick();
    const auto total = clock_.secondsAt(scoreTicks_) * sampleRate_;
    if (!std::isfinite(total) || total < 1.0 || total > sampleRate_ * 3600.0
        || total > std::numeric_limits<int>::max() - 1)
        return juce::Result::fail("Preview transport supports songs up to one hour.");
    lastSample_ = static_cast<juce::int64>(std::ceil(total));
    int noteCount = 0;
    int outputChannels = 0;
    for (auto& slot : instruments_) {
        slot.timeline.clear();
        slot.seekEvents.clear();
        slot.wav.setSize(0, 0);
    }
    juce::WavAudioFormat wavFormat;
    for (const auto& audio : project.audio) {
        if (audio.track >= instruments_.size()) continue;
        auto stream = audio.embedded.isEmpty()
            ? std::unique_ptr<juce::InputStream>(audio.localFile.createInputStream().release())
            : std::make_unique<juce::MemoryInputStream>(audio.embedded, false);
        if (!stream) return juce::Result::fail("Missing WAV: " + audio.path);
        if (audio.sha256.isNotEmpty() && (stream->getTotalLength() > 16 * 1024 * 1024
            || juce::SHA256(*stream).toHexString() != audio.sha256 || !stream->setPosition(0)))
            return juce::Result::fail("WAV does not match the saved project: " + audio.path);
        std::unique_ptr<juce::AudioFormatReader> reader(wavFormat.createReaderFor(stream.release(), true));
        if (!reader || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024
            || reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate <= 0)
            return juce::Result::fail("Unsupported WAV: " + audio.path);
        auto& slot = instruments_[audio.track];
        if (audio.sha256.isNotEmpty() && (reader->sampleRate != audio.sampleRate
            || static_cast<int>(reader->numChannels) != audio.channels || reader->lengthInSamples != audio.assetFrames))
            return juce::Result::fail("WAV format does not match its descriptor: " + audio.path);
        const auto frames = audio.frameCount > 0 ? audio.frameCount : reader->lengthInSamples;
        if (audio.startFrame < 0 || frames > reader->lengthInSamples - audio.startFrame)
            return juce::Result::fail("Invalid WAV source range: " + audio.path);
        slot.wavStart = juce::roundToInt(clock_.secondsAt(audio.startTick) * sampleRate_);
        slot.wavEnd = audio.durationTicks > 0 ? juce::roundToInt(clock_.secondsAt(audio.startTick + audio.durationTicks) * sampleRate_)
                                              : static_cast<int>(lastSample_);
        slot.wavGain = audio.muted ? 0.0f : juce::Decibels::decibelsToGain(static_cast<float>(audio.gainDb));
        slot.wav.setSize(static_cast<int>(reader->numChannels), static_cast<int>(frames));
        if (!reader->read(&slot.wav, 0, slot.wav.getNumSamples(), audio.startFrame, true, true))
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
            const auto at = static_cast<int>(std::llround(clock_.secondsAt(change.tick) * sampleRate_));
            midi->addEvent(juce::MidiMessage::programChange(change.channel.value_or(setup.midiChannel), change.program), at);
        }
        auto addNote = [&](juce::int64 start, juce::int64 end, int pitch, int velocity, int channel) {
            if (noteCount >= 20000) return false;
            const auto first = static_cast<int>(std::llround(clock_.secondsAt(start) * sampleRate_));
            const auto last = static_cast<int>(std::llround(clock_.secondsAt(end) * sampleRate_));
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

void PreviewTransport::seekToTick(juce::int64 tick) {
    if (!running_ || recording_) return;
    tick = std::clamp(tick, juce::int64(0), scoreTicks_);
    const auto sample = tick == scoreTicks_ ? lastSample_ : std::llround(clock_.secondsAt(tick) * sampleRate_);
    std::array<juce::MidiBuffer, maxTracks> events;
    for (std::size_t i = 0; i < instruments_.size(); ++i)
        if (instruments_[i].instance)
            events[i] = midiAtSeek(instruments_[i].timeline, static_cast<int>(sample));

    // Timelines are immutable during playback. Only the handover and processor
    // reset need to exclude the device callback.
    const auto apply = [&] {
        for (std::size_t i = 0; i < instruments_.size(); ++i) {
            if (auto* instance = instruments_[i].instance) instance->reset();
            instruments_[i].seekEvents.swapWith(events[i]);
        }
        for (const auto& chain : inserts_)
            for (auto* effect : chain) if (effect) effect->reset();
        sample_ = sample;
        running_ = sample < lastSample_;
    };
    if (devices_) {
        const juce::ScopedLock lock(devices_->getAudioCallbackLock());
        apply();
    } else {
        apply();
    }
}

std::vector<ostfstudio::PitchedNote> PreviewTransport::stop(Cursor* stoppedAt) {
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
    const auto finalCursor = cursor();
    if (stoppedAt) *stoppedAt = finalCursor;
    const auto tick = finalCursor.tick;
    const std::scoped_lock lock(recordingMutex_);
    if (recording_) {
        for (std::size_t key = 0; key < pressed_.size(); ++key)
            if (pressed_[key] >= 0 && tick > pressed_[key])
                recorded_.push_back({pressed_[key], tick, static_cast<int>(key % 128), pressedVelocity_[key]});
    }
    recording_ = false;
    sample_ = 0;
    takeMeters();
    return std::exchange(recorded_, {});
}

PreviewTransport::Cursor PreviewTransport::cursor() const {
    const auto seconds = sampleRate_ > 0 ? sample_.load() / sampleRate_ : 0.0;
    return {std::min(scoreTicks_, static_cast<juce::int64>(clock_.tickAt(seconds) + 1e-9)), seconds};
}

juce::int64 PreviewTransport::positionTick() const { return cursor().tick; }

void PreviewTransport::setChannelMix(std::size_t track, const ostfstudio::ChannelMix& mix, bool audible, double cueDb) {
    if (track >= gains_.size()) return;
    auto& channel = gains_[track];
    channel.level = audible ? ostfstudio::gainFromDb(mix.volumeDb) : 0.0f;
    channel.cue = mix.cue ? ostfstudio::gainFromDb(cueDb) : 0.0f;
    channel.pan = mix.pan;
    channel.bus = mix.outputBus >= 0 && mix.outputBus < static_cast<int>(ostfstudio::maxMixerBuses)
        ? static_cast<int>(ostfstudio::busOutput(static_cast<std::size_t>(mix.outputBus))) : 0;
    for (std::size_t i = 0; i < channel.sends.size(); ++i) channel.sends[i] = mix.returnSends[i];
}

void PreviewTransport::setOutputMix(const ostfstudio::MixerRouting& mix) {
    outputGains_[0] = mix.outputMuted[0] ? 0.0f : ostfstudio::gainFromDb(mix.masterDb);
    for (std::size_t i = 0; i < mix.busDb.size(); ++i) {
        const auto slot = ostfstudio::busOutput(i);
        outputGains_[slot] = i < mix.busCount && !mix.outputMuted[slot] ? ostfstudio::gainFromDb(mix.busDb[i]) : 0.0f;
    }
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
    const double seconds = sampleRate_ > 0 ? sample / sampleRate_ : 0.0;
    const double tick = clock_.tickAt(seconds);
    position.setTimeInSeconds(seconds);
    position.setPpqPosition(tick / ppq_);
    position.setBpm(clock_.bpmAt(static_cast<juce::int64>(tick + 1e-9)));
    position.setTimeSignature(TimeSignature{meterBeats_, beatUnit_});
    const auto barPpq = meterBeats_ * 4.0 / beatUnit_;
    position.setPpqPositionOfLastBarStart(std::floor(tick / ppq_ / barPpq) * barPpq);
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
        pluginMidi_.addEvents(slot.seekEvents, 0, -1, 0);
        slot.seekEvents.clear();
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
                const auto last = std::min(audible, slot.wavEnd - first);
                for (int i = std::max(0, slot.wavStart - first); i < last; ++i) {
                    const auto frame = (first + i - slot.wavStart) * slot.wavRate / sampleRate_;
                    if (frame >= slot.wav.getNumSamples()) break;
                    const auto pos = static_cast<int>(frame);
                    const auto next = std::min(pos + 1, slot.wav.getNumSamples() - 1);
                    dest[i] += (source[pos] + (source[next] - source[pos]) * static_cast<float>(frame - pos)) * slot.wavGain;
                }
            }
        }
        processEffects(inserts_[outputCount + track], block, count);
        const auto& mix = gains_[track];
        for (int channel = 0; channel < 2; ++channel) {
            const auto balance = ostfstudio::stereoBalance(mix.pan.load(), channel == 1);
            const auto gain = mix.level.load() * balance;
            const auto* data = block.getReadPointer(channel);
            trackMeters_[track].publish(channel, block.getMagnitude(channel, 0, audible) * gain);
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
        for (int c = 0; c < 2; ++c) {
            outputMeters_[i].publish(c, stems_[i].getMagnitude(c, 0, audible) * gain);
            stems_[0].addFrom(c, 0, stems_[i], c, 0, audible, gain);
        }
    }
    processEffects(inserts_[0], stems_[0], count);
    const float masterGain = outputGains_[0].load();
    for (int c = 0; c < 2; ++c) {
        outputMeters_[0].publish(c, stems_[0].getMagnitude(c, 0, audible) * masterGain);
        if (c < channels && output[c])
            juce::FloatVectorOperations::addWithMultiply(output[c], stems_[0].getReadPointer(c), masterGain, audible);
        if (channels >= 4 && output[c + 2] && audible > 0) {
            auto range = juce::FloatVectorOperations::findMinAndMax(output[c + 2], audible);
            cueMeter_.publish(c, std::max(std::abs(range.getStart()), std::abs(range.getEnd())));
        }
    }
}

ostfstudio::MeterFrame PreviewTransport::takeMeters() noexcept {
    ostfstudio::MeterFrame frame;
    for (std::size_t i = 0; i < trackMeters_.size(); ++i) frame.tracks[i] = trackMeters_[i].take();
    for (std::size_t i = 0; i < outputMeters_.size(); ++i) frame.outputs[i] = outputMeters_[i].take();
    frame.cue = cueMeter_.take();
    return frame;
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
