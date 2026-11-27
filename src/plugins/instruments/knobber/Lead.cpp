#include "Lead.h"
namespace knobber {
using State = juce::AudioProcessorValueTreeState;
static juce::StringArray choices(const char *labels) {
    return juce::StringArray::fromTokens(labels, "|", "");
}
static State::ParameterLayout makeLayout() {
    State::ParameterLayout layout;
    for (size_t i = 0; i < std::size(controls); ++i) {
        const auto &c = controls[i];
        const juce::ParameterID id{c.id, 1};
        const auto initial = juce::jlimit(c.low, c.high, programs[0].values[i]);
        if (*c.labels) {
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, c.name, choices(c.labels),
                                                                    juce::roundToInt(initial)));
        } else {
            const juce::NormalisableRange<float> range(c.low, c.high, c.discrete ? 1.0f : 0.0f);
            auto display = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [whole = c.discrete](float value, int) {
                    if (whole) return juce::String(juce::roundToInt(value));
                    return juce::String(value, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
                });
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, c.name, range, initial, display));
        }
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"mpe", 1}, "MPE", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"quality", 1}, "Voice quality",
        juce::StringArray{"Original rate", "2x oversampling"}, 0));
    return layout;
}
Lead::Lead()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "SuperKnobberLead", makeLayout()) {
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = state.getRawParameterValue(controls[i].id);
    mpeValue = state.getRawParameterValue("mpe");
    quality = state.getRawParameterValue("quality");
    setLatencySamples(OutputFilter::latency);
}
const juce::String Lead::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? juce::String(programs[index].name)
                                                             : juce::String{};
}
void Lead::setCurrentProgram(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return;
    for (size_t i = 0; i < values.size(); ++i) {
        auto *parameter = state.getParameter(controls[i].id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(programs[index].values[i]));
    }
    program.store(index);
}
void Lead::getStateInformation(juce::MemoryBlock &data) {
    auto tree = state.copyState();
    tree.setProperty("program", program.load(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, data);
}
void Lead::setStateInformation(const void *data, int size) {
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(state.state.getType())) {
        auto tree = juce::ValueTree::fromXml(*xml);
        program.store(
            juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(tree.getProperty("program", 0))));
        state.replaceState(tree);
    }
}
void Lead::prepareToPlay(double rate, int) {
    sampleRate = rate;
    slots = {};
    for (size_t i = 0; i < slots.size(); ++i)
        slots[i].engine.noise.seed += static_cast<uint32_t>(i) * 9719;
    channels = {};
    keys = {};
    clock = 0;
    drive = {};
    effects = Effects{};
    filter = {};
    previousQuality = false;
    lastDry = {};
}
void Lead::start(Slot &slot, int channel, int note, int velocity) {
    slot.channel = channel;
    slot.note.knote = note;
    slot.note.kvel = velocity;
    slot.note.kgate = 1;
    slot.note.ktrig = 1 - slot.note.ktrig;
    slot.held = true;
    slot.keyPressure = -1;
    slot.age = ++clock;
}
void Lead::releaseKey(int channel, int note) {
    keys[channel][note] = {};
    for (auto &slot : slots) {
        if (slot.held && slot.channel == channel && slot.note.knote == note) {
            slot.held = false;
            if (mono && !mpe) {
                uint64_t newest = 0;
                int lastChannel = 0, lastNote = 0;
                for (int ch = 0; ch < 16; ++ch)
                    for (int n = 0; n < 128; ++n)
                        if (keys[ch][n].order > newest) {
                            newest = keys[ch][n].order;
                            lastChannel = ch;
                            lastNote = n;
                        }
                if (newest)
                    start(slot, lastChannel, lastNote, keys[lastChannel][lastNote].velocity);
            }
        }
    }
    updateGates();
}
void Lead::updateGates() {
    for (auto &slot : slots) {
        if (!slot.held && !channels[slot.channel].pedal && !(mpe && channels[0].pedal))
            slot.note.kgate = 0;
    }
}
void Lead::receive(const juce::MidiMessage &msg) {
    if (msg.getChannel() < 1)
        return;
    const int ch = msg.getChannel() - 1;
    auto &expression = channels[ch];
    if (msg.isNoteOn()) {
        const int note = msg.getNoteNumber();
        keys[ch][note] = {++clock, msg.getVelocity()};
        auto *slot = &slots.front();
        if (!mono || mpe) {
            // Prefer a released voice, then steal the oldest active voice.
            for (auto &candidate : slots) {
                if (mpe && candidate.age && candidate.channel == ch) {
                    slot = &candidate;
                    break;
                }
                if (std::pair(candidate.note.kgate, candidate.age) < std::pair(slot->note.kgate, slot->age))
                    slot = &candidate;
            }
        }
        start(*slot, ch, note, msg.getVelocity());
    } else if (msg.isNoteOff()) {
        releaseKey(ch, msg.getNoteNumber());
    } else if (msg.isAllSoundOff() || msg.isAllNotesOff()) {
        if (mpe && ch == 0)
            keys = {};
        else
            keys[ch] = {};
        for (auto &slot : slots)
            if (slot.channel == ch || (mpe && ch == 0)) {
                slot.held = false;
                slot.note.kgate = 0;
                if (msg.isAllSoundOff())
                    slot = Slot{};
            }
        expression.pedal = false;
    } else if (msg.isPitchWheel()) {
        expression.bend = (msg.getPitchWheelValue() - 8192) / 8192.0;
    } else if (msg.isChannelPressure()) {
        expression.pressure = msg.getChannelPressureValue() / 127.0;
        for (auto &slot : slots)
            if (slot.channel == ch)
                slot.keyPressure = -1;
    } else if (msg.isAftertouch()) {
        for (auto &slot : slots)
            if (slot.channel == ch && slot.note.knote == msg.getNoteNumber())
                slot.keyPressure = msg.getAfterTouchValue() / 127.0;
    } else if (msg.isController()) {
        const auto value = msg.getControllerValue();
        switch (msg.getControllerNumber()) {
        case 1:
            expression.wheel = value / 127.0;
            break;
        case 64:
            expression.pedal = value >= 64;
            updateGates();
            break;
        case 74:
            expression.timbre = value / 127.0;
            break;
        default:
            break;
        }
    }
}
Stereo Lead::voices(double rate) {
    Stereo dry;
    // All three Gen voices run between notes. Free phases, glide and parameter
    // smoothing must reach the note-on sample even for a previously unused voice.
    for (auto &slot : slots) {
        dry += slot.engine.process(rate, parameters, slot.note);
    }
    return dry;
}
void Lead::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi) {
    juce::ScopedNoDenormals guard;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto &c = controls[i];
        parameters.*c.member = values[i]->load() * c.scale + c.offset;
    }
    const bool newMpe = mpeValue->load() > 0.5f;
    const bool newMono = parameters.mono_mode > 0.5 && !newMpe;
    if (mono != newMono || mpe != newMpe) {
        keys = {};
        for (auto &slot : slots) {
            slot.held = false;
            slot.note.kgate = 0;
        }
        mono = newMono;
        mpe = newMpe;
    }
    if (mpe)
        parameters.mono_mode = 0;
    const bool twice = quality->load() > 0.5f;
    if (twice != previousQuality) {
        filter.prime(lastDry);
        previousQuality = twice;
    }
    auto events = midi.begin();
    const auto end = midi.end();
    auto *left = buffer.getWritePointer(0);
    auto *right = buffer.getWritePointer(1);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        while (events != end && (*events).samplePosition <= i) {
            receive((*events).getMessage());
            ++events;
        }
        for (auto &slot : slots) {
            auto &exp = channels[slot.channel];
            const bool member = mpe && slot.channel != 0;
            slot.note.kbend = exp.bend + (member ? channels[0].bend : 0);
            slot.note.kwheel = std::max(exp.wheel, member ? channels[0].wheel : 0.0);
            slot.note.ktimbre = exp.timbre;
            slot.note.kpressure = slot.keyPressure >= 0 ? slot.keyPressure : exp.pressure;
        }
        Stereo dry;
        if (twice) {
            const auto even = voices(sampleRate * 2);
            const auto odd = voices(sampleRate * 2);
            dry = filter.downsample(even, odd);
        } else {
            dry = filter.delayBase(voices(sampleRate));
        }
        lastDry = dry;
        const auto wet = effects.process(sampleRate, parameters, drive.process(sampleRate, parameters, dry));
        left[i] = static_cast<float>(dry.left + wet.left);
        right[i] = static_cast<float>(dry.right + wet.right);
    }
    midi.clear();
}
} // namespace knobber
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new knobber::Lead(); }
