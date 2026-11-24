#include "StockEffects.h"
#include "ui/PreviewTransport.h"
#include "ui/PluginRoles.h"
#include <cmath>
#include <complex>
#include <iostream>

namespace {
using Instance = std::unique_ptr<juce::AudioPluginInstance>;

Instance load(juce::AudioPluginFormatManager& formats, const juce::File& bundle) {
    juce::OwnedArray<juce::PluginDescription> types;
    formats.getFormat(0)->findAllTypesForFile(types, bundle.getFullPathName());
    juce::String error;
    if (types.size() == 1) return formats.createPluginInstance(*types[0], 48000, 256, error);
    return {};
}

bool checkHarmonizer(juce::AudioPluginInstance& fx) {
    const auto& params = fx.getParameters();
    if (params.size() < 4 || !fx.hasEditor()) { std::cerr << "Missing harmonizer controls\n"; return false; }
    std::unique_ptr<juce::AudioProcessorEditor> editor(fx.createEditorIfNeeded());
    if (!editor) { std::cerr << "Harmonizer editor failed to open\n"; return false; }
    constexpr double rate = 48000;
    const std::array<float, 4> settings{19.f / 24.f, 0.f, .5f, .6f};  // +7 and -12 semitones
    for (int i = 0; i < 4; ++i) params[i]->setValueNotifyingHost(settings[i]);
    juce::MemoryBlock state;
    fx.getStateInformation(state);
    for (int i = 0; i < 4; ++i) params[i]->setValueNotifyingHost(1.f);
    fx.setStateInformation(state.getData(), int(state.getSize()));
    for (int i = 0; i < 4; ++i) if (std::abs(params[i]->getValue() - settings[i]) > .001f) {
        std::cerr << "Recall: " << params[i]->getName(64) << " = " << params[i]->getValue() << ", expected " << settings[i] << '\n';
        return false;
    }

    fx.setRateAndBufferSizeDetails(rate, 256);
    fx.prepareToPlay(rate, 256);
    juce::MidiBuffer midi;
    const std::array<double, 3> frequencies{440., 440. * std::exp2(7. / 12.), 220.};
    // Compare the three expected pitches with their neighbouring semitones. Measure
    // short windows separately so phase changes across synthesis frames do not cancel.
    std::array<std::array<double, 3>, 3> magnitude{};
    std::array<std::array<std::complex<double>, 3>, 3> spectrum{};
    const std::array<double, 3> offsets{std::exp2(-1. / 12.), 1., std::exp2(1. / 12.)};
    constexpr int blockSize = 193;
    constexpr int windowSize = 40 * blockSize;
    juce::AudioBuffer<float> block(2, blockSize);
    for (int b = 0; b < 260; ++b) {
        block.clear();
        for (int n = 0; n < blockSize; ++n)
            block.setSample(0, n, float(.4 * std::sin(juce::MathConstants<double>::twoPi * 440 * (b * blockSize + n) / rate)));
        fx.processBlock(block, midi);
        if (block.getMagnitude(1, 0, blockSize) > .0001f) { std::cerr << "Leakage on silent channel\n"; return false; }
        if (b < 100) continue;
        for (int n = 0; n < blockSize; ++n) {
            const double sample = block.getSample(0, n);
            if (!std::isfinite(sample)) return false;
            const int position = ((b - 100) % 40) * blockSize + n;
            const double window = .5 - .5 * std::cos(juce::MathConstants<double>::twoPi * position / windowSize);
            const double phase = -juce::MathConstants<double>::twoPi * position / rate;
            for (size_t f = 0; f < frequencies.size(); ++f)
                for (size_t k = 0; k < offsets.size(); ++k)
                    spectrum[f][k] += sample * window * std::polar(1., phase * frequencies[f] * offsets[k]);
        }
        if ((b - 99) % 40 == 0) {
            for (size_t f = 0; f < frequencies.size(); ++f)
                for (size_t k = 0; k < offsets.size(); ++k)
                    magnitude[f][k] += std::abs(spectrum[f][k]) / windowSize;
            spectrum = {};
        }
    }
    fx.releaseResources();
    for (size_t f = 0; f < frequencies.size(); ++f) {
        const auto& band = magnitude[f];
        if (band[1] < .025 || band[1] < 3 * std::max(band[0], band[2])) {
            std::cerr << frequencies[f] << " Hz (below/centre/above): " << band[0] << ", " << band[1] << ", " << band[2] << '\n';
            return false;
        }
    }

    if (!fx.setBusesLayout({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::mono()}})) {
        std::cerr << "Mono layout rejected\n"; return false;
    }
    params[3]->setValueNotifyingHost(0.f);
    fx.setRateAndBufferSizeDetails(44100, 128);
    fx.prepareToPlay(44100, 128);
    const int latency = fx.getLatencySamples();
    if (latency <= 0 || latency > 44100) { std::cerr << "Reported latency: " << latency << '\n'; return false; }
    juce::AudioBuffer<float> mono(1, 97);
    for (int start = 0; start <= latency + 194; start += 97) {
        mono.clear();
        if (start == 0) mono.setSample(0, 11, .5f);
        fx.processBlock(mono, midi);
        for (int i = 0; i < 97; ++i) {
            const float expected = start + i == latency + 11 ? .5f : 0.f;
            if (std::abs(mono.getSample(0, i) - expected) > .00001f) {
                std::cerr << "Dry impulse at " << start + i << ": " << mono.getSample(0, i) << ", latency " << latency << '\n'; return false;
            }
        }
    }
    fx.releaseResources();
    return true;
}

