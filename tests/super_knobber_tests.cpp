#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/plugins/instruments/knobber/Programs.h"
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>

using namespace juce;
static void check(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
static AudioProcessorParameter &parameter(AudioProcessor &p, String name) {
    for (auto *control : p.getParameters())
        if (control->getName(128) == name)
            return *control;
    throw std::runtime_error(("Missing parameter " + name).toStdString());
}
static constexpr int keys[]{60, 67, 72, 74, 79, 84, 81, 76};

static AudioBuffer<float> render(AudioProcessor &p, int rate, bool steady) {
    constexpr int block = 256;
    p.setRateAndBufferSizeDetails(rate, block);
    p.prepareToPlay(rate, block);
    const int total = steady ? 2 * rate : static_cast<int>(4.15 * rate);
    AudioBuffer<float> audio(2, total);
    MidiBuffer sequence;
    if (steady)
        sequence.addEvent(MidiMessage::noteOn(1, 105, uint8(100)), 0);
    else
        for (int i = 0; i < 8; ++i) {
            const int on = roundToInt((.125 + i * .375) * rate);
            sequence.addEvent(MidiMessage::noteOn(1, keys[i], uint8(100)), on);
            sequence.addEvent(MidiMessage::noteOff(1, keys[i]), on + roundToInt(rate * .270));
        }
    for (int pos = 0; pos < total; pos += block) {
        int count = std::min(block, total - pos);
        AudioBuffer<float> part(audio.getArrayOfWritePointers(), 2, pos, count);
        part.clear();
        MidiBuffer midi;
        midi.addEvents(sequence, pos, count, -pos);
        p.processBlock(part, midi);
    }
    p.releaseResources();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
            check(std::isfinite(audio.getSample(ch, i)), "Non-finite sample");
    check(audio.getMagnitude(0, total) > .001f, "Silent output");
    if (!steady)
        check(audio.getMagnitude(0, rate / 8) == 0, "MIDI offset lost");
    return audio;
}

static void saveWav(const File &file, const AudioBuffer<float> &data, int rate) {
    FileOutputStream output(file);
    check(output.openedOk() && output.setPosition(0) && output.truncate().wasOk(), "Cannot create WAV");
    const int bytes = data.getNumSamples() * 6;
    output.write("RIFF", 4);
    output.writeInt(36 + bytes);
    output.write("WAVEfmt ", 8);
    output.writeInt(16);
    output.writeShort(1);
    output.writeShort(2);
    output.writeInt(rate);
    output.writeInt(rate * 6);
    output.writeShort(6);
    output.writeShort(24);
    output.write("data", 4);
    output.writeInt(bytes);
    for (int i = 0; i < data.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch) {
            const auto sample =
                static_cast<uint32_t>(roundToInt(jlimit(-1.f, 1.f, data.getSample(ch, i)) * 8388607));
            output.writeByte(char(sample));
            output.writeByte(char(sample >> 8));
            output.writeByte(char(sample >> 16));
        }
}
static void saveMidi(const File &file) {
    MidiMessageSequence seq;
    seq.addEvent(MidiMessage::tempoMetaEvent(500000), 0);
    for (int i = 0; i < 8; ++i) {
        const int start = 125 + i * 375;
        seq.addEvent(MidiMessage::noteOn(1, keys[i], uint8(100)), start);
        seq.addEvent(MidiMessage::noteOff(1, keys[i]), start + 270);
    }
    MidiFile midi;
    midi.setTicksPerQuarterNote(500);
    midi.addTrack(seq);
    FileOutputStream output(file);
    check(output.openedOk() && output.setPosition(0) && output.truncate().wasOk() && midi.writeTo(output, 0),
          "Cannot save MIDI");
}

// A7 at 48 kHz completes an integer number of cycles within one second.
// Remaining energy after subtracting DC and its six in-band harmonics includes folded harmonics.
static double residualDb(const AudioBuffer<float> &signal) {
    const int size = 48000;
    const auto *data = signal.getReadPointer(0, size);
    double total = 0, mean = 0;
    for (int i = 0; i < size; ++i) {
        total += double(data[i]) * data[i] / size;
        mean += data[i] / double(size);
    }
    double harmonic = mean * mean;
    for (int h = 1; h * 3520 < size / 2; ++h) {
        std::complex<double> bin{};
        for (int i = 0; i < size; ++i)
            bin += double(data[i]) * std::polar(1.0, -2 * knobber::pi * h * 3520 * i / size);
        harmonic += 2 * std::norm(bin) / (double(size) * size);
    }
    return 10 * std::log10(std::max(1e-16, (total - harmonic) / total));
}

int main(int argc, char **argv) {
    ScopedJuceInitialiser_GUI gui;
    try {
        check(argc == 2 || argc == 3, "Expected VST3 path, optional listening folder");
        const File bundle(String::fromUTF8(argv[1]));
        VST3PluginFormat format;
        OwnedArray<PluginDescription> types;
        format.findAllTypesForFile(types, bundle.getFullPathName());
        check(types.size() == 1 && types[0]->isInstrument && types[0]->name == "SUPER KNOBBER LEAD",
              "VST3 scan failed");
        String error;
        auto plugin = format.createInstanceFromDescription(*types[0], 48000, 256, error);
        check(plugin != nullptr, "VST3 instantiation failed");
        auto &p = *plugin;
        check(p.hasEditor() && p.acceptsMidi(), "Instrument/editor missing");
        check(p.getNumPrograms() == std::size(knobber::programs), "Wrong factory bank size");
        for (int i = 0; i < p.getNumPrograms(); ++i)
            check(p.getProgramName(i) == knobber::programs[i].name, "Wrong factory preset name");
        for (auto &c : knobber::controls)
            check(parameter(p, c.name).isAutomatable(), "Control unreachable by host");
        for (auto *param : p.getParameters())
            check(!param->getName(128).containsIgnoreCase("Blue Hand"), "Unexpected proxy");
        p.setCurrentProgram(1);
        auto &quality = parameter(p, "Voice quality");
        auto &cutoff = parameter(p, "Cutoff");
        cutoff.setValueNotifyingHost(.38f);
        quality.setValueNotifyingHost(1);
        MemoryBlock saved;
        p.getStateInformation(saved);
        p.setCurrentProgram(9);
        quality.setValueNotifyingHost(0);
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        check(std::abs(cutoff.getValue() - .38f) < .001f && quality.getValue() > .5f,
              "DAW state recall failed");
        p.setCurrentProgram(0);
        p.setCurrentProgram(1);
        File folder;
        if (argc == 3) {
            folder = File(String::fromUTF8(argv[2]));
            check(folder.createDirectory().wasOk(), "Listening folder");
            saveMidi(folder.getChildFile("GM-082-phrase.mid"));
        }
        for (int rate : {44100, 48000})
            for (int mode : {0, 1}) {
                quality.setValueNotifyingHost(float(mode));
                auto audio = render(p, rate, false);
                if (argc == 3 && rate == 48000)
                    saveWav(folder.getChildFile(mode ? "VST3-2x.wav" : "VST3-original-rate.wav"), audio,
                            rate);
            }
        // Configure one stationary, bright, saturated saw using the actual host parameters.
        for (auto &c : knobber::controls) {
            float value = c.initial;
            const String id(c.id);
            for (auto name : {"o2_lvl", "o3_lvl", "sub_lvl", "saw_mix", "drift", "osc_slop", "envamt",
                              "kbamt", "reso", "glide", "va", "fa", "vel_amp", "nz_lvl"})
                if (id == name)
                    value = 0;
            if (id == "vs")
                value = 100;
            if (id == "cutoff")
                value = 16000;
            if (id == "o1_wave")
                value = .5f;
            if (id == "drive" || id == "sat")
                value = 6;
            auto &host = parameter(p, c.name);
            host.setValueNotifyingHost(jlimit(0.f, 1.f, (value - c.low) / (c.high - c.low)));
        }
        quality.setValueNotifyingHost(0);
        const auto baseSignal = render(p, 48000, true);
        const auto original = residualDb(baseSignal);
        quality.setValueNotifyingHost(1);
        const auto oversampled = render(p, 48000, true);
        const auto twice = residualDb(oversampled);
        if (argc == 3) {
            saveWav(folder.getChildFile("A7-VST3-original-rate.wav"), baseSignal, 48000);
            saveWav(folder.getChildFile("A7-VST3-2x.wav"), oversampled, 48000);
        }
        std::cout << "A7 non-harmonic energy relative to total, 1x " << original << " dB, 2x " << twice
                  << " dB\n";
        check(twice < original - 2, "Oversampling did not reduce alias energy on the bright saw fixture");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
