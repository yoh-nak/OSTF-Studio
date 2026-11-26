#include "ScaleDocument.h"
#include "FlowView.h"
#include "ScoreExchange.h"
#include "InstrumentRack.h"
#include "InstrumentPicker.h"
#include "ScaleAssets.h"
#include "ui/StudioLanguage.h"
#include <juce_javascript/juce_javascript.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>

namespace {
using namespace ostfstudio::scale;
class WorkspaceBrowser final : public juce::WebBrowserComponent {
public:
    using WebBrowserComponent::WebBrowserComponent;
    bool pageAboutToLoad(const juce::String& url) override {
        return url == "about:blank" || url == getResourceProviderRoot()
            || url.startsWith(getResourceProviderRoot() + "#");
    }
};
class Processor final : public juce::AudioProcessor, public juce::VST3ClientExtensions,
                        public juce::ChangeBroadcaster {
public:
    explicit Processor(juce::File preferences) : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)), languageFile(std::move(preferences)), rack(*this) {}
    const juce::File languageFile;
    const juce::String getName() const override { return "OSTF Scale Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void reset() override {heldChannels.fill(0); inputPitchClasses.store(0); rack.stopPreview(); rack.resetInput();}
    void prepareToPlay(double rate,int block) override {reset(); rack.prepare(rate,block);}
    void releaseResources() override {rack.release(); reset();}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi) override {
        // Observe host input before the rack inserts its preview notes.
        bool changed=false;
        for (const auto packet : midi) {
            if (packet.numBytes!=3) continue;
            const auto message=packet.getMessage();
            const unsigned channel=1u << (packet.data[0] & 15);
            if (message.isNoteOn() || message.isNoteOff()) {
                auto& held=heldChannels[size_t(message.getNoteNumber())];
                held=message.isNoteOn() ? held|channel : held&~channel;
                changed=true;
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                for (auto& held : heldChannels) held&=~channel;
                changed=true;
            }
        }
        if (changed) {
            int mask=0;
            for (size_t key=0; key<heldChannels.size(); ++key)
                if (heldChannels[key]) mask |= 1 << (key%12);
            inputPitchClasses.store(mask,std::memory_order_relaxed);
        }
        rack.process(audio,midi);
    }
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
            if (owner.document.text.isEmpty() || (owner.linkedFile.file==juce::File{} && owner.document.baseHash == digest(owner.document.text)
                    && !owner.hasWorkspaceEdits())) {
                const auto r = owner.document.load(source);
                if (r.failed()) return Steinberg::kResultFalse;
                owner.linkedFile={};
                ++owner.restoredStateVersion;
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
        xml.setAttribute("previewMidiOut",rack.previewMidiOut());
        xml.setAttribute("midiThru",rack.midiThruEnabled());
        xml.setAttribute("editorWidth",lastEditorSize.x);
        xml.setAttribute("editorHeight",lastEditorSize.y);
        xml.createNewChildElement("Score")->addTextElement(document.text);
        if (workspaceSource == document.text && workspaceDraft.isObject()) {
            auto* draft=xml.createNewChildElement("Workspace");
            draft->setAttribute("source",digest(document.text));
            draft->addTextElement(juce::JSON::toString(workspaceDraft,true));
        }
        if (unavailableInstrument) xml.addChildElement(new juce::XmlElement(*unavailableInstrument));
        else rack.save(xml);
        copyXmlToBinary(xml,dest);
    }
    void setStateInformation(const void* data,int size) override {
        if (const auto xml = getXmlFromBinary(data,size); xml && xml->hasTagName("ScaleEditor")) {
            const juce::ScopedLock lock(stateLock);
            lastEditorSize={juce::jlimit(740,8192,xml->getIntAttribute("editorWidth",1120)),
                            juce::jlimit(550,8192,xml->getIntAttribute("editorHeight",800))};
            const auto* score=xml->getChildByName("Score");
            rack.setPreviewMidiOut(xml->getBoolAttribute("previewMidiOut",false));
            rack.setMidiThru(xml->getBoolAttribute("midiThru",false));
            const auto restored=rack.restore(xml->getChildByName("Instrument"));
            rackError=restored.getErrorMessage();
            unavailableInstrument.reset();
            if (restored.failed()) {
                unavailableInstrument=std::make_unique<juce::XmlElement>(*xml->getChildByName("Instrument"));
                rack.restore(nullptr);
            }
            sendChangeMessage();
            if (document.load(score ? score->getAllSubText() : xml->getAllSubText()).wasOk()) {
                linkedFile={};
                ++restoredStateVersion;
                workspaceDraft=juce::var();
                workspaceSource.clear();
                if (auto* draft=xml->getChildByName("Workspace"); draft && draft->getStringAttribute("source")==digest(document.text)) {
                    auto saved=juce::JSON::parse(draft->getAllSubText());
                    if (saved["settings"].isObject() && saved["score"].toString().getNumBytesAsUTF8()<=maxBytes
                            && saved["text"].toString().getNumBytesAsUTF8()<=maxBytes) {
                        workspaceDraft=std::move(saved);
                        workspaceSource=document.text;
                    }
                }
                const auto hash = xml->getStringAttribute("baseHash");
                if (hash.length()==64 && hash.containsOnly("0123456789abcdef")) document.baseHash = hash;
                sendChangeMessage();
            }
        }
    }
    juce::CriticalSection stateLock;
    std::atomic<int> inputPitchClasses{0};
    std::array<unsigned,128> heldChannels{};
    juce::Point<int> lastEditorSize{1120,800};
    Document document;
    LinkedScoreFile linkedFile;
    juce::String workspaceSource;
    uint64_t restoredStateVersion=0;
    juce::var workspaceDraft;
    bool hasWorkspaceEdits() const {
        return workspaceSource==document.text && (workspaceDraft.hasProperty("score") || workspaceDraft.hasProperty("text"));
    }
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