bool checkEffect(juce::AudioPluginInstance& instance, const ostfstudio::StockEffect& fx) {
    const auto desc = instance.getPluginDescription();
    if (!PreviewTransport::supportsInsert(instance) || desc.manufacturerName != "OSTF Studio"
        || fx.chainId() != "VST3|OSTF Studio|" + desc.name + "|" + juce::String(desc.uniqueId)) return false;
    if (desc.name == "OSTF Harmonizer") return checkHarmonizer(instance);
    instance.setRateAndBufferSizeDetails(48000, 256);
    instance.prepareToPlay(48000, 256);
    // The multiband EQs intentionally load flat. Boost a mid band before measuring processing.
    if (desc.name == "OSTF 8-Band EQ" || desc.name == "OSTF Parametric EQ" || desc.name == "OSTF Graphic EQ") {
        const int gainIndex = desc.name == "OSTF Graphic EQ" ? 5 : desc.name == "OSTF Parametric EQ" ? 4 : 10;
        instance.getParameters()[gainIndex]->setValueNotifyingHost(.82f);
    }
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    double difference = 0, tail = 0;
    float peak = 0;
    for (int block = 0; block < 160; ++block) {
        for (int i = 0; i < 256; ++i) {
            const float x = block < 90 ? static_cast<float>((desc.name == "OSTF Gate" && block < 35 ? .008 : 1.2) * std::sin((desc.name == "OSTF De-Esser" ? .88 : .117) * (block * 256 + i))) : 0.f;
            buffer.setSample(0, i, x); buffer.setSample(1, i, x * 0.7f);
        }
        const juce::AudioBuffer<float> dry(buffer);
        instance.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) {
            const float wet = buffer.getSample(ch, i);
            if (!std::isfinite(wet)) return false;
            difference += std::abs(wet - dry.getSample(ch, i));
            if (block > 95) tail += std::abs(wet);
            if (block > 5) peak = std::max(peak, std::abs(wet));
        }
    }
    if (desc.name == "OSTF Stem Separator") return difference == 0 && instance.getLatencySamples() == 0 && instance.hasEditor();
    if (desc.name == "OSTF Wave Analyzer") {
        if (difference != 0 || instance.getLatencySamples() != 0 || !instance.hasEditor()) return false;
        auto* span = instance.getParameters()[0];
        span->setValueNotifyingHost(1.f);
        juce::MemoryBlock saved;
        instance.getStateInformation(saved);
        span->setValueNotifyingHost(0.f);
        instance.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        if (span->getValue() != 1.f) return false;
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.createEditorIfNeeded());
        if (!editor) return false;
        editor.reset();
        instance.releaseResources();
        if (!instance.setBusesLayout({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::mono()}})) return false;
        instance.prepareToPlay(44100, 128);
        juce::AudioBuffer<float> mono(1, 101);
        mono.clear(); mono.setSample(0, 89, -.375f);
        instance.processBlock(mono, midi);
        instance.releaseResources();
        return mono.getSample(0, 89) == -.375f && mono.getMagnitude(0, 0, 101) == .375f;
    }
    auto* param = instance.getParameters()[0];
    param->setValueNotifyingHost(.28f);
    juce::MemoryBlock state;
    instance.getStateInformation(state);
    param->setValueNotifyingHost(.78f);
    instance.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    instance.releaseResources();
    return difference > 1 && peak > 0.001 && std::abs(param->getValue() - .28f) < .01f
        && (desc.name != "OSTF Limiter" || peak <= juce::Decibels::decibelsToGain(-1.2f) + .0001f)
        && (desc.name != "OSTF Reverb" || tail > .1);
}
}

