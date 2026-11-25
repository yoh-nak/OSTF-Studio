#include "ScaleDocument.h"
#include "ScoreExchange.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>

namespace {
using namespace ostfstudio::scale;
class Processor final : public juce::AudioProcessor, public juce::VST3ClientExtensions,
                        public juce::ChangeBroadcaster {
public:
    Processor() : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                                  .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}
    const juce::String getName() const override { return "OSTF Scale Editor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void prepareToPlay(double,int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        auto in = l.getMainInputChannelSet();
        return in == l.getMainOutputChannelSet() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    struct Input final : ScoreInput {
        explicit Input(Processor& p) : owner(p) {}
        Processor& owner;
        std::atomic<Steinberg::uint32> references{1};
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID id, void** out) override {
            if (!out) return Steinberg::kInvalidArgument;
            if (std::memcmp(id,scoreInputId,16) && std::memcmp(id,Steinberg::FUnknown_iid,16)) {
                *out = nullptr; return Steinberg::kNoInterface;
            }
            *out = static_cast<ScoreInput*>(this); addRef(); return Steinberg::kResultOk;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return ++references; }
        Steinberg::uint32 PLUGIN_API release() override {
            const auto n = --references;
            if (!n) delete this;
            return n;
        }
        Steinberg::tresult PLUGIN_API offerScore(const char* bytes, Steinberg::int32 size) override {
            if (!bytes || size <= 0 || size > maxBytes) return Steinberg::kInvalidArgument;
            const auto source = juce::String::fromUTF8(bytes,size);
            const juce::ScopedLock lock(owner.stateLock);
            if (source == owner.offered) return Steinberg::kResultOk;
            owner.offered = source;
            // Reopening an editor must preserve unreturned edits.
            if (owner.document.text.isEmpty() || owner.document.baseHash == digest(owner.document.text)) {
                const auto r = owner.document.load(source);
                if (r.failed()) return Steinberg::kResultFalse;
            }
            owner.sendChangeMessage();
            return Steinberg::kResultOk;
        }
    };
    int32_t queryIAudioProcessor(const Steinberg::TUID id,void** out) override {
        if (std::memcmp(id,scoreInputId,16)) return juce::VST3ClientExtensions::queryIAudioProcessor(id,out);
        *out = static_cast<ScoreInput*>(new Input(*this));
        return Steinberg::kResultOk;
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        const juce::ScopedLock lock(stateLock);
        juce::XmlElement xml("ScaleEditor");
        xml.setAttribute("baseHash",document.baseHash);
        xml.addTextElement(document.text);
        copyXmlToBinary(xml,dest);
    }
    void setStateInformation(const void* data,int size) override {
        if (const auto xml = getXmlFromBinary(data,size); xml && xml->hasTagName("ScaleEditor")) {
            const juce::ScopedLock lock(stateLock);
            if (document.load(xml->getAllSubText()).wasOk()) {
                const auto hash = xml->getStringAttribute("baseHash");
                if (hash.length()==64 && hash.containsOnly("0123456789abcdef")) document.baseHash = hash;
                sendChangeMessage();
            }
        }
    }
    juce::CriticalSection stateLock;
    Document document;
    juce::String offered;
};

