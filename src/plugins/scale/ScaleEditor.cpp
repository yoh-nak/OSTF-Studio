#include "ScaleDocument.h"
#include "FlowView.h"
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

class ScaleRows final : public juce::ListBox, private juce::ListBoxModel {
public:
    ScaleRows() { setModel(this); setRowHeight(40); setColour(backgroundColourId,juce::Colour(0xff192534)); }
    ~ScaleRows() override { setModel(nullptr); }
    juce::StringArray labels, subtitles;
    std::function<void(int)> choose;
    int getNumRows() override { return labels.size(); }
    void paintListBoxItem(int i,juce::Graphics& g,int w,int h,bool selected) override {
        if (i>=labels.size()) return;
        g.fillAll(juce::Colour(selected ? 0xff294c4b : 0xff192534));
        g.setColour(juce::Colour(selected ? 0xff7fe0c0 : 0xff34485e)); g.fillRect(0,0,3,h-1);
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(14.f,juce::Font::bold));
        g.drawText(labels[i],10,3,w-16,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffaec0d0)); g.setFont(juce::FontOptions(12.f));
        g.drawText(subtitles[i],10,21,w-16,16,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int i) override { if (i>=0 && choose) choose(i); }
};

class Editor final : public juce::AudioProcessorEditor, private juce::ChangeListener {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), proc(p) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&song,&chords,&roots,&modes,&apply,&open,&refresh,&save,&drag,&status,&back,&forward,&strategy,&lens,&genre,&choices,&details,&notes,&audition,&compare,&stop,&loadInstrument,&showInstrument})
            addAndMakeVisible(c);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&mapView,&repeats,&rangeLabel,&undo})
            addAndMakeVisible(c);
        for (auto* c : std::initializer_list<juce::Component*>{&timeline,&candidates,&timelineTitle,&candidateTitle,&inspectorTitle,&sourceToggle,&autoSet,&redo,&pin})
            addAndMakeVisible(c);
        for(auto* heading:{&timelineTitle,&candidateTitle,&inspectorTitle}) {
            heading->setColour(juce::Label::textColourId,juce::Colour(0xff7fe0c0));
            heading->setFont(juce::FontOptions(15.f,juce::Font::bold));
        }
        for (juce::Component* c : std::initializer_list<juce::Component*>{&inspectMode,&fitRows,&compareText,&scopeMode,&nextUnassigned,&resetA}) addAndMakeVisible(c);
        addAndMakeVisible(flowView);
        addAndMakeVisible(flowKey);
        for (auto* tab : {&flowTab,&connectionsTab}) {
            addAndMakeVisible(*tab);
            tab->onClick=[this,tab]{showConnections=tab==&connectionsTab; updateInspector();};
        }
        flowView.setComponentID("scale-flow-view");
        inspectMode.addItemList({"Scale inspector","Compare A / B","Note Fit"},1);
        fitRows.setComponentID("scale-note-fit");
        inspectMode.setTitle("Scale workspace"); inspectMode.setSelectedId(1,juce::dontSendNotification);
        inspectMode.onChange=[this]{ updateInspector(); };
        scopeMode.addItemList({"Chord / drag range","Section","Whole song"},1);
        scopeMode.setSelectedId(1,juce::dontSendNotification); scopeMode.setTitle("Scale assignment scope");
        scopeMode.onChange=[this]{updateScope();};
        nextUnassigned.onClick=[this] {
            const int total=map.chords.size(), selected=chords.getSelectedItemIndex();
            for (int step=1;step<=total;++step) {
                const int i=(selected+step)%total;
                if (map.scales[i].isEmpty()) { chords.setSelectedItemIndex(i,juce::sendNotificationSync); return; }
            }
        };
        resetA.onClick=[this]{call("resetReference",{});updateInspector();};
        source.setMultiLine(true); source.setReadOnly(true); source.setScrollbarsShown(true);
        addChildComponent(source);
        sourceToggle.onClick=[this] {
            const bool show=!source.isVisible();
            source.setVisible(show);
            if (show) { source.setText(proc.document.text,false); source.toFront(false); }
            sourceToggle.setButtonText(show ? "Visual" : "OSTF text"); resized();
        };
        timeline.choose=[this](int i){chords.setSelectedItemIndex(i,juce::sendNotificationSync);};
        candidates.choose=[this](int i){choices.setSelectedItemIndex(i,juce::sendNotificationSync);};
        mapView.setViewedComponent(&map,false); mapView.setScrollBarsShown(false,true);
        repeats.onClick=[this]{updateScope();};
        map.selected=[this](int first,int last) {
            scopeMode.setSelectedId(1,juce::dontSendNotification);
            chords.setSelectedItemIndex(first,juce::dontSendNotification);
            selectChord(); map.anchor=first; map.end=last; updateScope();
        };
        redo.setEnabled(false);
        redo.onClick=[this] {
            const juce::ScopedLock lock(proc.stateLock);
            if (!redoDocument || redoAtText!=proc.document.text) return;
            previousDocument=proc.document; proc.document=std::move(*redoDocument); redoDocument.reset();
            undoAtText=proc.document.text; undo.setEnabled(true); redo.setEnabled(false); sync();
            proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
        };
        autoSet.onClick=[this]{applySongPlan();};
        pin.onClick=[this]{call("pinReference",{choices.getSelectedItemIndex()});inspectMode.setSelectedId(2,juce::dontSendNotification);updateInspector();};
        undo.setEnabled(false);
        undo.onClick=[this] {
            const juce::ScopedLock lock(proc.stateLock);
            if (!previousDocument || undoAtText != proc.document.text) { sync(); return; }
            redoDocument=proc.document; redoAtText=previousDocument->text; redo.setEnabled(true);
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
        genre.setTitle("Genre for scale ranking and audition");
        const auto styles = juce::JSON::parse(call("genreStyles",{}).toString());
        for (int i = 0; i < styles.size(); ++i) {
            genreIds.add(styles[i]["id"].toString());
            genre.addItem(styles[i]["label"].toString(), i + 1);
        }
        genre.setSelectedId(1, juce::dontSendNotification);
        strategy.onChange=lens.onChange=genre.onChange=[this]{selectChord(false);};
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
                redoDocument.reset(); redo.setEnabled(false);
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
        setResizable(true,false); setResizeLimits(795,655,1700,1150); setSize(1120,800);
    }
    ~Editor() override { proc.rack.stopPreview(); proc.rack.closeWindow(); proc.removeChangeListener(this); }
    void resized() override {
        auto area=getLocalBounds().reduced(12);
        auto head=area.removeFromTop(32); sourceToggle.setBounds(head.removeFromRight(100)); song.setBounds(head);
        auto files=area.removeFromTop(32);
        open.setBounds(files.removeFromLeft(100).reduced(1)); refresh.setBounds(files.removeFromLeft(145).reduced(1));
        save.setBounds(files.removeFromLeft(99).reduced(1)); redo.setBounds(files.removeFromRight(59).reduced(1));
        undo.setBounds(files.removeFromRight(89).reduced(1)); autoSet.setBounds(files.removeFromRight(115).reduced(1)); drag.setBounds(files.reduced(1));
        area.removeFromTop(8);
        if (source.isVisible()) source.setBounds(area);
        auto tabs=area.removeFromTop(27);
        flowTab.setBounds(tabs.removeFromLeft(105).reduced(1));
        connectionsTab.setBounds(tabs.removeFromLeft(115).reduced(1));
        flowKey.setBounds(tabs.reduced(5,0));
        flowView.setBounds(area.removeFromTop(110));
        auto footer=area.removeFromBottom(32);
        loadInstrument.setBounds(footer.removeFromLeft(175).reduced(1)); showInstrument.setBounds(footer.removeFromLeft(150).reduced(1)); status.setBounds(footer);
        auto mapArea=area.removeFromBottom(91);
        auto scope=mapArea.removeFromTop(28); scopeMode.setBounds(scope.removeFromLeft(148)); nextUnassigned.setBounds(scope.removeFromRight(140));
        repeats.setBounds(scope.removeFromRight(185)); rangeLabel.setBounds(scope);
        mapView.setBounds(mapArea.reduced(1)); map.setSize(std::max(1,map.chords.size()*ChordMap::cell),45);
        auto left=area.removeFromLeft(area.getWidth()/4); area.removeFromLeft(8);
        auto mid=area.removeFromLeft(area.getWidth()/2); area.removeFromLeft(8);
        timelineTitle.setBounds(left.removeFromTop(32)); candidateTitle.setBounds(mid.removeFromTop(32)); inspectMode.setBounds(area.removeFromTop(32).reduced(1));
        auto nav=left.removeFromBottom(30); back.setBounds(nav.removeFromLeft(32)); forward.setBounds(nav.removeFromRight(32)); chords.setBounds(nav);
        timeline.setBounds(left.reduced(0,2));
        auto policy=mid.removeFromTop(28); strategy.setBounds(policy.removeFromLeft(policy.getWidth()/2)); lens.setBounds(policy);
        genre.setBounds(mid.removeFromTop(28)); choices.setBounds(mid.removeFromBottom(28)); candidates.setBounds(mid.reduced(0,2));
        notes.setBounds(area.removeFromTop(37));
        auto edit=area.removeFromBottom(31); roots.setBounds(edit.removeFromLeft(51)); modes.setBounds(edit);
        apply.setBounds(area.removeFromBottom(32).reduced(1));
        auto ab=area.removeFromBottom(32); resetA.setBounds(ab.removeFromRight(61).reduced(1)); pin.setBounds(ab.removeFromRight(58).reduced(1));compare.setBounds(ab.reduced(1));
        auto play=area.removeFromBottom(31); stop.setBounds(play.removeFromRight(60).reduced(1)); audition.setBounds(play.reduced(1));
        details.setBounds(area.reduced(3));
        compareText.setBounds(details.getBounds()); fitRows.setBounds(details.getBounds());
    }
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff111923));}
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
        int first=std::min(map.anchor,map.end), last=std::max(map.anchor,map.end);
        if(scopeMode.getSelectedId()==3) {first=0; last=map.chords.size()-1;}
        if(scopeMode.getSelectedId()==2 && first>=0 && first<map.chords.size()) {
            const auto events=proc.document.score["harmony"]["chords"];
            const juce::int64 at=events[first]["start"];
            const auto sections=proc.document.score["timeline"]["sections"];
            bool found=false;
            if (auto* list=sections.getArray()) for (const auto& section:*list) {
                const juce::int64 start=section["start"], end=start+juce::int64(section["duration"]);
                if (at<start || at>=end) continue;
                found=true;
                last=first;
                while(first>0 && juce::int64(events[first-1]["start"])>=start) --first;
                while(last+1<events.size() && juce::int64(events[last+1]["start"])<end) ++last;
                break;
            }
            if (!found) {
                map.targets.clear(); apply.setEnabled(false); repeats.setEnabled(false);
                rangeLabel.setText("No section at this chord",juce::dontSendNotification); map.repaint(); return;
            }
        }
        map.targets=proc.document.scope(first,last,repeats.getToggleState() && scopeMode.getSelectedId()==1);
        apply.setEnabled(!map.targets.empty());
        repeats.setEnabled(scopeMode.getSelectedId()==1);
        rangeLabel.setText("Song scale map   /   "+juce::String(int(map.targets.size()))+" chords",juce::dontSendNotification);
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
        rows=juce::JSON::parse(call("rankChord",{chords.getSelectedItemIndex(),strategies[strategy.getSelectedItemIndex()],lenses[lens.getSelectedItemIndex()],genreIds[genre.getSelectedItemIndex()]}).toString());
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
        candidates.labels.clear(); candidates.subtitles.clear();
        for(int i=0;i<rows.size();++i) {
            candidates.labels.add(rows[i]["name"].toString()); candidates.subtitles.add(rows[i]["pitches"].toString());
        }
        candidates.updateContent();
        timeline.selectRow(chords.getSelectedItemIndex(),false,true);
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
    static int modeFor(const juce::var& item) {
        const auto values=item["intervals"];
        for(size_t i=0;i<catalog().size();++i) {
            const auto& intervals=catalog()[i].intervals;
            if(values.size()==int(intervals.size()) && std::equal(intervals.begin(),intervals.end(),values.getArray()->begin(),
                [](int a,const juce::var& b){return a==int(b);})) return int(i);
        }
        return -1;
    }
    void applySongPlan() {
        const juce::ScopedLock lock(proc.stateLock);
        const juce::StringArray strategies{"balanced","smooth","tension"},lenses{"chord","key","performance"};
        std::vector<Document::Assignment> plan;
        for(int i=0;i<chords.getNumItems();++i) {
            const auto ranked=juce::JSON::parse(call("rankChord",{i,strategies[strategy.getSelectedItemIndex()],
                lenses[lens.getSelectedItemIndex()],genreIds[genre.getSelectedItemIndex()]}).toString());
            if(auto* list=ranked.getArray()) for(const auto& choice:*list) {
                const int mode=modeFor(choice);
                if(mode>=0){ plan.push_back({i,int(choice["root"]),mode}); break; }
            }
        }
        if(int(plan.size())!=chords.getNumItems()) {selectChord();status.setText("Could not assign a scale to every chord.",juce::dontSendNotification);return;}
        const auto before=proc.document;
        const auto result=proc.document.assignPlan(plan);
        if(result.failed()){status.setText(result.getErrorMessage(),juce::dontSendNotification);return;}
        previousDocument=before; undoAtText=proc.document.text; undo.setEnabled(true); redoDocument.reset(); redo.setEnabled(false);
        sync(); proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }
    void selectCandidate() {
        const auto item=rows[choices.getSelectedItemIndex()];
        if(!item.isObject()) return;
        candidates.selectRow(choices.getSelectedItemIndex(),false,true);
        roots.setSelectedId(int(item["root"])+1,juce::dontSendNotification);
        modes.setSelectedId(modeFor(item)+1,juce::dontSendNotification);
        notes.setText(item["pitches"].toString(),juce::dontSendNotification);
        const auto caution=item["warnings"].toString();
        details.setText(item["description"].toString()+"\n"+(caution.isNotEmpty()?caution:item["reason"].toString()),juce::dontSendNotification);
        updateInspector();
    }
    void updateInspector() {
        const auto tab=inspectMode.getSelectedId(), index=choices.getSelectedItemIndex();
        flowTab.setToggleState(!showConnections,juce::dontSendNotification);
        connectionsTab.setToggleState(showConnections,juce::dontSendNotification);
        flowKey.setText(showConnections ? "Guide tones and approach notes  /  motion in semitones"
                                        : "Previous, selected and next chords  /  fill: scale tones   outline: chord tones",juce::dontSendNotification);
        flowView.update(index>=0 ? juce::JSON::parse(call("flowView",{index}).toString()) : juce::var{},showConnections);
        details.setVisible(tab==1); compareText.setVisible(tab==2); fitRows.setVisible(tab==3);
        if(tab==2) compareText.setText(index>=0 ? call("compareNotes",{index}).toString() : juce::String{},juce::dontSendNotification);
        if(tab==3) {
            fitRows.labels.clear(); fitRows.subtitles.clear();
            const auto stats=index>=0 ? juce::JSON::parse(call("noteFit",{index}).toString()) : juce::var{};
            if (auto* list=stats.getArray()) for(const auto& row:*list) {
                const int pct=row["percent"];
                fitRows.labels.add(row["track"].toString() + "    " + (pct>=0 ? juce::String(pct)+"%" : juce::String::charToString(0x2014)));
                const auto outside=row["outside"].toString();
                fitRows.subtitles.add(pct<0 ? "No notes in this chord" : outside.isEmpty() ? "All notes in scale" : "Outside   "+outside);
            }
            fitRows.updateContent(); fitRows.repaint();
        }
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
        nextUnassigned.setEnabled(ready && map.scales.contains(juce::String{})); scopeMode.setEnabled(ready);
        autoSet.setEnabled(ready); pin.setEnabled(ready);
        if(redoDocument && redoAtText!=proc.document.text){redoDocument.reset();redo.setEnabled(false);}
        for(juce::Component* c:std::initializer_list<juce::Component*>{&apply,&roots,&modes,&chords}) c->setEnabled(ready);
        timeline.labels=map.chords; timeline.subtitles=map.scales;
        for(int i=0;i<timeline.labels.size();++i) timeline.labels.set(i,juce::String(i+1)+"   "+timeline.labels[i]);
        timeline.updateContent();
        if (source.isVisible()) source.setText(proc.document.text,false);
        if (!ready) { candidates.labels.clear(); candidates.subtitles.clear(); candidates.updateContent(); choices.clear(juce::dontSendNotification); updateInspector(); }
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
        previousDocument.reset(); undo.setEnabled(false); redoDocument.reset(); redo.setEnabled(false);
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
    ScaleRows timeline,candidates;
    FlowView flowView;
    juce::TextButton flowTab{"Scale Flow"}, connectionsTab{"Connections"};
    juce::Label flowKey;
    bool showConnections=false;
    ScaleRows fitRows;
    juce::ComboBox inspectMode,scopeMode;
    juce::Label compareText;
    juce::TextButton nextUnassigned{"Next unassigned"},resetA{"Reset A"};
    juce::Label timelineTitle{"","Chord timeline"},candidateTitle{"","Compare scales"},inspectorTitle{"","Scale inspector"};
    juce::TextButton sourceToggle{"OSTF text"};
    juce::TextEditor source;
    ChordMap map;
    juce::Viewport mapView;
    juce::ToggleButton repeats{"Include matching progressions"};
    juce::TextButton undo{"Undo apply"};
    juce::Label rangeLabel;
    juce::TextButton autoSet{"Auto-set song"},redo{"Redo"},pin{"Pin A"};
    std::optional<Document> previousDocument,redoDocument;
    juce::String redoAtText;
    juce::String undoAtText;
    juce::Label song,status,notes,details;
    juce::TextButton back{"<"},forward{">"},audition{"Hear candidate"},compare{"A/B with reference"},stop{"Stop"},
        loadInstrument{"Load VST3 instrument..."},showInstrument;
    juce::ComboBox choices,strategy,lens,genre;
    juce::StringArray genreIds;
    juce::ComboBox chords,roots,modes;
    juce::TextButton open{"Open OSTF"}, refresh{"Read Studio score"}, save{"Save OSTF..."},apply{"Assign scale"};
    Drag drag;
    std::unique_ptr<juce::FileChooser> chooser;
};
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor;}