int checkStudioInstruments(const juce::StringArray& args) {
    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    juce::FileInputStream input{juce::File(args[2])};
    std::unique_ptr<ostfstudio::OstfpPreview> song;
    if (ostfstudio::readOstfpPreview(input, song).failed() || !song->savedMix) return 1;
    const auto& mix = *song->savedMix;
    const std::array<juce::String,4> ids {"arp","sub","drums","pad"};
    std::vector<Instance> instances;
    std::vector<PreviewTransport::TrackSetup> tracks(song->trackLanes.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        const int track = song->trackIds.indexOf(ids[i]);
        if (track < 0) return 2;
        const auto& def = ostfstudio::stockInstruments[i];
        auto synth = load(formats,juce::File(args[1]).getChildFile(juce::String(def.name) + ".vst3"));
        if (!synth || !PreviewTransport::supportsInstrument(*synth)
            || !matchesVst3Instrument(mix.instruments[track],synth->getPluginDescription())) return 3;
        const int ch = mix.midiRoutes[track].channel;
        const int key = ids[i] == "drums" ? 36 : 60;
        synth->setRateAndBufferSizeDetails(48000,2048);
        synth->prepareToPlay(48000,2048);
        const auto render = [&](int program) {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> audio(2,2048);
            audio.clear();
            midi.addEvent(juce::MidiMessage::allSoundOff(ch),0);
            if (program >= 0) midi.addEvent(juce::MidiMessage::programChange(ch,program),0);
            midi.addEvent(juce::MidiMessage::noteOn(ch,key,.88f),16);
            synth->processBlock(audio,midi);
            return audio;
        };
        std::array<juce::AudioBuffer<float>,4> patches;
        for (int p = 0; p < 4; ++p) {
            patches[p] = render(p);
            if (patches[p].getMagnitude(0,16) != 0 || patches[p].getMagnitude(16,2032) < .0003f) return 4;
            for (int j = 0; j < p; ++j) {
                double diff = 0;
                for (int n = 0; n < 2048; ++n) diff += std::abs(patches[p].getSample(0,n)-patches[j].getSample(0,n));
                if (!std::isfinite(diff) || diff < .05) return 5;
            }
        }
        juce::MemoryBlock saved;
        synth->getStateInformation(saved);
        (void)render(0);
        synth->setStateInformation(saved.getData(),int(saved.getSize()));
        const auto recalled = render(-1);
        for (int n = 0; n < 2048; ++n)
            if (std::abs(recalled.getSample(0,n)-patches[3].getSample(0,n)) > .0001f) return 6;
        synth->releaseResources();
        tracks[track] = {{}, synth.get(), ch};
        instances.push_back(std::move(synth));
    }
    PreviewTransport transport;
    transport.setOutputMix(mix.routing);
    for (const auto& id : ids) {
        const int track = song->trackIds.indexOf(id);
        transport.setChannelMix(track, mix.tracks[track], true, 0);
    }
    if (transport.preparePlayback(*song,tracks,{},48000,256).failed()) return 7;
    // Daybreak starts with the authored program changes on all four channels.
    transport.seekToTick(48 * 4 * song->ppq);
    std::array<float,4> peaks {};
    juce::AudioBuffer<float> block(2,256);
    for (int b = 0; b < 600; ++b) {
        transport.renderBlock(block.getArrayOfWritePointers(),2,256);
        const auto meters = transport.takeMeters();
        for (size_t i = 0; i < ids.size(); ++i)
            peaks[i] = std::max(peaks[i], meters.tracks[song->trackIds.indexOf(ids[i])][0]);
    }
    transport.stop();
    for (size_t i = 0; i < peaks.size(); ++i) {
        if (!std::isfinite(peaks[i]) || peaks[i] <= .0001f) {
            std::cerr << "Silent or invalid demo stem: " << ids[i] << " peak " << peaks[i] << '\n';
            return 8;
        }
    }
    auto singer = load(formats, juce::File(args[1]).getChildFile("OSTF Formant Singer.vst3"));
    auto vocalFile = juce::File(args[2]).getSiblingFile("paper-lantern-replies.ostfp");
    std::unique_ptr<ostfstudio::OstfpPreview> vocal;
    juce::FileInputStream vocalInput(vocalFile);
    if (!singer || ostfstudio::readOstfpPreview(vocalInput,vocal).failed()) return 9;
    const int voice = vocal->trackIds.indexOf("voice-track");
    if (voice<0 || vocal->pitchedNotes[voice].empty() || !vocal->savedMix ||
        !matchesVst3Instrument(vocal->savedMix->instruments[voice],singer->getPluginDescription())) return 10;
    std::vector<PreviewTransport::TrackSetup> vocalRoutes(vocal->trackLanes.size());
    vocalRoutes[voice].instrument=singer.get();
    auto renderVowel = [&](const juce::String& vowel) {
        if (vowel.isNotEmpty()) {
            auto score = juce::JSON::parse(vocal->sourceScore.toString());
            for (auto& unit : *score["lyrics"]["units"].getArray())
                unit.getDynamicObject()->setProperty("pronunciation",vowel);
            const auto json = juce::JSON::toString(score);
            vocal->sourceScore = juce::MemoryBlock(json.toRawUTF8(),json.getNumBytesAsUTF8());
        }
        PreviewTransport preview;
        preview.setChannelMix(voice,{},true,0);
        preview.setOutputMix({});
        juce::AudioBuffer<float> result(2,2048);
        result.clear();
        const auto ready = preview.preparePlayback(*vocal,vocalRoutes,{},48000,2048);
        if (ready.failed()) std::cerr << ready.getErrorMessage() << '\n';
        if (ready.wasOk()) {
            preview.seekToTick(vocal->pitchedNotes[voice].front().startTick + 40);
            preview.renderBlock(result.getArrayOfWritePointers(),2,result.getNumSamples());
            preview.stop();
        }
        return result;
    };
    const auto authored = renderVowel({});
    if (authored.getMagnitude(0, 0, authored.getNumSamples()) < .001f) return 11;
    const auto aa=renderVowel("a"), ii=renderVowel("i");
    double delta=0;
    for (int n=0;n<aa.getNumSamples();++n) delta+=std::abs(aa.getSample(0,n)-ii.getSample(0,n));
    const auto aLevel = aa.getMagnitude(0,0,2048), iLevel = ii.getMagnitude(0,0,2048);
    if (aLevel < .001f || iLevel < .001f || !(delta > 1)) {
        std::cerr << "Hosted singer vowel peaks " << aLevel << ", " << iLevel << ", difference " << delta << '\n';
        return 11;
    }
    return 0;
}

