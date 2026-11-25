#include "ScaleDocument.h"
#include "ScoreExchange.h"
#include "InstrumentRack.h"
#include "ScaleAssets.h"
#include <juce_javascript/juce_javascript.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>

namespace {
using namespace ostfstudio::scale;
class Processor final : public juce::AudioProcessor, public juce::VST3ClientExtensions,
                        public juce::ChangeBroadcaster {
public:
    Processor() : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)), rack(*this) {}
    const juce::String getName() const override { return "OSTF Scale Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void prepareToPlay(double rate,int block) override {rack.prepare(rate,block);}
    void releaseResources() override {rack.release();}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi) override {rack.process(audio,midi);}
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
        xml.createNewChildElement("Score")->addTextElement(document.text);
        if (unavailableInstrument) xml.addChildElement(new juce::XmlElement(*unavailableInstrument));
        else rack.save(xml);
        copyXmlToBinary(xml,dest);
    }
    void setStateInformation(const void* data,int size) override {
        if (const auto xml = getXmlFromBinary(data,size); xml && xml->hasTagName("ScaleEditor")) {
            const juce::ScopedLock lock(stateLock);
            const auto* score=xml->getChildByName("Score");
            const auto restored=rack.restore(xml->getChildByName("Instrument"));
            rackError=restored.getErrorMessage();
            unavailableInstrument.reset();
            if (restored.failed()) {
                unavailableInstrument=std::make_unique<juce::XmlElement>(*xml->getChildByName("Instrument"));
                rack.restore(nullptr);
            }
            sendChangeMessage();
            if (document.load(score ? score->getAllSubText() : xml->getAllSubText()).wasOk()) {
                const auto hash = xml->getStringAttribute("baseHash");
                if (hash.length()==64 && hash.containsOnly("0123456789abcdef")) document.baseHash = hash;
                sendChangeMessage();
            }
        }
    }
    juce::CriticalSection stateLock;
    Document document;
    juce::String offered,rackError;
    InstrumentRack rack;
    std::unique_ptr<juce::XmlElement> unavailableInstrument;
};