class Editor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::ChangeListener, private juce::Timer {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), proc(p) {
        language.setComponentID("scale-language");
        addAndMakeVisible(language);
        languageChoice = proc.languageFile.loadFileAsString().trim();
        if (languageChoice != "en" && languageChoice != "ja") languageChoice = "system";
        language.onChange = [this] {
            const auto id = language.getSelectedId();
            const juce::String choice = id == 2 ? "en" : id == 3 ? "ja" : "system";
            const bool saved = proc.languageFile.getParentDirectory().createDirectory().wasOk()
                && proc.languageFile.replaceWithText(choice);
            if (saved) languageChoice = choice;
            applyLanguage();
            if (!saved) status.setText(words("Could not save the language setting.", "言語設定を保存できませんでした。"),juce::dontSendNotification);
        };
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
        addAndMakeVisible(fullWorkspace); addAndMakeVisible(returnWorkspace);
        fullWorkspace.onClick=[this]{openFullWorkspace();}; returnWorkspace.onClick=[this]{importFullWorkspace();};
        returnWorkspace.setEnabled(false);
        addChildComponent(reviewList);
        reviewList.setTitle("Chord review"); reviewList.setComponentID("scale-review-list");
        reviewList.choose=[this](int row){
            if (row >= 0 && row < reviewIndices.size()) chords.setSelectedItemIndex(reviewIndices[row],juce::sendNotificationSync);
        };
        for (auto* tab : {&flowTab,&connectionsTab,&reviewTab}) {
            addAndMakeVisible(*tab);
            tab->onClick=[this,tab]{showConnections=tab==&connectionsTab; showingReview=tab==&reviewTab; updateInspector();};
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
            if (workspace && workspace->isVisible()) {
                workspace->evaluateJavascript("window.ScaleAdviserApp.view(window.ScaleAdviserApp.state.view === 'text' ? 'visual' : 'text')");
                return;
            }
            const bool show=!source.isVisible();
            source.setVisible(show);
            if (show) { source.setText(proc.document.text,false); source.toFront(false); }
            sourceToggle.setButtonText(show ? words("Visual","ビジュアル") : words("OSTF text","OSTFテキスト")); updateInspector(); resized();
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
        addAndMakeVisible(testSound);
        addAndMakeVisible(previewMidiOut);
        addAndMakeVisible(midiThru);
        midiThru.setComponentID("scale-midi-thru");
        midiThru.onClick=[this] {
            proc.rack.setMidiThru(midiThru.getToggleState());
            proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
        };
        previewMidiOut.setTooltip("Send audition notes to the host on MIDI channel 1. Route the plug-in MIDI output to an instrument track.");
        previewMidiOut.onClick=[this] {
            proc.rack.setPreviewMidiOut(previewMidiOut.getToggleState());
            proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
        };
        testSound.onClick=[this] {
            proc.rack.audition(juce::JSON::parse(R"([{"at":0,"note":60,"velocity":94,"ms":480}])"));
            status.setText("Test note C4  /  "+(proc.rack.previewMidiOut() ? juce::String("MIDI out, Ch 1") : proc.rack.name()),juce::dontSendNotification);
        };
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
        open.onClick = [this]{ choose(false); };
        save.onClick = [this]{
            if (workspace && workspace->isVisible()) workspace->evaluateJavascript("window.ScaleAdviserApp.saveAs()");
            else choose(true);
        };
        refresh.onClick = [this]{ requestLoad(proc.offered); };
        proc.addChangeListener(this);
        applyLanguage();
        sync();
        juce::Point<int> size;
        { const juce::ScopedLock lock(proc.stateLock); size=proc.lastEditorSize; seenStateVersion=proc.restoredStateVersion; }
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
            size.x = std::min(size.x,display->userArea.getWidth()-24);
            size.y = std::min(size.y,display->userArea.getHeight()-60);
        }
        setResizable(true,true); setResizeLimits(740,550,8192,8192);
        setSize(std::max(740,size.x),std::max(550,size.y));
        showInitialWorkspace();
        startTimerHz(25);
    }
    ~Editor() override {
        if (pickerBox) pickerBox->dismiss();
        stopTimer();
        proc.rack.stopPreview(); proc.rack.closeWindow(); proc.removeChangeListener(this);
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        return files.size()==1 && juce::File(files[0]).hasFileExtension("ostf");
    }
    void filesDropped(const juce::StringArray& files,int,int) override {
        if (isInterestedInFileDrag(files)) openScoreFile(juce::File(files[0]));
    }
    void resized() override {
        if (getWidth()>=740 && getHeight()>=550) {
            const juce::ScopedLock lock(proc.stateLock);
            proc.lastEditorSize={getWidth(),getHeight()};
        }
        auto area=getLocalBounds().reduced(12);
        auto head=area.removeFromTop(32); language.setBounds(head.removeFromRight(145).reduced(1)); sourceToggle.setBounds(head.removeFromRight(95)); returnWorkspace.setBounds(head.removeFromRight(140).reduced(1)); fullWorkspace.setBounds(head.removeFromRight(145).reduced(1)); song.setBounds(head);
        auto files=area.removeFromTop(32);
        open.setBounds(files.removeFromLeft(100).reduced(1)); refresh.setBounds(files.removeFromLeft(145).reduced(1));
        save.setBounds(files.removeFromLeft(99).reduced(1)); redo.setBounds(files.removeFromRight(59).reduced(1));
        undo.setBounds(files.removeFromRight(89).reduced(1)); autoSet.setBounds(files.removeFromRight(115).reduced(1)); drag.setBounds(files.reduced(1));
        area.removeFromTop(8);
        if (source.isVisible()) source.setBounds(area);
        auto tabs=area.removeFromTop(27);
        flowTab.setBounds(tabs.removeFromLeft(105).reduced(1));
        connectionsTab.setBounds(tabs.removeFromLeft(115).reduced(1));
        reviewTab.setBounds(tabs.removeFromLeft(100).reduced(1));
        flowKey.setBounds(tabs.reduced(5,0));
        flowView.setBounds(area.removeFromTop(110));
        reviewList.setBounds(flowView.getBounds());
        auto footer=area.removeFromBottom(32);
        loadInstrument.setBounds(footer.removeFromLeft(136).reduced(1)); showInstrument.setBounds(footer.removeFromLeft(160).reduced(1));
        testSound.setBounds(footer.removeFromLeft(89).reduced(1));
        previewMidiOut.setBounds(footer.removeFromLeft(145).reduced(1));
        midiThru.setBounds(footer.removeFromLeft(108).reduced(1)); status.setBounds(footer);
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
        const bool full=workspace && workspace->isVisible();
        for (auto* control : std::initializer_list<juce::Component*>{&song,&language,&sourceToggle,&returnWorkspace,
                &fullWorkspace,&open,&refresh,&save,&autoSet,&undo,&redo,&drag,&loadInstrument,&showInstrument,
                &testSound,&previewMidiOut,&midiThru,&status})
            control->setVisible(!full);
        if (full) workspace->setBounds(getLocalBounds());
    }
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff111923));}
private:
    juce::String words(const char* en,const char* ja) const {
        return juce::String::fromUTF8(japanese ? ja : en);
    }
    void applyLanguage() {
        japanese = ostfstudio::resolvedLanguage(languageChoice,juce::SystemStats::getDisplayLanguage())=="ja";
        language.clear(juce::dontSendNotification);
        language.setTitle(words("Settings · Display language","設定・表示言語"));
        language.setTooltip(language.getTitle());
        language.getRootMenu()->addSectionHeader(language.getTitle());
        language.addItem(words("Language: System","言語：OSに合わせる"),1);
        language.addItem("Language: English",2); language.addItem(juce::String::fromUTF8("言語：日本語"),3);
        language.setSelectedId(languageChoice=="en" ? 2 : languageChoice=="ja" ? 3 : 1,juce::dontSendNotification);
        auto caption=[this](juce::Button& b,const char* en,const char* ja){ b.setButtonText(words(en,ja)); };
        caption(open,"Open OSTF","OSTFを開く"); caption(refresh,"Read Studio score","Studioから読み込む");
        caption(save,"Save OSTF...","OSTFを保存…"); caption(drag,"Copy scales to Studio","Studio用にコピー");
        drag.setTooltip(words("Click to copy, then choose Edit > Paste scales from Scale Editor in Studio. You can also drag to Studio.",
            "クリックでコピーし、Studioの「編集」→「Scale Editorのスケールを貼り付け」で反映します。Studioへドラッグすることもできます。"));
        caption(autoSet,"Auto-set song","曲全体に自動設定"); caption(undo,"Undo apply","適用を取り消す"); caption(redo,"Redo","やり直す");
        caption(apply,"Assign scale","スケールを設定"); caption(nextUnassigned,"Next unassigned","次の未設定コード");
        caption(loadInstrument,"Instruments...","音源を選択…"); caption(testSound,"Test sound","テスト発音"); caption(previewMidiOut,"Preview MIDI out","試聴をMIDI出力");
        caption(midiThru,"MIDI thru","MIDIスルー");
        midiThru.setTooltip(words("Also send incoming MIDI to the host output, on its original channel.","鍵盤などの入力MIDIを、元のチャンネルのままホストにも送ります。"));
        caption(audition,"Hear candidate","候補を試聴"); caption(compare,"A/B with reference","基準音と比較"); caption(stop,"Stop","停止");
        caption(flowTab,"Scale Flow","スケールの流れ"); caption(connectionsTab,"Connections","つながり"); caption(reviewTab,"Review","確認");
        caption(pin,"Pin A","Aを固定"); caption(resetA,"Reset A","Aをリセット"); caption(repeats,"Include matching progressions","同じ進行を含める");
        fullWorkspace.setButtonText(workspace && workspace->isVisible() ? words("Native controls","簡易エディター") : words("Full editor","詳細エディター"));
        returnWorkspace.setButtonText(workspaceFile && !workspace ? words("Import edited scales","編集結果を読み込む") : words("Apply scales","スケールを適用"));
        sourceToggle.setButtonText(source.isVisible()?words("Visual","ビジュアル"):words("OSTF text","OSTFテキスト"));
        timelineTitle.setText(words("Chord timeline","コード進行"),juce::dontSendNotification);
        candidateTitle.setText(words("Compare scales","スケールを比較"),juce::dontSendNotification);
        inspectorTitle.setText(words("Scale inspector","スケールの詳細"),juce::dontSendNotification);
        auto items=[this](juce::ComboBox& box,std::initializer_list<const char*> en,std::initializer_list<const char*> ja){
            int id=1;
            for (auto label : japanese ? ja : en) box.changeItemText(id++,juce::String::fromUTF8(label));
        };
        items(inspectMode,{"Scale inspector","Compare A / B","Note Fit"},{"スケールの詳細","A / B 比較","音の適合度"});
        items(scopeMode,{"Chord / drag range","Section","Whole song"},{"コード／選択範囲","構成","曲全体"});
        items(strategy,{"Balanced","Smooth voice leading","Tension / resolution"},{"バランス","滑らかな声部進行","緊張と解決"});
        items(lens,{"Chord scale","Key and mode","Performance"},{"コードスケール","調とモード","演奏"});
        updateInstrument();
        if (!chords.getNumItems()) status.setText(words("Open an OSTF score with a chord track.","コード進行を含むOSTFを開いてください。"),juce::dontSendNotification);
        shownLanguage.clear();
    }
    struct Drag final : juce::TextButton {
        Drag():TextButton("Copy scales to Studio"){ setComponentID("return-scales-to-studio"); onClick=[this]{if (!started && beginExport) beginExport(false);}; }
        std::function<void(bool)> beginExport;
        void mouseDown(const juce::MouseEvent& e) override { started=false; juce::TextButton::mouseDown(e); }
        void mouseUp(const juce::MouseEvent& e) override { juce::TextButton::mouseUp(e); started=false; }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (started || !e.mouseWasDraggedSinceMouseDown()) return;
            started=true;
            if (beginExport) beginExport(true);
        }
        bool started=false;
    };
    void showInitialWorkspace() {
        if (!openedWorkspace) {
            openedWorkspace = true;
            openFullWorkspace();
        }
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        const juce::ScopedLock lock(proc.stateLock);
        if (seenStateVersion!=proc.restoredStateVersion) {
            seenStateVersion=proc.restoredStateVersion;
            ++workspaceGeneration;
            openedWorkspace=workspace && !workspace->isVisible();
            workspace.reset();
            resized();
            workspaceHash.clear();
            previousDocument.reset();
            redoDocument.reset();
            undo.setEnabled(false);
            redo.setEnabled(false);
            fullWorkspace.setButtonText(words("Full editor","詳細エディター"));
            returnWorkspace.setEnabled(false);
        }
        sync(); showInitialWorkspace();
    }
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
        status.setText(lastError.isNotEmpty()?lastError:label.isEmpty() ? words("Select a candidate or set a custom scale below.","候補を選ぶか、下でスケールを指定してください。") : words("Assigned: ","設定済み：")+label,juce::dontSendNotification);
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
        flowTab.setToggleState(!showConnections && !showingReview,juce::dontSendNotification);
        connectionsTab.setToggleState(showConnections,juce::dontSendNotification);
        flowKey.setText(showConnections ? "Guide tones and approach notes  /  motion in semitones"
                                        : "Previous, selected and next chords  /  fill: scale tones   outline: chord tones",juce::dontSendNotification);
        reviewTab.setToggleState(showingReview,juce::dontSendNotification);
        flowView.setVisible(!showingReview && !source.isVisible());
        reviewList.setVisible(showingReview && !source.isVisible());
        if (showingReview) {
            const auto rows=juce::JSON::parse(call("reviewChords",{}).toString());
            reviewIndices.clear(); reviewList.labels.clear(); reviewList.subtitles.clear();
            for (int i=0;i<rows.size();++i) {
                const auto row=rows[i]; const int chord=row["index"];
                reviewIndices.add(chord);
                reviewList.labels.add(juce::String(chord+1)+"   "+row["chord"].toString()+"   /   "+row["level"].toString());
                reviewList.subtitles.add(row["reason"].toString());
            }
            reviewList.updateContent(); reviewList.repaint();
            flowKey.setText(rows.size() ? "Select a chord to compare its scale choices" : "No chords need review",juce::dontSendNotification);
        }
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
        status.setText(ab?words("Playing reference, then candidate.","基準音、候補の順に試聴中です。"):words("Playing candidate.","候補を試聴中です。"),juce::dontSendNotification);
    }
    void chooseInstrument(juce::Rectangle<int> anchor={}) {
        if (pickerBox) { pickerBox->toFront(true); return; }
        auto picker=std::make_unique<InstrumentPicker>(proc.rack.description(),japanese);
        const auto self=juce::Component::SafePointer<Editor>(this);
        picker->onChoose=[self, panel=juce::Component::SafePointer<InstrumentPicker>(picker.get())](InstrumentChoice c,bool audition) {
            if (!self) return;
            const auto result = self->loadChoice(c);
            if (result.failed()) {
                if (panel) panel->showStatus(result.getErrorMessage());
            } else if (audition) {
                if (panel) panel->showStatus(c.name + self->words("  /  Playing C4","  /  C4を試聴中"));
            } else if (self->pickerBox) self->pickerBox->dismiss();
            self->instrumentChanged(result,audition);
        };
        picker->onReference=[self] { if(self) { self->pickerBox->dismiss(); self->instrumentChanged(self->loadChoice({})); } };
        picker->onBrowse=[self] { if(self) { self->pickerBox->dismiss(); self->browseInstrument(); } };
        pickerBox=&juce::CallOutBox::launchAsynchronously(std::move(picker),anchor.isEmpty() ? loadInstrument.getScreenBounds() : anchor,nullptr);
    }
    juce::Result loadChoice(const InstrumentChoice& choice) {
        const juce::ScopedLock lock(proc.stateLock);
        auto* missing=proc.unavailableInstrument.get();
        const auto result=choice.scanned ? proc.rack.select(*choice.scanned,missing)
            : choice.path.isEmpty() ? proc.rack.restore(nullptr)
            : proc.rack.choose(juce::File(choice.path),choice.needsInspection ? juce::String{} : choice.name,missing);
        if (result.wasOk()) {
            proc.unavailableInstrument.reset();
            proc.rackError.clear();
        }
        return result;
    }
    void instrumentChanged(const juce::Result& result,bool audition=false) {
        if (result.failed()) { status.setText(result.getErrorMessage(),juce::dontSendNotification); return; }
        updateInstrument();
        status.setTooltip({});
        if (audition) proc.rack.audition(juce::JSON::parse(R"([{"at":0,"note":60,"velocity":85,"ms":380}])"),true);
        status.setText(proc.rack.name()+"  /  "+(audition ? words("Playing C4","C4を試聴中") : words("Ready to audition","試聴できます")),juce::dontSendNotification);
        proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }
    void browseInstrument() {
        const auto paths=juce::VST3PluginFormat{}.getDefaultLocationsToSearch();
        juce::File start;
        for(int i=0;i<paths.getNumPaths();++i) if(paths[i].isDirectory()){start=paths[i];break;}
        chooser=std::make_unique<juce::FileChooser>("Load a VST3 instrument",start,"*.vst3",useNativeVst3Chooser);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories,
            [self=juce::Component::SafePointer<Editor>(this)](const auto& fc){
                if(!self || fc.getResult()==juce::File{}) return;
                self->instrumentChanged(self->loadChoice({{}, {}, fc.getResult().getFullPathName(), {}}));
            });
    }
    void updateInstrument() {
        showInstrument.setButtonText(proc.rack.hasInstrument() ? proc.rack.name() : words("Reference tone","基準音"));
        showInstrument.setEnabled(proc.rack.hasInstrument());
    }
    void sync() {
        const juce::ScopedLock lock(proc.stateLock);
        previewMidiOut.setToggleState(proc.rack.previewMidiOut(),juce::dontSendNotification);
        midiThru.setToggleState(proc.rack.midiThruEnabled(),juce::dontSendNotification);
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
        if(ready) selectChord(); else status.setText(words("Open an OSTF score with a chord track.","コード進行を含むOSTFを開いてください。"),juce::dontSendNotification);
        if(proc.unavailableInstrument) {
            const auto* info=proc.unavailableInstrument->getFirstChildElement();
            const auto name=info ? info->getStringAttribute("name") : juce::String{};
            status.setText(name+"  /  "+words("Select the same instrument to recover its preset.",
                                               "同じ音源を選ぶと、保存した音色を復元できます。"),juce::dontSendNotification);
            status.setTooltip(proc.rackError);
        } else status.setTooltip({});
        drag.beginExport=[this](bool dragFile) { beginStudioReturn(dragFile); };
    }
    void beginStudioReturn(bool dragFile) {
        if (!workspace || !workspace->isVisible()) {
            Document snapshot;
            { const juce::ScopedLock guard(proc.stateLock); snapshot=proc.document; }
            returnDocument(snapshot,dragFile);
            return;
        }
        const auto hash=workspaceHash;
        workspace->evaluateJavascript("({draft:window.ScaleAdviserApp.state.textDirty,score:window.ScaleAdviserApp.exportText()})",
            [self=juce::Component::SafePointer<Editor>(this),page=workspaceGeneration,hash,dragFile](auto evaluated) {
                if (!self || page!=self->workspaceGeneration) return;
                const auto* value=evaluated.getResult();
                if (!value || !(*value)["score"].isString() || bool((*value)["draft"])) {
                    self->status.setText(self->words("Apply pending text edits before returning scales.","テキストの編集を適用してからスケールを戻してください。"),juce::dontSendNotification);
                    return;
                }
                Document snapshot;
                {
                    const juce::ScopedLock guard(self->proc.stateLock);
                    if (self->seenStateVersion!=self->proc.restoredStateVersion) return;
                    snapshot=self->proc.document;
                }
                const auto result=snapshot.importWorkspaceScales((*value)["score"].toString(),hash);
                if (result.failed()) self->status.setText(result.getErrorMessage(),juce::dontSendNotification);
                else if (!dragFile || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()) self->returnDocument(snapshot,dragFile);
            });
    }
    void returnDocument(const Document& document,bool dragFile) {
        if (!dragFile) {
            juce::SystemClipboard::copyTextToClipboard(clipboardReturnPrefix + document.baseHash + "\n" + document.text);
            status.setText(words("Copied. In Studio, choose Edit > Paste scales from Scale Editor.","コピーしました。Studioの「編集」→「Scale Editorのスケールを貼り付け」で反映します。"),juce::dontSendNotification);
            return;
        }
        const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("OSTF Scale Editor");
        juce::File file;
        const auto result=document.exportForStudio(dir,file);
        if (result.failed()) status.setText(result.getErrorMessage(),juce::dontSendNotification);
        else juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,this);
    }
    void requestLoad(juce::String source, LinkedScoreFile link={}) {
        if (workspace && workspace->isVisible() && workspaceSource.isNotEmpty()) {
            workspace->evaluateJavascript("Boolean(window.ScaleAdviserApp?.state.dirty || window.ScaleAdviserApp?.state.textDirty)",
                [self=juce::Component::SafePointer<Editor>(this),source,link](auto result) {
                    if (self) self->confirmLoad(source,link,result.getResult() && bool(*result.getResult()));
                });
        } else {
            const juce::ScopedLock lock(proc.stateLock);
            confirmLoad(source,link,proc.hasWorkspaceEdits());
        }
    }
    void confirmLoad(const juce::String& source,const LinkedScoreFile& link,bool workspaceDirty) {
        const juce::ScopedLock lock(proc.stateLock);
        if (!workspaceDirty && !proc.hasWorkspaceEdits() && (proc.document.text.isEmpty() || proc.document.baseHash == digest(proc.document.text))) {
            load(source,link);
            return;
        }
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            "Read another score?", "Replace the scale assignments currently open in the editor?", "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create([self=juce::Component::SafePointer<Editor>(this),source,link](int answer) {
                if (self && answer) self->load(source,link);
            }));
    }
    void load(const juce::String& source,const LinkedScoreFile& link) {
        const juce::ScopedLock lock(proc.stateLock);
        const auto r=proc.document.load(source);
        if(r.failed()) {status.setText(r.getErrorMessage(),juce::dontSendNotification); return;}
        proc.linkedFile=link;
        proc.workspaceDraft=juce::var();
        proc.workspaceSource.clear();
        previousDocument.reset(); undo.setEnabled(false); redoDocument.reset(); redo.setEnabled(false);
        if (workspace) {
            workspace.reset();
        }
        openedWorkspace = false;
        sync(); showInitialWorkspace();
        proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }
    int shownPitchClasses=-1, shownMidiRoute=-1, shownHostFlags=-1;
    juce::String shownInstrument,shownLanguageChoice,shownStatus;
    void handleHostAction(const juce::var& request) {
        const auto action=request["action"].toString();
        const auto value=request["value"];
        if (action=="read" && refresh.isEnabled()) refresh.onClick();
        else if ((action=="copy" || action=="drag") && drag.isEnabled()) beginStudioReturn(action=="drag");
        else if (action=="commit" && returnWorkspace.isEnabled()) importFullWorkspace();
        else if (action=="native") openFullWorkspace();
        else if (action=="instrument" && showInstrument.isEnabled()) showInstrument.onClick();
        else if (action=="test") testSound.onClick();
        else if ((action=="preview" || action=="thru") && value.isBool()) {
            auto& button=action=="preview" ? previewMidiOut : midiThru;
            button.setToggleState(bool(value),juce::dontSendNotification);
            button.onClick();
        } else if (action=="language") {
            const auto choice=value.toString();
            if (choice=="system" || choice=="en" || choice=="ja")
                language.setSelectedId(choice=="system" ? 1 : choice=="en" ? 2 : 3,juce::sendNotificationSync);
            shownHostFlags=-1;
        }
    }
    void timerCallback() override {
        if (!workspace || !workspace->isShowing()) { shownPitchClasses=shownMidiRoute=-1; shownLanguage.clear(); return; }
        const juce::String locale=japanese ? "ja" : "en";
        if (locale!=shownLanguage) {
            workspace->emitEventIfBrowserIsVisible("ostfScaleLanguage",locale);
            shownLanguage=locale;
        }
        const int route=proc.rack.previewMidiOut() ? 1 : 0;
        const auto flags=int(refresh.isEnabled()) | (int(showInstrument.isEnabled())<<1)
            | (route<<2) | (int(proc.rack.midiThruEnabled())<<3);
        if (flags!=shownHostFlags || languageChoice!=shownLanguageChoice) {
            juce::var state(new juce::DynamicObject());
            auto& fields=*state.getDynamicObject();
            fields.setProperty("read",refresh.isEnabled());
            fields.setProperty("instrument",showInstrument.isEnabled());
            fields.setProperty("preview",route!=0);
            fields.setProperty("thru",proc.rack.midiThruEnabled());
            fields.setProperty("language",languageChoice);
            workspace->emitEventIfBrowserIsVisible("ostfScaleHostState",state);
            shownHostFlags=flags;
            shownLanguageChoice=languageChoice;
        }
        if (shownStatus!=status.getText()) {
            shownStatus=status.getText();
            workspace->emitEventIfBrowserIsVisible("ostfScaleHostStatus",shownStatus);
        }
        const auto instrumentName=showInstrument.getButtonText();
        if (route!=shownMidiRoute || instrumentName!=shownInstrument) {
            juce::var destination(new juce::DynamicObject());
            destination.getDynamicObject()->setProperty("external",route!=0);
            destination.getDynamicObject()->setProperty("instrument",instrumentName);
            workspace->emitEventIfBrowserIsVisible("ostfScaleMidiRoute",destination);
            shownMidiRoute=route;
            shownInstrument=instrumentName;
        }
        const int held=proc.inputPitchClasses.load(std::memory_order_relaxed);
        if (held!=shownPitchClasses) {
            workspace->emitEventIfBrowserIsVisible("ostfScaleInput",held);
            shownPitchClasses=held;
        }
    }
    void openFullWorkspace() {
        if (workspace && workspace->isVisible()) {
            workspace->setVisible(false);
            resized();
            fullWorkspace.setButtonText(words("Full editor","詳細エディター"));
            returnWorkspace.setEnabled(false);
            return;
        }
        const juce::ScopedLock lock(proc.stateLock);
        const auto snapshot=proc.document.text;
        if (workspace && workspaceHash==digest(snapshot)) {
            workspace->setVisible(true); workspace->toFront(false);
            resized();
            fullWorkspace.setButtonText(words("Native controls","簡易エディター")); returnWorkspace.setEnabled(snapshot.isNotEmpty()); return;
        }
        auto html=juce::String::fromUTF8(ScaleAssets::ScaleWorkspace_html,ScaleAssets::ScaleWorkspace_htmlSize)
            .replace("<html lang=\"en\">",japanese ? "<html lang=\"ja\">" : "<html lang=\"en\">")
            .replace("<!--STUDIO_SCORE-->",juce::Base64::toBase64(snapshot));
        using Browser = juce::WebBrowserComponent;
        auto options = Browser::Options{}.withKeepPageLoadedWhenBrowserIsHidden();
       #if JUCE_WINDOWS
        options = options.withBackend(Browser::Options::Backend::webview2)
            .withWinWebView2Options(Browser::Options::WinWebView2{}.withUserDataFolder(
                juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("OSTF Scale WebView")));
       #endif
       #if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
        if (Browser::areOptionsSupported(options)) {
            const int page=++workspaceGeneration;
            workspaceSource=snapshot;
            const auto draft=proc.workspaceSource==snapshot && proc.workspaceDraft.isObject()
                ? juce::Base64::toBase64(juce::JSON::toString(proc.workspaceDraft,true)) : juce::String{};
            html=html.replace("<!--STUDIO_DRAFT-->",draft);
            html=html.replace("<!--STUDIO_FILE-->",juce::Base64::toBase64(proc.linkedFile.file.getFileName()));
            options = options.withNativeIntegrationEnabled()
                .withEventListener("ostfScaleDraft",[self=juce::Component::SafePointer<Editor>(this),page](juce::var patch){
                    if (self && self->workspaceGeneration==page) self->rememberWorkspace(patch);
                })
                .withEventListener("ostfScaleInputReady", [self=juce::Component::SafePointer<Editor>(this),page](juce::var) {
                    if (self && self->workspaceGeneration==page) {
                        self->shownPitchClasses=self->shownMidiRoute=self->shownHostFlags=-1;
                        self->shownLanguage.clear();
                    }
                })
                .withEventListener("ostfScaleHostAction", [self=juce::Component::SafePointer<Editor>(this),page](juce::var request) {
                    if (self && self->workspaceGeneration==page && self->workspace && self->workspace->isVisible())
                        self->handleHostAction(request);
                })
                .withEventListener("ostfScaleOpen", [self=juce::Component::SafePointer<Editor>(this)](juce::var) {
                    if (self) self->choose(false);
                })
                .withEventListener("ostfScaleFirstDrop", [self=juce::Component::SafePointer<Editor>(this),page](juce::var text) {
                    if (text.isString()) juce::MessageManager::callAsync([self,page,text] {
                        if (self && self->workspaceGeneration==page && self->workspaceSource.isEmpty())
                            self->requestLoad(text.toString());
                    });
                })
                .withEventListener("ostfScaleChooseInstrument", [self=juce::Component::SafePointer<Editor>(this),page](juce::var pos) {
                    if (!self || self->workspaceGeneration!=page || !self->workspace) return;
                    const auto bounds=self->workspace->getScreenBounds();
                    const auto x=juce::jlimit(0.0,1.0,double(pos["x"]));
                    const auto y=juce::jlimit(0.0,1.0,double(pos["y"]));
                    self->chooseInstrument({bounds.getX()+juce::roundToInt(bounds.getWidth()*x),
                        bounds.getY()+juce::roundToInt(bounds.getHeight()*y),1,1});
                })
                .withEventListener("ostfScaleForgetFile", [self=juce::Component::SafePointer<Editor>(this),page](juce::var) {
                    if (!self || self->workspaceGeneration!=page) return;
                    const juce::ScopedLock lock(self->proc.stateLock);
                    self->proc.linkedFile={};
                })
                .withEventListener("ostfScaleSave", [self=juce::Component::SafePointer<Editor>(this),page](juce::var request) {
                    if (self && self->workspaceGeneration==page) self->saveWorkspaceFile(request);
                })
                .withEventListener("ostfScalePreview", [self=juce::Component::SafePointer<Editor>(this)](juce::var notes) {
                    if (!self || !notes.isArray() || notes.size() > 2048) return;
                    if (notes.size()) self->proc.rack.audition(notes);
                    else self->proc.rack.stopPreview();
                });
            options = options.withResourceProvider([html](const juce::String& path)->std::optional<Browser::Resource>{
                if(path!="/") return {};
                const auto* bytes=reinterpret_cast<const std::byte*>(html.toRawUTF8());
                return Browser::Resource{std::vector<std::byte>(bytes,bytes+html.getNumBytesAsUTF8()),"text/html"};
            });
            workspace=std::make_unique<WorkspaceBrowser>(options);
            workspace->setComponentID("scale-full-workspace");
            addAndMakeVisible(*workspace); resized();
            workspace->goToURL(Browser::getResourceProviderRoot());
            workspaceHash=digest(snapshot);
            fullWorkspace.setButtonText(words("Native controls","簡易エディター"));
            returnWorkspace.setButtonText(words("Apply scales","スケールを適用")); returnWorkspace.setEnabled(snapshot.isNotEmpty());
            return;
        }
       #endif
        if (snapshot.isEmpty()) return;
        auto file=std::make_unique<juce::TemporaryFile>(".html");
        if(!file->getFile().replaceWithText(html) || !file->getFile().startAsProcess()) {
            status.setText("Could not open the local Scale Editor in your browser.",juce::dontSendNotification); return;
        }
        workspaceFile=std::move(file); workspaceHash=digest(snapshot);
        returnWorkspace.setButtonText(words("Import edited scales","編集結果を読み込む"));
        returnWorkspace.setEnabled(true);
    }
    void importFullWorkspace() {
        if(workspace && workspace->isVisible()) {
            const auto opened=workspaceHash;
            workspace->evaluateJavascript("({text:window.ScaleAdviserApp.exportText(),epoch:window.ScaleAdviserApp.state.epoch})",
                [self=juce::Component::SafePointer<Editor>(this),page=workspaceGeneration,opened](auto result){
                    if(!self || !self->workspace || page!=self->workspaceGeneration) return;
                    const auto* value=result.getResult();
                    if(!value || !(*value)["text"].isString()) { self->status.setText("The full editor is still loading.",juce::dontSendNotification); return; }
                    const juce::ScopedLock lock(self->proc.stateLock);
                    if (self->seenStateVersion!=self->proc.restoredStateVersion) return;
                    const auto before=self->proc.document;
                    const auto r=self->proc.document.importWorkspaceScales((*value)["text"].toString(),opened);
                    if(r.failed()){self->status.setText(r.getErrorMessage(),juce::dontSendNotification);return;}
                    if (before.text!=self->proc.document.text) {
                        self->previousDocument=before; self->undoAtText=self->proc.document.text;
                        self->undo.setEnabled(true); self->redoDocument.reset(); self->redo.setEnabled(false);
                    }
                    self->workspaceHash=digest(self->proc.document.text);
                    self->workspaceSource=self->proc.document.text;
                    self->proc.workspaceSource=self->workspaceSource;
                    if (auto* draft=self->proc.workspaceDraft.getDynamicObject()) draft->removeProperty("score");
                    const auto& score=self->proc.document.score;
                    const auto metadata=score.hasProperty("document") ? juce::JSON::toString(score["document"],true) : juce::String("null");
                    self->workspace->evaluateJavascript("window.ScaleAdviserApp.acceptStudioRevision("+metadata+","
                        +juce::String(int((*value)["epoch"]))+")");
                    self->sync(); self->proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
                    self->status.setText("Scales applied to the plug-in score.",juce::dontSendNotification);
                });
            return;
        }
        chooser=std::make_unique<juce::FileChooser>("Import the full editor's exported scales",juce::File{},"*.ostf");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
            [self=juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& fc) {
                if(!self) return;
                const auto f=fc.getResult(); if(!f.existsAsFile()) return;
                if(f.getSize()>maxBytes) {self->status.setText("Scale export exceeds 24 MiB.",juce::dontSendNotification);return;}
                const juce::ScopedLock lock(self->proc.stateLock);
                const auto before=self->proc.document;
                const auto r=self->proc.document.importWorkspaceScales(f.loadFileAsString(),self->workspaceHash);
                if(r.failed()){self->status.setText(r.getErrorMessage(),juce::dontSendNotification);return;}
                if (before.text!=self->proc.document.text) {
                    self->previousDocument=before; self->undoAtText=self->proc.document.text;
                    self->undo.setEnabled(true); self->redoDocument.reset(); self->redo.setEnabled(false);
                }
                self->sync(); self->returnWorkspace.setEnabled(false);
                self->proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
            });
    }
    void rememberWorkspace(const juce::var& patch) {
        if (!patch["settings"].isObject()) return;
        if (patch["score"].toString().getNumBytesAsUTF8()>maxBytes || patch["text"].toString().getNumBytesAsUTF8()>maxBytes) {
            status.setText("Workspace draft exceeds 24 MiB.",juce::dontSendNotification);
            return;
        }
        {
            const juce::ScopedLock lock(proc.stateLock);
            if (seenStateVersion!=proc.restoredStateVersion || workspaceSource!=proc.document.text) return;
            if (proc.workspaceSource!=workspaceSource || !proc.workspaceDraft.isObject())
                proc.workspaceDraft=juce::var(new juce::DynamicObject());
            proc.workspaceSource=workspaceSource;
            for (const auto* key : {"settings","score","text"}) if (patch.hasProperty(key)) {
                if (patch[key].isVoid()) proc.workspaceDraft.getDynamicObject()->removeProperty(key);
                else proc.workspaceDraft.getDynamicObject()->setProperty(key,patch[key]);
            }
        }
        proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }
    void saveWorkspaceFile(const juce::var& request) {
        if (!workspace) return;
        const auto text = request["text"].toString();
        const int ticket = request["ticket"];
        const juce::ScopedLock lock(proc.stateLock);
        const auto reply = [browser=juce::Component::SafePointer<juce::WebBrowserComponent>(workspace.get()),ticket]
                (bool ok,const juce::String& name={},const juce::String& message={}) {
            if (browser) browser->evaluateJavascript("window.ScaleAdviserApp.finishStudioSave(" + juce::String(ticket)
                + "," + (ok ? "true" : "false") + "," + juce::JSON::toString(name,true)
                + "," + juce::JSON::toString(message,true) + ")");
        };
        if (workspaceSaveChooser) { reply(false); return; }
        auto checked=std::make_shared<Document>();
        if (text.getNumBytesAsUTF8() > maxBytes || checked->load(text).failed()) {
            status.setText("Invalid OSTF export.",juce::dontSendNotification); reply(false); return;
        }
        auto write=[self=juce::Component::SafePointer<Editor>(this),checked,reply,page=workspaceGeneration,
                    version=proc.restoredStateVersion](const juce::File& file) {
            if (!self) return;
            const juce::ScopedLock stateLock(self->proc.stateLock);
            if (page!=self->workspaceGeneration || version!=self->proc.restoredStateVersion) {reply(false); return;}
            const auto result=self->proc.linkedFile.write(*checked,file,true);
            self->status.setText(result.wasOk() ? "Saved edited OSTF." : result.getErrorMessage(),juce::dontSendNotification);
            reply(result.wasOk(),file.getFileName(),result.getErrorMessage());
        };
        if (request["mode"]=="save") {write(proc.linkedFile.file); return;}
        workspaceSaveChooser=std::make_unique<juce::FileChooser>("Save edited OSTF as",proc.linkedFile.file,"*.ostf");
        workspaceSaveChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting,
            [self=juce::Component::SafePointer<Editor>(this),write,reply](const juce::FileChooser& fc) {
                if (!self) return;
                const auto file = fc.getResult();
                if (file != juce::File{}) write(file); else reply(false);
                self->workspaceSaveChooser.reset();
            });
    }
    void openScoreFile(const juce::File& file) {
        LinkedScoreFile link; juce::String text;
        const auto result=link.read(file,text);
        if (result.wasOk()) requestLoad(text,link);
        else status.setText(result.getErrorMessage(),juce::dontSendNotification);
    }
    void choose(bool writing) {
        chooser=std::make_unique<juce::FileChooser>(writing?"Save OSTF":"Open OSTF",juce::File{},"*.ostf");
        using F=juce::FileBrowserComponent;
        chooser->launchAsync((writing?F::saveMode|F::warnAboutOverwriting:F::openMode)|F::canSelectFiles,
            [self=juce::Component::SafePointer<Editor>(this),writing](const auto& fc) {
                if (!self) return;
                auto file=fc.getResult(); if(file==juce::File{}) return;
                if (!writing) {self->openScoreFile(file); return;}
                const juce::ScopedLock lock(self->proc.stateLock);
                auto saved=self->proc.linkedFile;
                const auto result=saved.write(self->proc.document,file);
                if (result.wasOk() && (!self->workspace || file==self->proc.linkedFile.file))
                    self->proc.linkedFile=saved;
                self->status.setText(result.wasOk()?"Saved OSTF.":result.getErrorMessage(),juce::dontSendNotification);
            });
    }
    using ChangeDetails = juce::AudioProcessorListener::ChangeDetails;
    Processor& proc;
    juce::ComboBox language;
    juce::String languageChoice,shownLanguage;
    bool japanese=false;
    juce::JavascriptEngine js;
    juce::Result engineStatus=juce::Result::ok();
    juce::String loadedText,lastError;
    juce::var rows;
    ScaleRows timeline,candidates;
    FlowView flowView;
    juce::TextButton flowTab{"Scale Flow"}, connectionsTab{"Connections"}, reviewTab{"Review"};
    ScaleRows reviewList;
    juce::Array<int> reviewIndices;
    bool showingReview=false;
    juce::TextButton fullWorkspace{"Full editor"}, returnWorkspace{"Apply scales"};
    std::unique_ptr<juce::WebBrowserComponent> workspace;
    std::unique_ptr<juce::FileChooser> workspaceSaveChooser;
    juce::String workspaceHash,workspaceSource;
    int workspaceGeneration=0;
    uint64_t seenStateVersion=0;
    bool openedWorkspace = false;
    std::unique_ptr<juce::TemporaryFile> workspaceFile;
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
    juce::ToggleButton previewMidiOut{"Preview MIDI out"};
    juce::ToggleButton midiThru{"MIDI thru"};
    juce::TextButton back{"<"},forward{">"},audition{"Hear candidate"},compare{"A/B with reference"},stop{"Stop"},
        loadInstrument{"Instruments..."},showInstrument,testSound{"Test sound"};
    juce::Component::SafePointer<juce::CallOutBox> pickerBox;
    juce::ComboBox choices,strategy,lens,genre;
    juce::StringArray genreIds;
    juce::ComboBox chords,roots,modes;
    juce::TextButton open{"Open OSTF"}, refresh{"Read Studio score"}, save{"Save OSTF..."},apply{"Assign scale"};
    Drag drag;
    std::unique_ptr<juce::FileChooser> chooser;
};
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* createScaleEditorWithPreferences(const juce::File& file){return new Processor(file);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){
    juce::PropertiesFile::Options location;
    location.applicationName="Scale Editor Language";
    location.folderName="OSTF Studio";
    location.filenameSuffix=".txt";
    location.osxLibrarySubFolder="Application Support";
    return createScaleEditorWithPreferences(location.getDefaultFile());
}