class Editor final : public juce::AudioProcessorEditor, private juce::ChangeListener {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), proc(p) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&song,&chords,&roots,&modes,&apply,&open,&refresh,&save,&drag,&status})
            addAndMakeVisible(c);
        chords.setTitle("Chord track"); roots.setTitle("Scale tonic"); modes.setTitle("Scale type");
        roots.setTextWhenNothingSelected("Tonic"); modes.setTextWhenNothingSelected("Scale");
        for (int i=0;i<12;++i) roots.addItem(tonicNames[i],i+1);
        for (const auto& s : catalog()) modes.addItem(juce::String::fromUTF8(s.name),modes.getNumItems()+1);
        chords.onChange = [this]{ selectChord(); };
        apply.onClick = [this]{
            const juce::ScopedLock lock(proc.stateLock);
            const auto r = proc.document.assign(chords.getSelectedItemIndex(),roots.getSelectedItemIndex(),modes.getSelectedItemIndex());
            if (r.wasOk()) { proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true)); selectChord(); }
            else status.setText(r.getErrorMessage(),juce::dontSendNotification);
        };
        open.onClick = [this]{ choose(false); }; save.onClick = [this]{ choose(true); };
        refresh.onClick = [this]{ requestLoad(proc.offered); };
        proc.addChangeListener(this);
        sync();
        setSize(620,218);
    }
    ~Editor() override { proc.removeChangeListener(this); }
    void resized() override {
        auto a=getLocalBounds().reduced(12);
        song.setBounds(a.removeFromTop(32));
        auto files=a.removeFromTop(31);
        open.setBounds(files.removeFromLeft(105).reduced(1)); refresh.setBounds(files.removeFromLeft(150).reduced(1));
        save.setBounds(files.removeFromLeft(110).reduced(1)); drag.setBounds(files.reduced(1));
        a.removeFromTop(8); chords.setBounds(a.removeFromTop(31)); a.removeFromTop(6);
        auto edit=a.removeFromTop(32);
        roots.setBounds(edit.removeFromLeft(75).reduced(1)); apply.setBounds(edit.removeFromRight(105).reduced(1)); modes.setBounds(edit.reduced(1));
        status.setBounds(a);
    }
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff202e36));}
private:
    struct Drag final : juce::TextButton {
        Drag():TextButton("Drag OSTF to Studio"){}
        std::function<juce::File()> exportFile;
        void mouseDown(const juce::MouseEvent& e) override { started=false; juce::TextButton::mouseDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (started || !e.mouseWasDraggedSinceMouseDown()) return;
            started=true;
            auto f=exportFile();
            if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()},false,this);
        }
        bool started=false;
    };
    void changeListenerCallback(juce::ChangeBroadcaster*) override { sync(); }
    void selectChord() {
        const juce::ScopedLock lock(proc.stateLock);
        auto chord=proc.document.score["harmony"]["chords"][chords.getSelectedItemIndex()];
        if (!chord.isObject()) return;
        auto tonic=chord["root"];
        int mode=1; juce::String label;
        const auto scales=proc.document.score["harmony"]["scales"];
        if (auto* list=scales.getArray()) for (const auto& s:*list) {
            auto targets=s["targets"];
            if (targets.size()!=1 || targets[0]["type"].toString()!="chord" || targets[0]["id"]!=chord["id"]
                || s["start"]!=chord["start"] || s["duration"]!=chord["duration"]) continue;
            tonic=s["tonic"]; label=s["label"].toString();
            mode=0;
            for (const auto& candidate:catalog()) {
                juce::Array<juce::var> values;
                for(int n:candidate.intervals) values.add(n);
                if(juce::JSON::toString(juce::var(values),true)==juce::JSON::toString(s["intervals"],true)) {
                    mode=int(&candidate-catalog().data())+1; break;
                }
            }
            break;
        }
        const auto step=juce::String("C D EF G A B").indexOf(tonic["step"].toString());
        roots.setSelectedId(((std::max(0,step)+int(tonic["alter"])+12)%12)+1,juce::dontSendNotification);
        modes.setSelectedId(mode,juce::dontSendNotification);
        status.setText(label.isEmpty() ? "Choose a scale for this chord." : "Assigned: "+label,juce::dontSendNotification);
    }
    void sync() {
        const juce::ScopedLock lock(proc.stateLock);
        auto score=proc.document.score;
        song.setText("OSTF Scale Editor   /   "+score["song"]["title"].toString(),juce::dontSendNotification);
        const int selected=chords.getSelectedItemIndex();
        chords.clear(juce::dontSendNotification);
        const auto events=score["harmony"]["chords"];
        if (const auto* list=events.getArray()) for(const auto& c:*list)
            chords.addItem(juce::String(chords.getNumItems()+1)+".    "+c["label"].toString()+"    /    tick "+c["start"].toString(),chords.getNumItems()+1);
        chords.setSelectedId(juce::jlimit(0,chords.getNumItems(),std::max(1,selected+1)),juce::dontSendNotification);
        const bool ready=chords.getNumItems()>0;
        for(juce::Component* c:std::initializer_list<juce::Component*>{&apply,&roots,&modes,&chords}) c->setEnabled(ready);
        refresh.setEnabled(proc.offered.isNotEmpty()); save.setEnabled(!proc.document.text.isEmpty()); drag.setEnabled(save.isEnabled());
        if(ready) selectChord(); else status.setText("Open an OSTF score with a chord track.",juce::dontSendNotification);
        drag.exportFile=[this] {
            const juce::ScopedLock guard(proc.stateLock);
            const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("OSTF Scale Editor");
            const auto file=dir.getChildFile("studio-scales-"+proc.document.baseHash+"-"+juce::Uuid().toString()+".ostf");
            if (dir.createDirectory().failed() || !file.replaceWithText(proc.document.text,false,false,nullptr)) {
                status.setText("Unable to create the drag file.",juce::dontSendNotification); return juce::File{};
            }
            return file;
        };
    }
    void requestLoad(juce::String source) {
        const juce::ScopedLock lock(proc.stateLock);
        if (proc.document.text.isEmpty() || proc.document.baseHash == digest(proc.document.text)) {
            load(source);
            return;
        }
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            "Read another score?", "Replace the scale assignments currently open in the editor?", "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create([self=juce::Component::SafePointer<Editor>(this),source](int answer) {
                if (self && answer) self->load(source);
            }));
    }
    void load(const juce::String& source) {
        const juce::ScopedLock lock(proc.stateLock);
        const auto r=proc.document.load(source);
        if(r.failed()) {status.setText(r.getErrorMessage(),juce::dontSendNotification); return;}
        sync(); proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }
    void choose(bool writing) {
        chooser=std::make_unique<juce::FileChooser>(writing?"Save OSTF":"Open OSTF",juce::File{},"*.ostf");
        using F=juce::FileBrowserComponent;
        chooser->launchAsync((writing?F::saveMode|F::warnAboutOverwriting:F::openMode)|F::canSelectFiles,
            [self=juce::Component::SafePointer<Editor>(this),writing](const auto& fc) {
                if (!self) return;
                auto file=fc.getResult(); if(file==juce::File{}) return;
                if (!writing && file.getSize()<=maxBytes) {self->requestLoad(file.loadFileAsString());return;}
                const juce::ScopedLock lock(self->proc.stateLock);
                if (writing && !file.hasFileExtension("ostf")) {
                    self->status.setText("Choose a filename ending in .ostf.",juce::dontSendNotification); return;
                }
                const bool ok=writing && file.replaceWithText(self->proc.document.text,false,false,nullptr);
                self->status.setText(ok?"Saved OSTF.":"Unable to read or write this score.",juce::dontSendNotification);
            });
    }
    using ChangeDetails = juce::AudioProcessorListener::ChangeDetails;
    Processor& proc;
    juce::Label song,status;
    juce::ComboBox chords,roots,modes;
    juce::TextButton open{"Open OSTF"}, refresh{"Read Studio score"}, save{"Save OSTF..."},apply{"Assign scale"};
    Drag drag;
    std::unique_ptr<juce::FileChooser> chooser;
};
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor;}