static bool checkCueMonitoring(ostfstudio::OstfpPreview& song) {
    auto copy = song.audio.front();
    const auto tick = copy.startTick;
    const auto first = copy.track;
    const auto second = (first + 1) % song.sourceTrackCount();
    copy.track = second;
    song.audio.push_back(copy);
    PreviewTransport engine;
    const auto ready = engine.preparePlayback(song, {}, {}, 48000, 256);
    song.audio.pop_back();
    if (ready.failed()) return false;
    engine.setOutputMix({});
    ostfstudio::ChannelMix mix;
    mix.cue = true;
    mix.volumeDb = -40;
    engine.setChannelMix(first, mix, false, -12);
    engine.setChannelMix(second, {}, false, -12);
    auto render = [&](int outputs, bool listen, std::optional<float> expectedCue = {}) {
        engine.setListenToCue(listen);
        engine.seekToTick(tick);
        juce::AudioBuffer<float> buffer(outputs, 256);
        float peak = 0;
        for (int n = 0; n < 24; ++n) {
            engine.renderBlock(buffer.getArrayOfWritePointers(), outputs, 256);
            const auto frame = engine.takeMeters();
            if (frame.outputs[0] != ostfstudio::StereoPeak{}) return -1.f;
            for (int c = 0; c < 2; ++c) {
                peak = std::max(peak, frame.cue[c]);
                if (std::abs(buffer.getMagnitude(c, 0, 256) - (listen ? frame.cue[c] : 0.f)) > .00001f) return -1.f;
                if (outputs == 4 && std::abs(buffer.getMagnitude(c + 2, 0, 256) - frame.cue[c]) > .00001f) return -1.f;
            }
            if (outputs == 3 && buffer.getMagnitude(2, 0, 256) != 0) return -1.f;
        }
        return expectedCue && std::abs(peak - *expectedCue) > .00001f ? -1.f : peak;
    };
    const auto one = render(2, false);
    if (one <= .0001f) return false;
    engine.setChannelMix(second, mix, false, -12);
    if (render(4, false, 2 * one) < 0 || render(2, true, 2 * one) < 0
        || render(3, false, 2 * one) < 0) return false;
    engine.setChannelMix(first, mix, false, -18);
    mix.cue = false;
    engine.setChannelMix(second, mix, false, -18);
    if (render(4, true, one * ostfstudio::gainFromDb(-6)) < 0) return false;
    engine.setChannelMix(first, mix, false, -18);
    if (render(2, true, 0.f) != 0) return false;
    mix.volumeDb = 0;
    engine.setChannelMix(first, mix, true, 0);
    juce::AudioBuffer<float> audio(2, 256);
    for (bool listen : {true, false}) {
        engine.setListenToCue(listen);
        engine.seekToTick(tick);
        float masterPeak = 0;
        for (int n = 0; n < 24; ++n) {
            engine.renderBlock(audio.getArrayOfWritePointers(), 2, 256);
            const auto frame = engine.takeMeters();
            if (frame.cue != ostfstudio::StereoPeak{}) return false;
            for (int c = 0; c < 2; ++c) {
                masterPeak = std::max(masterPeak, frame.outputs[0][c]);
                if (std::abs(audio.getMagnitude(c, 0, 256) - (listen ? 0.f : frame.outputs[0][c])) > .00001f) return false;
            }
        }
        if (masterPeak < .0001f) return false;
    }
    return true;
}