class ChordMap final : public juce::Component {
public:
    static constexpr int cell = 82;
    juce::StringArray chords, scales;
    int anchor = 0, end = 0;
    std::vector<int> targets;
    std::function<void(int,int)> selected;
    ChordMap() { setViewportIgnoreDragFlag(true); setTitle("Song scale map"); }
    void paint(juce::Graphics& g) override {
        const auto clip = g.getClipBounds();
        for (int i = std::max(0,clip.getX()/cell); i < chords.size() && i <= clip.getRight()/cell; ++i) {
            juce::Rectangle<int> tile(i*cell,0,cell-2,getHeight()-1);
            const bool active = i>=std::min(anchor,end) && i<=std::max(anchor,end);
            g.setColour(juce::Colour(active ? 0xff27675e : std::binary_search(targets.begin(),targets.end(),i) ? 0xff404f70 : 0xff26343e));
            g.fillRoundedRectangle(tile.toFloat(),3);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(13.f,juce::Font::bold));
            g.drawText(juce::String(i+1)+"  "+chords[i],tile.removeFromTop(22).reduced(4,0),juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xffb5d4cc)); g.setFont(juce::FontOptions(11.f));
            g.drawText(scales[i],tile.reduced(4,0),juce::Justification::centredLeft);
        }
    }
    int hit(const juce::MouseEvent& e) const {return std::clamp(e.x/cell,0,std::max(0,chords.size()-1));}
    void mouseDown(const juce::MouseEvent& e) override { if (!e.mods.isShiftDown()) anchor=hit(e); end=hit(e); repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (auto* view=findParentComponentOfClass<juce::Viewport>()) view->autoScroll(e.getEventRelativeTo(view).x,0,24,12);
        end=hit(e); repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if(selected && !chords.isEmpty()) selected(std::min(anchor,end),std::max(anchor,end));
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::ChangeListener {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), proc(p) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&song,&chords,&roots,&modes,&apply,&open,&refresh,&save,&drag,&status,&back,&forward,&strategy,&lens,&choices,&details,&notes,&audition,&compare,&stop,&loadInstrument,&showInstrument})
            addAndMakeVisible(c);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&mapView,&repeats,&rangeLabel,&undo})
            addAndMakeVisible(c);
        mapView.setViewedComponent(&map,false); mapView.setScrollBarsShown(false,true);
        repeats.onClick=[this]{updateScope();};
        map.selected=[this](int first,int last) {
            chords.setSelectedItemIndex(first,juce::dontSendNotification);
            selectChord(); map.anchor=first; map.end=last; updateScope();
        };
        undo.setEnabled(false);
        undo.onClick=[this] {
            const juce::ScopedLock lock(proc.stateLock);
            if (!previousDocument || undoAtText != proc.document.text) { sync(); return; }
            proc.document=std::move(*previousDocument); previousDocument.reset(); undo.setEnabled(false);
            sync(); proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
        };
        js.maximumExecutionTime=juce::RelativeTime::seconds(6);
        engineStatus=js.execute(juce::String::fromUTF8(ScaleAssets::Adviser_js,ScaleAssets::Adviser_jsSize));
        if(engineStatus.wasOk()) engineStatus=js.execute(juce::String::fromUTF8(ScaleAssets::AdviserBridge_js,ScaleAssets::AdviserBridge_jsSize));
        back.onClick=[this]{chords.setSelectedItemIndex(chords.getSelectedItemIndex()-1,juce::sendNotificationSync);};
        forward.onClick=[this]{chords.setSelectedItemIndex(chords.getSelectedItemIndex()+1,juce::sendNotificationSync);};
        strategy.addItemList({"Balanced","Smooth voice leading","Tension / resolution"},1);
        lens.addItemList({"Chord scale","Key and mode","Performance"},1);
        strategy.setSelectedId(1,juce::dontSendNotification); lens.setSelectedId(1,juce::dontSendNotification);
        strategy.onChange=lens.onChange=[this]{selectChord(false);};
        choices.setTitle("Ranked scale candidates");
        choices.onChange=[this]{selectCandidate();};
        loadInstrument.onClick=[this]{chooseInstrument();};
        showInstrument.onClick=[this]{proc.rack.openWindow();};
        audition.onClick=[this]{listen(false);}; compare.onClick=[this]{listen(true);};
        stop.onClick=[this]{proc.rack.stopPreview();};
        notes.setColour(juce::Label::textColourId,juce::Colour(0xff95ead6));
        notes.setFont(juce::FontOptions(18,juce::Font::bold));
        details.setJustificationType(juce::Justification::centredLeft);
        chords.setTitle("Chord track"); roots.setTitle("Scale tonic"); modes.setTitle("Scale type");
        roots.setTextWhenNothingSelected("Tonic"); modes.setTextWhenNothingSelected("Scale");
        for (int i=0;i<12;++i) roots.addItem(tonicNames[i],i+1);
        for (const auto& s : catalog()) modes.addItem(juce::String::fromUTF8(s.name),modes.getNumItems()+1);
        chords.onChange = [this]{ selectChord(); };
        apply.onClick = [this]{
            const juce::ScopedLock lock(proc.stateLock);
            const auto before = proc.document;
            const auto r = proc.document.assign(map.targets,roots.getSelectedItemIndex(),modes.getSelectedItemIndex());
            if (r.wasOk()) {
                previousDocument = before; undoAtText = proc.document.text; undo.setEnabled(true);
                const auto first=map.anchor, last=map.end;
                proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true)); sync();
                map.anchor=first; map.end=last; updateScope();
            }
            else status.setText(r.getErrorMessage(),juce::dontSendNotification);
        };
        open.onClick = [this]{ choose(false); }; save.onClick = [this]{ choose(true); };
        refresh.onClick = [this]{ requestLoad(proc.offered); };
        proc.addChangeListener(this);
        sync();
        setResizable(true,false); setResizeLimits(700,565,1600,1000); setSize(900,625);
    }
    ~Editor() override { proc.rack.stopPreview(); proc.rack.closeWindow(); proc.removeChangeListener(this); }
    void resized() override {
        auto a=getLocalBounds().reduced(12);
        song.setBounds(a.removeFromTop(32));
        auto files=a.removeFromTop(31);
        open.setBounds(files.removeFromLeft(105).reduced(1)); refresh.setBounds(files.removeFromLeft(150).reduced(1));
        save.setBounds(files.removeFromLeft(110).reduced(1)); drag.setBounds(files.reduced(1));
        a.removeFromTop(10);
        auto scopeRow=a.removeFromTop(28);
        undo.setBounds(scopeRow.removeFromRight(90)); repeats.setBounds(scopeRow.removeFromRight(210));
        rangeLabel.setBounds(scopeRow);
        mapView.setBounds(a.removeFromTop(61)); map.setSize(std::max(1,map.chords.size()*ChordMap::cell),45);
        auto nav=a.removeFromTop(32);
        back.setBounds(nav.removeFromLeft(38).reduced(1)); forward.setBounds(nav.removeFromRight(38).reduced(1)); chords.setBounds(nav.reduced(1));
        auto policy=a.removeFromTop(32);
        strategy.setBounds(policy.removeFromLeft(policy.getWidth()/2).reduced(1)); lens.setBounds(policy.reduced(1));
        choices.setBounds(a.removeFromTop(33).reduced(1));
        notes.setBounds(a.removeFromTop(29)); details.setBounds(a.removeFromTop(std::max(48,(a.getHeight()-175)/2)));
        auto play=a.removeFromTop(32);
        audition.setBounds(play.removeFromLeft(155).reduced(1)); compare.setBounds(play.removeFromLeft(180).reduced(1)); stop.setBounds(play.removeFromLeft(64).reduced(1));
        a.removeFromTop(6);
        auto edit=a.removeFromTop(32);
        roots.setBounds(edit.removeFromLeft(75).reduced(1)); apply.setBounds(edit.removeFromRight(105).reduced(1)); modes.setBounds(edit.reduced(1));
        auto instrument=a.removeFromBottom(34);
        loadInstrument.setBounds(instrument.removeFromLeft(180).reduced(1)); showInstrument.setBounds(instrument.reduced(1));
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
    void updateScope() {
        const juce::ScopedLock lock(proc.stateLock);
        map.targets=proc.document.scope(std::min(map.anchor,map.end),std::max(map.anchor,map.end),repeats.getToggleState());
        rangeLabel.setText("Song scale map   /   "+juce::String(int(map.targets.size()))+" chords   ·   Drag to select",juce::dontSendNotification);
        map.repaint();
    }
    void selectChord(bool resetRange = true) {
        const juce::ScopedLock lock(proc.stateLock);
        auto chord=proc.document.score["harmony"]["chords"][chords.getSelectedItemIndex()];
        if (!chord.isObject()) return;
        if (resetRange) map.anchor=map.end=chords.getSelectedItemIndex();
        updateScope();
        const int left=map.anchor*ChordMap::cell;
        if(left<mapView.getViewPositionX() || left+ChordMap::cell>mapView.getViewArea().getRight()) mapView.setViewPosition(left,0);
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
        back.setEnabled(chords.getSelectedItemIndex()>0);
        forward.setEnabled(chords.getSelectedItemIndex()+1<chords.getNumItems());
        if(loadedText!=proc.document.text && engineStatus.wasOk()) {
            call("openScore",{proc.document.text}); loadedText=proc.document.text;
        }
        const juce::StringArray strategies{"balanced","smooth","tension"}, lenses{"chord","key","performance"};
        rows=juce::JSON::parse(call("rankChord",{chords.getSelectedItemIndex(),strategies[strategy.getSelectedItemIndex()],lenses[lens.getSelectedItemIndex()]}).toString());
        choices.clear(juce::dontSendNotification);
        for(int i=0;i<rows.size();++i)
            choices.addItem(juce::String(i+1)+".   "+rows[i]["name"].toString(),i+1);
        int selected=choices.getNumItems()?1:0;
        if (label.isNotEmpty() && mode>0) for (int i=0;i<rows.size();++i) {
            const auto item=rows[i];
            if (int(item["root"])+1!=roots.getSelectedId()) continue;
            const auto intervals=item["intervals"];
            const auto& desired=catalog()[size_t(mode-1)].intervals;
            if(intervals.size()==int(desired.size()) && std::equal(desired.begin(),desired.end(),intervals.getArray()->begin(),[](int a,const juce::var& b){return a==int(b);})) {
                selected=i+1; break;
            }
        }
        choices.setSelectedId(selected,juce::dontSendNotification);
        if(rows.size()) selectCandidate();
        else { notes.setText({},juce::dontSendNotification); details.setText("No ranked candidates for this chord.",juce::dontSendNotification); }
        audition.setEnabled(rows.size()>0); compare.setEnabled(rows.size()>0);
        status.setText(lastError.isNotEmpty()?lastError:label.isEmpty() ? "Select a candidate or set a custom scale below." : "Assigned: "+label,juce::dontSendNotification);
    }
    juce::var call(const juce::Identifier& function,std::initializer_list<juce::var> args) {
        auto result=engineStatus;
        juce::var value;
        if(result.wasOk()) value=js.callFunction(function,{juce::var{},args.begin(),int(args.size())},&result);
        lastError=result.getErrorMessage();
        return value;
    }
    void selectCandidate() {
        const auto item=rows[choices.getSelectedItemIndex()];
        if(!item.isObject()) return;
        roots.setSelectedId(int(item["root"])+1,juce::dontSendNotification);
        for(size_t i=0;i<catalog().size();++i) {
            const auto values=item["intervals"];
            const auto& intervals=catalog()[i].intervals;
            if(values.size()==int(intervals.size()) && std::equal(intervals.begin(),intervals.end(),values.getArray()->begin(),[](int a,const juce::var& b){return a==int(b);})) {
                modes.setSelectedId(int(i+1),juce::dontSendNotification);break;
            }
        }
        notes.setText(item["pitches"].toString(),juce::dontSendNotification);
        const auto caution=item["warnings"].toString();
        details.setText(item["description"].toString()+"\n"+(caution.isNotEmpty()?caution:item["reason"].toString()),juce::dontSendNotification);
    }
    void listen(bool ab) {
        auto events=juce::JSON::parse(call("listenTo",{choices.getSelectedItemIndex(),ab}).toString());
        if(!events.size()) { status.setText(lastError.isNotEmpty()?lastError:"No phrase for this chord.",juce::dontSendNotification); return; }
        proc.rack.audition(events);
        status.setText(ab?"Playing reference, then candidate.":"Playing candidate.",juce::dontSendNotification);
    }
    void chooseInstrument() {
        chooser=std::make_unique<juce::FileChooser>("Load a VST3 instrument",juce::File{},"*.vst3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories,
            [self=juce::Component::SafePointer<Editor>(this)](const auto& fc){
                if(!self || fc.getResult()==juce::File{}) return;
                auto r=self->proc.rack.choose(fc.getResult());
                self->status.setText(r.failed()?r.getErrorMessage():self->proc.rack.name(),juce::dontSendNotification);
                if(r.wasOk()) {
                    { const juce::ScopedLock lock(self->proc.stateLock);
                      self->proc.unavailableInstrument.reset(); self->proc.rackError.clear(); }
                    self->updateInstrument();
                    self->proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
                }
            });
    }
    void updateInstrument() {
        showInstrument.setButtonText(proc.rack.name());
        showInstrument.setEnabled(proc.rack.hasInstrument());
    }
    void sync() {
        const juce::ScopedLock lock(proc.stateLock);
        if (previousDocument && undoAtText != proc.document.text) {
            previousDocument.reset();
            undo.setEnabled(false);
        }
        auto score=proc.document.score;
        song.setText("OSTF Scale Editor   /   "+score["song"]["title"].toString(),juce::dontSendNotification);
        const int selected=chords.getSelectedItemIndex();
        chords.clear(juce::dontSendNotification);
        const auto events=score["harmony"]["chords"];
        if (const auto* list=events.getArray()) for(const auto& c:*list)
            chords.addItem(juce::String(chords.getNumItems()+1)+".    "+c["label"].toString()+"    /    tick "+c["start"].toString(),chords.getNumItems()+1);
        map.chords.clear(); map.scales.clear();
        std::map<juce::String,juce::String> labels;
        if (auto* list=score["harmony"]["scales"].getArray()) for(const auto& s:*list)
            for (int i=0;i<s["targets"].size();++i) if (s["targets"][i]["type"].toString()=="chord")
                labels[s["targets"][i]["id"].toString()]=s["label"].toString();
        if(auto* list=events.getArray()) for(const auto& c:*list) {
            map.chords.add(c["label"].toString()); map.scales.add(labels[c["id"].toString()]);
        }
        map.setSize(std::max(1,map.chords.size()*ChordMap::cell),45);
        chords.setSelectedId(juce::jlimit(0,chords.getNumItems(),std::max(1,selected+1)),juce::dontSendNotification);
        const bool ready=chords.getNumItems()>0;
        for(juce::Component* c:std::initializer_list<juce::Component*>{&apply,&roots,&modes,&chords}) c->setEnabled(ready);
        refresh.setEnabled(proc.offered.isNotEmpty()); save.setEnabled(!proc.document.text.isEmpty()); drag.setEnabled(save.isEnabled());
        back.setEnabled(false);forward.setEnabled(false);audition.setEnabled(ready);compare.setEnabled(ready);
        updateInstrument();
        if(ready) selectChord(); else status.setText("Open an OSTF score with a chord track.",juce::dontSendNotification);
        if(proc.rackError.isNotEmpty()) status.setText(proc.rackError,juce::dontSendNotification);
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
        previousDocument.reset(); undo.setEnabled(false);
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
    juce::JavascriptEngine js;
    juce::Result engineStatus=juce::Result::ok();
    juce::String loadedText,lastError;
    juce::var rows;
    ChordMap map;
    juce::Viewport mapView;
    juce::ToggleButton repeats{"Include matching progressions"};
    juce::TextButton undo{"Undo apply"};
    juce::Label rangeLabel;
    std::optional<Document> previousDocument;
    juce::String undoAtText;
    juce::Label song,status,notes,details;
    juce::TextButton back{"<"},forward{">"},audition{"Hear candidate"},compare{"A/B with reference"},stop{"Stop"},
        loadInstrument{"Load VST3 instrument..."},showInstrument;
    juce::ComboBox choices,strategy,lens;
    juce::ComboBox chords,roots,modes;
    juce::TextButton open{"Open OSTF"}, refresh{"Read Studio score"}, save{"Save OSTF..."},apply{"Assign scale"};
    Drag drag;
    std::unique_ptr<juce::FileChooser> chooser;
};
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor;}
