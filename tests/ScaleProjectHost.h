#pragma once
#include "../src/plugins/scale/ScoreExchange.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>

// Exercise the same in-process interface through a packaged VST3 or the test processor.
class ScaleProjectHost final : public ostfstudio::scale::ProjectWriter {
public:
    explicit ScaleProjectHost(juce::AudioProcessor& processor) {
        void* result=nullptr;
        using namespace ostfstudio::scale;
        if (auto* plugin=dynamic_cast<juce::AudioPluginInstance*>(&processor)) {
            if (auto* client=plugin->getVST3Client())
                client->getIComponentPtr()->queryInterface(projectScalesId,&result);
        } else if (auto* extensions=processor.getVST3ClientExtensions()) {
            extensions->queryIAudioProcessor(projectScalesId,&result);
        }
        input=Steinberg::owned(static_cast<ProjectScales*>(result));
    }
    bool publish(const juce::String& score) {
        if (!input || input->syncProject(this,score.toRawUTF8(),int(score.getNumBytesAsUTF8()))!=Steinberg::kResultOk) return false;
        current=score;
        return true;
    }
    Steinberg::tresult PLUGIN_API applyScales(const char* base,const char* bytes,Steinberg::int32 size) override {
        if (!base || !bytes || size<=0 || juce::SHA256(current.toRawUTF8(), current.getNumBytesAsUTF8()).toHexString()!=base)
            return Steinberg::kResultFalse;
        return publish(juce::String::fromUTF8(bytes,size)) ? Steinberg::kResultOk : Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID id,void** out) override {
        if (!out) return Steinberg::kInvalidArgument;
        *out=nullptr;
        if (std::memcmp(id,Steinberg::FUnknown_iid,16)) return Steinberg::kNoInterface;
        *out=this; addRef(); return Steinberg::kResultOk;
    }
    Steinberg::uint32 PLUGIN_API addRef() override {return ++refs;}
    Steinberg::uint32 PLUGIN_API release() override {
        auto n=--refs;
        if (!n) delete this;
        return n;
    }
    juce::String current;
private:
    std::atomic<Steinberg::uint32> refs{1};
    Steinberg::IPtr<ostfstudio::scale::ProjectScales> input;
};
