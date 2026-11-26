#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

juce::AudioProcessor* createOstfRack(bool);

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3) return 1;
    juce::VST3PluginFormat vst;
    juce::OwnedArray<juce::PluginDescription> synths,effects;
    vst.findAllTypesForFile(synths,juce::String::fromUTF8(argv[1]));
    vst.findAllTypesForFile(effects,juce::String::fromUTF8(argv[2]));
    if(synths.size()!=1 || effects.size()!=1) return 2;
    for(bool instrument:{false,true}) {
        auto render=[&](int count,bool missing=false,bool bypassFirst=false){
            std::unique_ptr<juce::AudioProcessor> processor(createOstfRack(instrument));
            juce::XmlElement xml("ParallelRack");xml.setAttribute("instrument",instrument);
            for(int i=0;i<count;++i) {
                auto* layer=xml.createNewChildElement("Layer");
                layer->setAttribute("level",.25);
                if(instrument) layer->createNewChildElement("Device")->addChildElement(synths[0]->createXml().release());
                auto effect = *effects[0];
                if (missing) effect.fileOrIdentifier = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("Unavailable test effect.vst3").getFullPathName();
                auto* effectNode = layer->createNewChildElement("Device");
                effectNode->addChildElement(effect.createXml().release());
                if (bypassFirst) layer->getFirstChildElement()->setAttribute("bypass", true);
                if (missing) effectNode->setAttribute("state", juce::MemoryBlock("saved",5).toBase64Encoding());
            }
            juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(xml,state);
            processor->setStateInformation(state.getData(),int(state.getSize()));
            processor->setRateAndBufferSizeDetails(48000,256);processor->prepareToPlay(48000,256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(100)),32);
            double energy=0.;
            for(int b=0;b<12;++b) {
                juce::AudioBuffer<float> audio(2,256);audio.clear();
                if(!instrument) for(int ch=0;ch<2;++ch) for(int i=0;i<256;++i) audio.setSample(ch,i,.2f*std::sin((256*b+i)*.11f));
                processor->processBlock(audio,midi);midi.clear();
                if(b==0 && instrument && audio.getMagnitude(0,0,32)>1.e-6f) return -1.;
                energy+=audio.getRMSLevel(0,0,256);
            }
            processor->getStateInformation(state);
            auto saved=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()));
            if(!saved || saved->getNumChildElements()!=count) return -1.;
            for(auto* layer:saved->getChildWithTagNameIterator("Layer")) {
                if(layer->getNumChildElements()!=(instrument?2:1) || std::abs(layer->getDoubleAttribute("level")-.25)>.001) return -1.;
                const auto* last = layer->getChildElement(layer->getNumChildElements() - 1);
                if (layer->getFirstChildElement()->getBoolAttribute("bypass") != bypassFirst) return -1.;
                if (missing && last->getStringAttribute("state") != juce::MemoryBlock("saved",5).toBase64Encoding()) return -1.;
            }
            processor->releaseResources();
            return energy;
        };
        const double one=render(1),two=render(2);
        const auto bypassed=render(1,!instrument,true);
        if (instrument ? bypassed!=0. : bypassed<=0.) return 4;
        if(one<.001 || !std::isfinite(two) || std::abs(two/one-2.)>.025 || render(1,true)!=0.) {
            std::cerr<<(instrument?"Instrument":"Effect")<<" rack: one layer "<<one<<", two layers "<<two<<'\n';return 3;
        }
    }
    return 0;
}
