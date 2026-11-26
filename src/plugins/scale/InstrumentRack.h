#pragma once
#include "MidiThru.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <optional>

namespace ostfstudio::scale {
// The processor callback lock protects replacement and preview scheduling. The audio
// thread renders only the installed instrument and never scans or instantiates plug-ins.
class InstrumentRack {
public:
    explicit InstrumentRack(juce::AudioProcessor& p) : owner(p) {
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    }
    juce::String name() const { return child ? child->getName() : "Reference tone"; }
    std::optional<juce::PluginDescription> description() const {
        if (child) return child->getPluginDescription();
        return {};
    }
    juce::Result choose(const juce::File& file,const juce::String& className={}) {
        juce::OwnedArray<juce::PluginDescription> types;
        formats.getFormat(0)->findAllTypesForFile(types,file.getFullPathName());
        for (auto* type : types)
            if (type->isInstrument && type->name != owner.getName() && (className.isEmpty() || type->name==className)) return install(*type,{});
        return juce::Result::fail("Select a VST3 instrument other than Scale Editor.");
    }
    juce::Result install(const juce::PluginDescription& type,const juce::MemoryBlock& state) {
        if (!type.isInstrument || type.pluginFormatName != "VST3" || type.name == owner.getName())
            return juce::Result::fail("Unsupported instrument.");
        juce::String error;
        double loadRate; int loadBlock;
        { const juce::ScopedLock lock(owner.getCallbackLock()); loadRate=rate; loadBlock=blockSize; }
        auto next = formats.createPluginInstance(type,loadRate,loadBlock,error);
        if (!next) return juce::Result::fail(error);
        if (next->getName() == owner.getName() || !next->acceptsMidi())
            return juce::Result::fail("This instrument cannot receive notes here.");
        if (state.getSize()) next->setStateInformation(state.getData(),int(state.getSize()));
        next->disableNonMainBuses();
        const auto outputs = next->getMainBusNumOutputChannels();
        if (outputs < 1 || next->getTotalNumOutputChannels() > 2 || next->getTotalNumInputChannels() > 2)
            return juce::Result::fail("Select a mono or stereo instrument.");
        closeWindow();
        {
            const juce::ScopedLock lock(owner.getCallbackLock());
            if (prepared) { next->setRateAndBufferSizeDetails(rate,blockSize); next->prepareToPlay(rate,blockSize); }
            child.swap(next);
            stopPreview();
            voices = {};
        }
        if (next) next->releaseResources();
        return juce::Result::ok();
    }
    void prepare(double sr,int size) {
        rate=sr; blockSize=size; prepared=true; voices={}; stopPreview();
        previewOut.ensureSize(32768);
        keyboardIn.ensureSize(32768);
        if (child) { child->setRateAndBufferSizeDetails(sr,size); child->prepareToPlay(sr,size); }
    }
    void release() {
        if (child && prepared) child->releaseResources();
        prepared=false; voices={}; stopPreview();
    }
    struct Window final : juce::DocumentWindow {
        explicit Window(const juce::String& title) : DocumentWindow(title,juce::Colour(0xff202e36),closeButton) {}
        void closeButtonPressed() override { setVisible(false); }
    };
    void closeWindow() { window.reset(); }
    void openWindow() {
        if (!child) return;
        if (!window) {
            auto editor = std::unique_ptr<juce::AudioProcessorEditor>(child->createEditorIfNeeded());
            if (!editor) editor=std::make_unique<juce::GenericAudioProcessorEditor>(*child);
            window=std::make_unique<Window>(child->getName());
            window->setUsingNativeTitleBar(true);
            window->setContentOwned(editor.release(),true);
            window->centreWithSize(window->getWidth(),window->getHeight());
        }
        window->setVisible(true); window->toFront(true);
    }
    void save(juce::XmlElement& xml) {
        const juce::ScopedLock lock(owner.getCallbackLock());
        if (!child) return;
        auto* entry=xml.createNewChildElement("Instrument");
        entry->addChildElement(child->getPluginDescription().createXml().release());
        juce::MemoryBlock bytes;
        child->getStateInformation(bytes);
        entry->setAttribute("state",bytes.toBase64Encoding());
    }
    juce::Result restore(const juce::XmlElement* entry) {
        if (!entry) {
            closeWindow();
            std::unique_ptr<juce::AudioPluginInstance> previous;
            { const juce::ScopedLock lock(owner.getCallbackLock()); previous.swap(child); stopPreview(); voices={}; }
            if(previous) previous->releaseResources();
            return juce::Result::ok();
        }
        juce::PluginDescription type;
        juce::MemoryBlock bytes;
        if (!entry->getFirstChildElement() || !type.loadFromXml(*entry->getFirstChildElement())
            || !bytes.fromBase64Encoding(entry->getStringAttribute("state")))
            return juce::Result::fail("Unable to restore the hosted instrument.");
        return install(type,bytes);
    }
    void audition(const juce::var& notes, bool localOnly=false) {
        std::vector<Event> next;
        if (const auto* items=notes.getArray()) for (const auto& item:*items) {
            const int key=item["note"], vel=item["velocity"], at=item["at"], ms=item["ms"];
            if(key<0 || key>127 || vel<1 || vel>127 || at<0 || ms<1 || at+double(ms)>180000) continue;
            next.push_back({at,key,vel}); next.push_back({at+ms,key,0});
        }
        std::stable_sort(next.begin(),next.end(),[](const Event& a,const Event& b){return a.ms<b.ms || (a.ms==b.ms && a.velocity<b.velocity);});
        const juce::ScopedLock lock(owner.getCallbackLock());
        stopPreview(); sequence.swap(next);
        auditionLocally=localOnly;
    }
    void stopPreview() {
        const juce::ScopedLock lock(owner.getCallbackLock());
        sequence.clear(); cursor=0; elapsed=0; flush=true;
    }
    void setPreviewMidiOut(bool enabled) {
        const juce::ScopedLock lock(owner.getCallbackLock());
        if (external.load()==enabled) return;
        stopPreview();
        external.store(enabled);
    }
    bool previewMidiOut() const {return external.load();}
    bool midiThruEnabled() const {return thruEnabled.load();}
    void setMidiThru(bool enabled) {
        const juce::ScopedLock lock(owner.getCallbackLock());
        if (thruEnabled.exchange(enabled) && !enabled) releaseKeys=true;
    }
    void resetInput() {
        const juce::ScopedLock lock(owner.getCallbackLock());
        releaseKeys=true;
    }
    void process(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi) {
        audio.clear();
        keyboardIn.clear();
        if (thruEnabled.load()) keyboardIn.addEvents(midi,0,-1,0);
        previewOut.clear();
        if(flush) {
            auto& previous = lastExternal ? previewOut : midi;
            for(int k=0;k<128;++k) if(sounding[size_t(k)]) previous.addEvent(juce::MidiMessage::noteOff(previewChannel,k),0);
            sounding={}; flush=false;
        }
        lastExternal=external.load() && !auditionLocally;
        auto& destination=lastExternal ? previewOut : midi;
        const int count=audio.getNumSamples();
        while(cursor<sequence.size()) {
            const auto& e=sequence[cursor];
            const auto pos=std::llround(e.ms * .001 * rate)-elapsed;
            if(pos>=count) break;
            const auto offset=int(std::max<juce::int64>(0,pos));
            auto& active=sounding[size_t(e.key)];
            if(e.velocity ? (!lastExternal || active==0) : active==1)
                destination.addEvent(e.velocity ? juce::MidiMessage::noteOn(previewChannel,e.key,(juce::uint8)e.velocity)
                                         : juce::MidiMessage::noteOff(previewChannel,e.key),offset);
            active=e.velocity ? active+1 : std::max(0,active-1);
            ++cursor;
        }
        if(cursor<sequence.size()) elapsed+=count;
        if(child) {
            child->setPlayHead(owner.getPlayHead());
            const int channels=std::max(child->getTotalNumInputChannels(),child->getTotalNumOutputChannels());
            juce::AudioBuffer<float> bus(audio.getArrayOfWritePointers(),channels,count);
            child->processBlock(bus,midi);
            if(child->getMainBusNumOutputChannels()==1) audio.copyFrom(1,0,audio,0,0,count);
        } else {
            int pos=0;
            for(const auto message:midi) {
                const auto stop=juce::jlimit(pos,count,message.samplePosition);
                reference(audio,pos,stop); pos=stop;
                const auto m=message.getMessage();
                if(m.isNoteOn()) {
                    auto* voice=&voices[0];
                    for(auto& v:voices) {
                        if(v.amp==0 || (v.channel==m.getChannel() && v.key==m.getNoteNumber())) {voice=&v;break;}
                        if(v.amp<voice->amp) voice=&v;
                    }
                    *voice={m.getChannel(),m.getNoteNumber(),0.,juce::MathConstants<double>::twoPi*juce::MidiMessage::getMidiNoteInHertz(m.getNoteNumber())/rate,m.getFloatVelocity()*.12f,false};
                } else if(m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff()) {
                    for(auto& v:voices) if(v.channel==m.getChannel() && (!m.isNoteOff() || v.key==m.getNoteNumber())) v.releasing=true;
                }
            }
            reference(audio,pos,count);
            audio.copyFrom(1,0,audio,0,0,count);
        }
        thru.write(midi,keyboardIn,previewOut,releaseKeys);
        releaseKeys=false;
    }
    bool hasInstrument() const {return child!=nullptr;}
private:
    static constexpr int previewChannel=MidiThru::auditionChannel;
    struct Event { int ms,key,velocity; };
    struct Voice { int channel{},key{}; double phase{},step{}; float amp{}; bool releasing{}; };
    void reference(juce::AudioBuffer<float>& audio,int from,int to) {
        auto* samples=audio.getWritePointer(0);
        const auto decay=float(std::exp(-1./(.016*rate)));
        for(auto& v:voices) if(v.amp>0) for(int i=from;i<to;++i) {
            samples[i]+=v.amp*float(std::sin(v.phase)); v.phase+=v.step;
            if(v.phase>=juce::MathConstants<double>::twoPi) v.phase-=juce::MathConstants<double>::twoPi;
            if(v.releasing && (v.amp*=decay)<.00005f){v.amp=0;break;}
        }
    }
    juce::AudioProcessor& owner;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> child;
    std::unique_ptr<Window> window;
    std::array<Voice,32> voices{};
    std::array<int,128> sounding{};
    juce::MidiBuffer previewOut,keyboardIn;
    MidiThru thru;
    std::atomic<bool> thruEnabled{true};
    bool releaseKeys=false;
    std::atomic<bool> external{false};
    bool lastExternal=false, auditionLocally=false;
    std::vector<Event> sequence;
    size_t cursor=0;
    juce::int64 elapsed=0;
    double rate=48000.;
    int blockSize=512;
    bool prepared=false,flush=false;
};
}
