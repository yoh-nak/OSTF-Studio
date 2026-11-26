#include "../StudioPluginEditor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

namespace {
using Graph = juce::AudioProcessorGraph;
using IO = Graph::AudioGraphIOProcessor;
using Node = Graph::Node::Ptr;
using Update = Graph::UpdateKind;

class ProcessorBase : public juce::AudioProcessor {
public:
    explicit ProcessorBase(const BusesProperties& buses) : AudioProcessor(buses) {}
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    double getTailLengthSeconds() const override { return 0.; }
    static BusesProperties stereo(bool input) {
        auto buses=BusesProperties();
        if(input) buses=buses.withInput("Input",juce::AudioChannelSet::stereo(),true);
        return buses.withOutput("Output",juce::AudioChannelSet::stereo(),true);
    }
};

class LayerGain final : public ProcessorBase {
public:
    LayerGain() : ProcessorBase(stereo(true)) {}
    const juce::String getName() const override { return "Layer level"; }
    bool acceptsMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    void prepareToPlay(double,int) override { last=level.load(); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&) override {
        const auto target=level.load();
        audio.applyGainRamp(0,audio.getNumSamples(),last,target); last=target;
    }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*,int) override {}
    std::atomic<float> level{1.f};
private:
    float last=1.f;
};

struct Device {
    juce::PluginDescription type;
    juce::MemoryBlock state;
    Node node;
};
struct Layer {
    std::vector<Device> chain;
    Node gain;
    LayerGain& level() const {return *static_cast<LayerGain*>(gain->getProcessor());}
};

class Rack final : public ProcessorBase, public juce::ChangeBroadcaster {
public:
    explicit Rack(bool isInstrument) : ProcessorBase(stereo(!isInstrument)),instrument(isInstrument) {
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
        graph.setPlayConfigDetails(instrument?0:2,2,44100,512);
        if(!instrument) input=graph.addNode(std::make_unique<IO>(IO::audioInputNode));
        midi=graph.addNode(std::make_unique<IO>(IO::midiInputNode));
        output=graph.addNode(std::make_unique<IO>(IO::audioOutputNode));
        connectLayers();
    }
    const juce::String getName() const override {return instrument?"OSTF Instrument Rack":"OSTF Effect Rack";}
    bool acceptsMidi() const override {return instrument;}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()
            && (instrument || b.getMainInputChannelSet()==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double rate,int block) override {
        graph.setPlayConfigDetails(instrument?0:2,2,rate,block);
        graph.setNonRealtime(isNonRealtime()); graph.prepareToPlay(rate,block);
        setLatencySamples(graph.getLatencySamples());
    }
    void releaseResources() override {graph.releaseResources();}
    void reset() override {graph.reset();}
    double getTailLengthSeconds() const override {
        const juce::ScopedLock guard(modelLock);
        double tail=0.;
        for(const auto& layer:layers) {
            double sum=0.;
            for(const auto& d:layer.chain) if(d.node) sum+=d.node->getProcessor()->getTailLengthSeconds();
            tail=std::max(tail,sum);
        }
        return tail;
    }
    void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& events) override {
        graph.setPlayHead(getPlayHead()); graph.processBlock(audio,events); events.clear();
    }
    juce::Result append(const juce::File& file,int layerIndex) {
        const juce::ScopedLock guard(modelLock);
        const bool newLayer=layerIndex<0;
        if(!newLayer && size_t(layerIndex)>=layers.size()) return juce::Result::fail("Select a layer.");
        const bool synth=instrument && newLayer;
        juce::OwnedArray<juce::PluginDescription> types;
        formats.getFormat(0)->findAllTypesForFile(types,file.getFullPathName());
        for(auto* type:types) if(type->isInstrument==synth) {
            Device d{*type,{},{}};
            auto result=load(d);
            if(result.failed()) return result;
            if(newLayer) layers.push_back(makeLayer());
            layers[newLayer?layers.size()-1:size_t(layerIndex)].chain.push_back(std::move(d));
            changed(); return juce::Result::ok();
        }
        return juce::Result::fail(synth?"Select a VST3 instrument.":"Select a VST3 audio effect.");
    }
    void remove(int l,int index) {
        const juce::ScopedLock guard(modelLock);
        if(l<0 || size_t(l)>=layers.size()) return;
        auto& layer=layers[size_t(l)];
        if(index<0 || size_t(index)>=layer.chain.size()) return;
        if(index==0 && instrument) {
            for(auto& d:layer.chain) if(d.node) graph.removeNode(d.node->nodeID,Update::none);
            layer.chain.clear();
        } else {
            if(auto n=layer.chain[size_t(index)].node) graph.removeNode(n->nodeID,Update::none);
            layer.chain.erase(layer.chain.begin()+index);
        }
        if(layer.chain.empty()) {graph.removeNode(layer.gain->nodeID,Update::none);layers.erase(layers.begin()+l);}
        changed();
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        const juce::ScopedLock guard(modelLock);
        juce::XmlElement xml("ParallelRack");
        xml.setAttribute("instrument",instrument);
        for(auto& l:layers) {
            auto* layer=xml.createNewChildElement("Layer");
            layer->setAttribute("level",double(l.level().level.load()));
            for(auto& d:l.chain) {
                auto* item=layer->createNewChildElement("Device");
                item->addChildElement(d.type.createXml().release());
                if(d.node) d.node->getProcessor()->getStateInformation(d.state);
                item->setAttribute("state",d.state.toBase64Encoding());
            }
        }
        copyXmlToBinary(xml,dest);
    }
    void setStateInformation(const void* data,int bytes) override {
        const juce::ScopedLock guard(modelLock);
        auto xml=getXmlFromBinary(data,bytes);
        if(!xml || !xml->hasTagName("ParallelRack") || xml->getBoolAttribute("instrument")!=instrument) return;
        std::vector<Layer> next;
        for(auto* entry:xml->getChildWithTagNameIterator("Layer")) {
            Layer layer=makeLayer();
            layer.level().level.store(float(juce::jlimit(0.,2.,entry->getDoubleAttribute("level",1.))));
            for(auto* item:entry->getChildWithTagNameIterator("Device")) {
                Device d;
                if(auto* type=item->getFirstChildElement(); type && d.type.loadFromXml(*type)) {
                    d.state.fromBase64Encoding(item->getStringAttribute("state"));
                    if(d.type.isInstrument==(instrument && layer.chain.empty())) load(d);
                    layer.chain.push_back(std::move(d));
                }
            }
            if(layer.chain.empty()) graph.removeNode(layer.gain->nodeID,Update::none);
            else next.push_back(std::move(layer));
        }
        for(auto& layer:layers) {
            for(auto& d:layer.chain) if(d.node) graph.removeNode(d.node->nodeID,Update::none);
            graph.removeNode(layer.gain->nodeID,Update::none);
        }
        layers.swap(next); connectLayers(); sendChangeMessage();
    }
    const bool instrument;
    juce::CriticalSection modelLock;
    std::vector<Layer> layers;
