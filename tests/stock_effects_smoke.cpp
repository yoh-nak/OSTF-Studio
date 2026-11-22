#include "StockEffects.h"
#include "ui/PreviewTransport.h"
#include "ui/PluginRoles.h"
#include <cmath>
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

bool checkEffect(juce::AudioPluginInstance& instance, const ostfstudio::StockEffect& fx) {
    const auto desc = instance.getPluginDescription();
    if (!PreviewTransport::supportsInsert(instance) || desc.manufacturerName != "OSTF Studio"
        || fx.chainId() != "VST3|OSTF Studio|" + desc.name + "|" + juce::String(desc.uniqueId)) return false;
    instance.setRateAndBufferSizeDetails(48000, 256);
    instance.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    double difference = 0, tail = 0;
    float peak = 0;
    for (int block = 0; block < 160; ++block) {
        for (int i = 0; i < 256; ++i) {
            const float x = block < 90 ? static_cast<float>(1.2 * std::sin(.117 * (block * 256 + i))) : 0.f;
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
    auto render = [&](bool bus, bool room, bool send, bool master) {
        PreviewTransport transport;
        auto routing = mix.routing;
        routing.outputMuted[2] = !bus;
        routing.outputMuted[3] = !room;
        routing.outputMuted[0] = !master;
        transport.setOutputMix(routing);
        auto channel = mix.tracks[piano];
        if (!send) channel.returnSends[0] = 0;
        transport.setChannelMix(piano, channel, true, 0);
        if (transport.preparePlayback(*song, tracks, inserts, 48000, 256).failed()) return -1.0;
        juce::AudioBuffer<float> audio(2, 256);
        double energy = 0;
        for (int block = 0; block < 375; ++block) {
            transport.renderBlock(audio.getArrayOfWritePointers(), 2, 256);
            energy += audio.getRMSLevel(0, 0, 256);
        }
        return energy;
    };
    return render(true, false, true, true) > .001 && render(false, true, true, true) > .0001
        && render(false, true, false, true) == 0 && render(true, true, true, false) == 0 ? 0 : 26;
}