int checkStockEffects(const juce::StringArray& args) {
    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    const juce::File directory(args[1]);
    for (const auto& fx : ostfstudio::stockEffects) {
        auto effect = load(formats, directory.getChildFile(juce::String(fx.name) + ".vst3"));
        if (!effect || !checkEffect(*effect, fx)) {
            std::cerr << "Failed VST3 render/state test: " << fx.name << '\n';
            return 10 + static_cast<int>(&fx - ostfstudio::stockEffects);
        }
    }

    // Play the jazz demo's opening acoustic piano through its authored harmony bus and room send.
    juce::FileInputStream file{juce::File(args[3])};
    std::unique_ptr<ostfstudio::OstfpPreview> song;
    if (ostfstudio::readOstfpPreview(file, song).failed() || !song->savedMix) return 21;
    const auto& mix = *song->savedMix;
    const auto score = juce::JSON::parse(song->sourceScore.toString());
    int piano = -1;
    for (int i = 0; i < score["tracks"].size(); ++i) if (score["tracks"][i]["id"] == "keys") piano = i;
    if (piano < 0 || mix.tracks[piano].outputBus != 1 || mix.tracks[piano].returnSends[0] <= 0) return 22;
    std::vector<Instance> plugins;
    plugins.push_back(load(formats, juce::File(args[2])));
    if (!plugins.back() || !matchesVst3Instrument(mix.instruments[piano], plugins.back()->getPluginDescription())) return 23;
    std::vector<PreviewTransport::TrackSetup> tracks(song->trackLanes.size());
    tracks[piano] = {{}, plugins.back().get(), mix.midiRoutes[piano].channel};
    PreviewTransport::Inserts inserts{};
    auto restore = [&](const juce::StringArray& chain, std::size_t channel) {
        for (int slot = 0; slot < chain.size(); ++slot) {
            const auto fx = std::find_if(std::begin(ostfstudio::stockEffects), std::end(ostfstudio::stockEffects),
                                         [&](const auto& item) { return item.chainId() == chain[slot]; });
            if (fx == std::end(ostfstudio::stockEffects)) return false;
            plugins.push_back(load(formats, directory.getChildFile(juce::String(fx->name) + ".vst3")));
            if (!plugins.back()) return false;
            inserts[channel][slot] = plugins.back().get();
        }
        return true;
    };
    if (!restore(mix.trackEffects[piano], PreviewTransport::outputCount + piano)) return 24;
    for (std::size_t i = 0; i < mix.outputEffects.size(); ++i) if (!restore(mix.outputEffects[i], i)) return 25;
    bool metersMatchAudio = true;
    auto render = [&](bool bus, bool room, bool send, bool master, bool cueOnly = false) {
        PreviewTransport transport;
        auto routing = mix.routing;
        routing.outputMuted[2] = !bus;
        routing.outputMuted[3] = !room;
        routing.outputMuted[0] = !master;
        transport.setOutputMix(routing);
        auto channel = mix.tracks[piano];
        if (!send) channel.returnSends[0] = 0;
        channel.cue = true;
        if (cueOnly) channel.pan = 1.0f;
        transport.setChannelMix(piano, channel, !cueOnly, 0);
        if (transport.preparePlayback(*song, tracks, inserts, 48000, 256).failed()) return -1.0;
        juce::AudioBuffer<float> audio(cueOnly ? 4 : 2, 256);
        std::array<float, 4> actualPeak{};
        double energy = 0;
        for (int block = 0; block < 375; ++block) {
            transport.renderBlock(audio.getArrayOfWritePointers(), audio.getNumChannels(), 256);
            for (int c = 0; c < audio.getNumChannels(); ++c)
                actualPeak[c] = std::max(actualPeak[c], audio.getMagnitude(c, 0, 256));
            energy += audio.getRMSLevel(0, 0, 256);
        }
        const auto frame = transport.takeMeters();
        for (int c = 0; c < 2; ++c) {
            metersMatchAudio &= std::abs(std::min(1.0f, frame.outputs[0][c]) - actualPeak[c]) < .00001f;
            if (audio.getNumChannels() == 4)
                metersMatchAudio &= std::abs(std::min(1.0f, frame.cue[c]) - actualPeak[c + 2]) < .00001f;
            else
                metersMatchAudio &= frame.cue[c] > .00001f;
            metersMatchAudio &= (frame.tracks[piano][c] > 0.00001f) == !cueOnly;
            metersMatchAudio &= (frame.outputs[2][c] > 0.00001f) == (bus && !cueOnly);
            metersMatchAudio &= (frame.outputs[3][c] > 0.00001f) == (room && send && !cueOnly);
        }
        if (cueOnly) metersMatchAudio &= frame.cue[0] == 0 && frame.cue[1] > .00001f;
        const auto drained = transport.takeMeters();
        metersMatchAudio &= drained.tracks[piano] == ostfstudio::StereoPeak{} && drained.outputs[0] == ostfstudio::StereoPeak{}
                         && drained.cue == ostfstudio::StereoPeak{};
        return energy;
    };
    if (!(render(true, false, true, true) > .001 && render(false, true, true, true) > .0001
        && render(false, true, false, true) == 0 && render(true, true, true, false) == 0
        && render(true, true, true, true, true) == 0 && metersMatchAudio)) return 26;

    // An actual hosted GM instrument must sound mid-note after a locate,
    // and become silent on an immediate jump to an empty part of the score.
    auto isolated = juce::JSON::parse(song->sourceScore.toString());
    isolated["tracks"][piano].getDynamicObject()->setProperty("clips", juce::Array<juce::var>{});
    const auto text = juce::JSON::toString(isolated);
    juce::MemoryInputStream input(text.toRawUTF8(), text.getNumBytesAsUTF8(), false);
    std::unique_ptr<ostfstudio::OstfpPreview> edited;
    if (ostfstudio::readOstfPreview(input, edited).failed()) return 32;
    juce::MemoryBlock before;
    juce::StringArray ids;
    ostfstudio::PitchedNote authored {0, 2 * edited->ppq, 64, 100};
    if (edited->editNotes(piano, {{ostfstudio::NoteEdit::insert, authored}}, before, ids).failed()) return 33;
    authored.id = ids[0];
    authored.startTick += edited->ppq;
    authored.endTick += edited->ppq;
    if (edited->editNotes(piano, {{ostfstudio::NoteEdit::change, authored}}, before, ids).failed()) return 34;
    PreviewTransport transport;
    transport.setOutputMix({});
    transport.setChannelMix(piano, {}, true, 0);
    if (transport.preparePlayback(*edited, tracks, {}, 48000, 256).failed()) return 27;
    juce::AudioBuffer<float> audio(2, 256);
    const auto energyAt = [&](juce::int64 tick) {
        transport.seekToTick(tick);
        float peak = 0;
        for (int block = 0; block < 12; ++block) {
            transport.renderBlock(audio.getArrayOfWritePointers(), 2, 256);
            peak = std::max(peak, audio.getMagnitude(0, 0, 256));
        }
        return peak;
    };
    if (energyAt(2 * song->ppq) <= .0001f || energyAt(4 * song->ppq) > .00001f
        || energyAt(2 * song->ppq) <= .0001f || energyAt(0) > .00001f) return 28;
    transport.stop();
    juce::FileInputStream mediaFile(juce::File(args[3]).getSiblingFile("paper-lantern-replies.ostfp"));
    std::unique_ptr<ostfstudio::OstfpPreview> cue;
    if (ostfstudio::readOstfpPreview(mediaFile, cue).failed() || cue->audio.size() != 1 || !cue->savedMix) return 29;
    if (!checkCueMonitoring(*cue)) { std::cerr << "Cue bus or monitor routing failed\n"; return 52; }
    const auto lyricScore = juce::JSON::parse(cue->sourceScore.toString());
    const auto lyricId = lyricScore["lyrics"]["alignments"][0]["noteIds"][0].toString();
    bool testedLyric = false;
    for (std::size_t i = 0; i < cue->sourceTrackCount() && !testedLyric; ++i) {
        const auto& notes = cue->pitchedNotes[i];
        auto pos = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == lyricId; });
        if (pos == notes.end()) continue;
        auto linked = *pos;
        const auto original = cue->sourceScore;
        if (cue->editNotes(i, {{ostfstudio::NoteEdit::erase, linked}}, before, ids).wasOk() || cue->sourceScore != original) return 35;
        linked.velocity = 73;
        if (cue->editNotes(i, {{ostfstudio::NoteEdit::change, linked}}, before, ids).failed() || ids[0] != lyricId) return 36;
        if (juce::JSON::toString(juce::JSON::parse(cue->sourceScore.toString())["lyrics"]) != juce::JSON::toString(lyricScore["lyrics"])) return 37;
        testedLyric = true;
    }
    if (!testedLyric) return 38;
    const auto& region = cue->audio.front();
    const auto& mixAudio = *cue->savedMix;
    inserts = {};
    if (!restore(mixAudio.trackEffects[region.track], PreviewTransport::outputCount + region.track)
        || !restore(mixAudio.outputEffects[3], 3)) return 30;
    auto cuePeak = [&](juce::int64 tick, bool soloOther, bool mute, bool bus, bool send, double db, bool wet) {
        PreviewTransport playback;
        auto channels = mixAudio.tracks;
        auto& strip = channels[region.track];
        strip.muted = mute;
        strip.soloed = !soloOther;
        channels[0].soloed = soloOther;
        strip.volumeDb = db;
        if (!send) strip.returnSends[0] = 0;
        auto routing = mixAudio.routing;
        routing.outputMuted[2] = !bus;
        playback.setOutputMix(routing);
        playback.setChannelMix(region.track, strip, ostfstudio::channelIsAudible(channels, region.track), 0);
        if (playback.preparePlayback(*cue, {}, wet ? inserts : PreviewTransport::Inserts{}, 48000, 256).failed()) return -1.f;
        playback.seekToTick(tick);
        float peak = 0.f;
        for (int i = 0; i < 24; ++i) {
            playback.renderBlock(audio.getArrayOfWritePointers(), 2, 256);
            peak = std::max(peak, audio.getMagnitude(0, 0, 256));
        }
        return peak;
    };
    const auto at = region.startTick;
    const auto dry = cuePeak(at, false, false, true, false, 0, false);
    const auto half = cuePeak(at, false, false, true, false, -6, false);
    if (dry <= .01f || std::abs(half / dry - .5012f) > .002f
        || cuePeak(0, false, false, true, false, 0, false) != 0
        || cuePeak(at + region.durationTicks, false, false, true, false, 0, false) != 0
        || cuePeak(at, true, false, true, true, 0, true) != 0
        || cuePeak(at, false, true, true, true, 0, true) != 0
        || cuePeak(at, false, false, false, false, 0, true) != 0
        || cuePeak(at, false, false, false, true, 0, true) <= .00001f
        || std::abs(cuePeak(at, false, false, true, false, 0, true) - dry) < .00001f) return 31;
    // Seek into the overlap, with only the authored audio tracks feeding the two buses.
    for (const auto* file : {"relay-circuit.ostfp", "first-light-vector.ostfp"}) {
        juce::FileInputStream source(juce::File(args[3]).getSiblingFile(file));
        std::unique_ptr<ostfstudio::OstfpPreview> club;
        if (ostfstudio::readOstfpPreview(source, club).failed() || !club->savedMix) return 39;
        PreviewTransport playback;
        playback.setOutputMix(club->savedMix->routing);
        for (const auto& clip : club->audio)
            playback.setChannelMix(clip.track, club->savedMix->tracks[clip.track], true, 0);
        if (playback.preparePlayback(*club, {}, {}, 48000, 256).failed()) return 40;
        const auto tick = club->audio[club->audio.size() - 2].startTick;
        playback.seekToTick(tick);
        float master = 0;
        for (int block = 0; block < 120; ++block) {
            playback.renderBlock(audio.getArrayOfWritePointers(), 2, 256);
            master = std::max(master, audio.getMagnitude(0, 0, 256));
        }
        const auto meters = playback.takeMeters();
        const auto sounding = std::count_if(club->audio.begin(), club->audio.end(), [&](const auto& region) {
            return meters.tracks[region.track][0] > .001f;
        });
        if (master <= .001f || sounding != 2) return 41;
    }
    return 0;
}