private:
    juce::AudioPluginFormatManager formats;
    Graph graph;
    Node input,midi,output;
    Layer makeLayer() {return {{},graph.addNode(std::make_unique<LayerGain>(),{},Update::none)};}
    void locateDevice(juce::PluginDescription& type) {
        const auto path = type.fileOrIdentifier.replaceCharacter('\\', '/');
        if (juce::File::isAbsolutePath(path) && juce::File(path).exists()) return;
        const auto name = path.fromLastOccurrenceOf("/", false, false);
        if (!name.endsWithIgnoreCase(".vst3")) return;
        const auto locations = formats.getFormat(0)->getDefaultLocationsToSearch();
        juce::Array<juce::File> folders{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory()};
        for (int i = 0; i < locations.getNumPaths(); ++i) {
            folders.add(locations[i]);
            folders.add(locations[i].getChildFile("OSTF Studio"));
        }
        for (const auto& folder : folders) {
            const auto candidate = folder.getChildFile(name);
            if (!candidate.exists()) continue;
            juce::OwnedArray<juce::PluginDescription> matches;
            formats.getFormat(0)->findAllTypesForFile(matches, candidate.getFullPathName());
            for (const auto* match : matches)
                if (match->uniqueId == type.uniqueId && match->manufacturerName == type.manufacturerName
                    && match->isInstrument == type.isInstrument) { type = *match; return; }
        }
    }
    juce::Result load(Device& device) {
        locateDevice(device.type);
        const auto& type=device.type;
        if(type.pluginFormatName!="VST3" || type.name.containsIgnoreCase("OSTF Instrument Rack")
            || type.name.containsIgnoreCase("OSTF Effect Rack") || type.fileOrIdentifier.containsIgnoreCase("OSTF Instrument Rack.vst3")
            || type.fileOrIdentifier.containsIgnoreCase("OSTF Effect Rack.vst3")) return juce::Result::fail("A rack cannot host another OSTF rack.");
        juce::String error;
        auto processor=formats.createPluginInstance(type,getSampleRate()>0?getSampleRate():44100.,
                                                    getBlockSize()>0?getBlockSize():512,error);
        if(!processor) return juce::Result::fail(error);
        processor->disableNonMainBuses();
        const int outputs=processor->getMainBusNumOutputChannels(), inputs=processor->getMainBusNumInputChannels();
        if(outputs<1 || outputs>2 || (!type.isInstrument && (inputs<1 || inputs>2)))
            return juce::Result::fail("Select a mono or stereo VST3.");
        if(device.state.getSize()) processor->setStateInformation(device.state.getData(),int(device.state.getSize()));
        device.node=graph.addNode(std::move(processor),{},Update::none);
        return device.node ? juce::Result::ok() : juce::Result::fail("Cannot create rack node.");
    }
    void stereoLink(Node from,Node to) {
        const int out=from->getProcessor()->getTotalNumOutputChannels();
        const int in=to->getProcessor()->getTotalNumInputChannels();
        for(int ch=0;ch<std::min(2,in);++ch)
            graph.addConnection({{from->nodeID,std::min(ch,out-1)},{to->nodeID,ch}},Update::none);
    }
    void connectLayers() {
        for(const auto& conn:graph.getConnections()) graph.removeConnection(conn,Update::none);
        if(layers.empty() && input) stereoLink(input,output);
        for(auto& l:layers) {
            Node previous=input;
            if(std::any_of(l.chain.begin(),l.chain.end(),[](const auto& d){return !d.node;})) continue;
            for(auto& d:l.chain) {
                if(previous) stereoLink(previous,d.node);
                if(d.node->getProcessor()->acceptsMidi() && instrument)
                    graph.addConnection({{midi->nodeID,Graph::midiChannelIndex},{d.node->nodeID,Graph::midiChannelIndex}},Update::none);
                previous=d.node;
            }
            if(previous) {stereoLink(previous,l.gain);stereoLink(l.gain,output);}
        }
        graph.rebuild(); setLatencySamples(graph.getLatencySamples());
    }
    void changed(){connectLayers();sendChangeMessage();updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));}
};

class RackEditor final : public ostfstudio::StudioPluginEditor, private juce::ChangeListener {
public:
    explicit RackEditor(Rack& p): StudioPluginEditor(p),rack(p) {
        for(auto* c:std::initializer_list<juce::Component*>{&title,&layers,&devices,&add,&insert,&open,&remove,&level,&levelLabel,&status})
            addAndMakeVisible(c);
        title.setText(p.getName(),juce::dontSendNotification);
        title.setFont(juce::FontOptions(20.f,juce::Font::bold));
        layers.setTextWhenNothingSelected("Layers"); devices.setTextWhenNothingSelected("Plug-ins in signal order");
        layers.setTitle("Layer"); devices.setTitle("Plug-in");
        levelLabel.setText("Layer level",juce::dontSendNotification);
        level.setRange(-60.,6.,.1); level.setTextValueSuffix(" dB");
        level.setSliderStyle(juce::Slider::LinearHorizontal);level.setTextBoxStyle(juce::Slider::TextBoxRight,false,74,23);
        add.setButtonText(p.instrument?"+ Instrument layer":"+ Effect layer");
        add.onClick=[this]{choose(-1);};
        insert.onClick=[this]{choose(layers.getSelectedItemIndex());};
        layers.onChange=[this]{refreshDevices();};
        devices.onChange=[this]{buttons();};
        level.onValueChange=[this]{
            const juce::ScopedLock guard(rack.modelLock);
            const int i=layers.getSelectedItemIndex();
            if(i>=0 && size_t(i)<rack.layers.size()) {
                rack.layers[size_t(i)].level().level.store(juce::Decibels::decibelsToGain(float(level.getValue()),-60.f));
                rack.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
            }
        };
        remove.onClick=[this]{window.reset();rack.remove(layers.getSelectedItemIndex(),devices.getSelectedItemIndex());refresh();};
        open.onClick=[this]{
            const juce::ScopedLock guard(rack.modelLock);
            auto* d=selected();if(!d || !d->node) return;
            window.reset();window=std::make_unique<ChildWindow>(d->node);
            window->setVisible(true);window->toFront(true);
        };
        rack.addChangeListener(this); refresh();setSize(540,196);
    }
    ~RackEditor() override {rack.removeChangeListener(this);}
    void resized() override {
        auto r=getLocalBounds().reduced(10);
        auto h=r.removeFromTop(34);add.setBounds(h.removeFromRight(150).reduced(2));title.setBounds(h);
        auto a=r.removeFromTop(34);layers.setBounds(a.removeFromLeft(200).reduced(2));levelLabel.setBounds(a.removeFromLeft(74));level.setBounds(a);
        devices.setBounds(r.removeFromTop(33).reduced(2));auto b=r.removeFromTop(32);
        insert.setBounds(b.removeFromLeft(135).reduced(2));open.setBounds(b.removeFromLeft(130).reduced(2));remove.setBounds(b.removeFromLeft(135).reduced(2));
        status.setBounds(r);
    }
private:
    struct ChildWindow final:juce::DocumentWindow {
        Node keepAlive;
        explicit ChildWindow(Node n):DocumentWindow(n->getProcessor()->getName(),juce::Colour(0xff202b31),closeButton),keepAlive(n) {
            auto* child=n->getProcessor();
            auto* editor=child->createEditorIfNeeded();
            if(!editor) editor=new juce::GenericAudioProcessorEditor(*child);
            setUsingNativeTitleBar(true);setContentOwned(editor,true);centreWithSize(getWidth(),getHeight());
        }
        ~ChildWindow() override {clearContentComponent();}
        void closeButtonPressed() override {setVisible(false);}
    };
    void choose(int layer) {
        const juce::ScopedLock guard(rack.modelLock);
        const auto target = layer>=0 && size_t(layer)<rack.layers.size()?rack.layers[size_t(layer)].gain:Node{};
        chooser=std::make_unique<juce::FileChooser>("Choose a VST3 plug-in",juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory(),"*.vst3",false);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories,
            [self=juce::Component::SafePointer<RackEditor>(this),target](const juce::FileChooser& fc) {
                if(!self || fc.getResult()==juce::File{}) return;
                const juce::ScopedLock guard(self->rack.modelLock);
                int index=-1;
                if(target) {
                    const auto& rows=self->rack.layers;
                    auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& l){return l.gain==target;});
                    if(found==rows.end()) return;
                    index=int(found-rows.begin());
                }
                auto result=self->rack.append(fc.getResult(),index);
                self->status.setText(result.getErrorMessage(),juce::dontSendNotification);
                if(result.wasOk()){
                    self->refresh();self->layers.setSelectedId(index<0?int(self->rack.layers.size()):index+1,juce::sendNotificationSync);
                    self->devices.setSelectedId(self->devices.getNumItems(),juce::sendNotificationSync);
                }
            });
    }
    Device* selected() {
        int l=layers.getSelectedItemIndex(), i=devices.getSelectedItemIndex();
        if(l<0 || size_t(l)>=rack.layers.size()) return nullptr;
        auto& chain=rack.layers[size_t(l)].chain;
        return i>=0 && size_t(i)<chain.size()?&chain[size_t(i)]:nullptr;
    }
    void buttons(){const juce::ScopedLock guard(rack.modelLock);auto* d=selected();open.setEnabled(d && d->node);remove.setEnabled(d!=nullptr);}
    void refreshDevices() {
        const juce::ScopedLock guard(rack.modelLock);
        const int picked=devices.getSelectedId();devices.clear(juce::dontSendNotification);
        int l=layers.getSelectedItemIndex();
        const bool active=l>=0 && size_t(l)<rack.layers.size();
        if(active) {
            auto& layer=rack.layers[size_t(l)];
            for(auto& d:layer.chain) devices.addItem(juce::String(devices.getNumItems()+1)+". "+d.type.name+(d.node?"":" (missing)"),devices.getNumItems()+1);
            devices.setSelectedId(juce::jlimit(1,devices.getNumItems(),picked),juce::dontSendNotification);
            level.setValue(juce::Decibels::gainToDecibels(layer.level().level.load(),-60.f),juce::dontSendNotification);
        }
        insert.setEnabled(active);level.setEnabled(active);buttons();
    }
    void refresh(){
        const juce::ScopedLock guard(rack.modelLock);
        int selectedLayer=layers.getSelectedId();layers.clear(juce::dontSendNotification);
        for(auto& layer:rack.layers) layers.addItem(juce::String(layers.getNumItems()+1)+". "+layer.chain.front().type.name,layers.getNumItems()+1);
        if(layers.getNumItems()) layers.setSelectedId(juce::jlimit(1,layers.getNumItems(),selectedLayer),juce::dontSendNotification);
        refreshDevices();
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override {refresh();}
    Rack& rack;
    juce::ComboBox layers,devices;
    juce::Slider level;
    juce::Label title,levelLabel,status;
    juce::TextButton add,insert{"+ Insert effect"},open{"Open plug-in"},remove{"Remove plug-in"};
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ChildWindow> window;
};
juce::AudioProcessorEditor* Rack::createEditor(){return new RackEditor(*this);}
}
juce::AudioProcessor* createOstfRack(bool instrument){return new Rack(instrument);}
