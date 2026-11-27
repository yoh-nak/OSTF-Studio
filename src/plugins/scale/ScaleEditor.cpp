#include "../StudioPluginEditor.h"
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
        if (!connected.load(std::memory_order_relaxed)) { audio.clear(); midi.clear(); return; }
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
    struct Input final : ProjectScales {
        explicit Input(Processor& p) : owner(p) {}
        Processor& owner;
        std::atomic<Steinberg::uint32> references{1};
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID id, void** out) override {
            if (!out) return Steinberg::kInvalidArgument;
            if (std::memcmp(id,projectScalesId,16) && std::memcmp(id,Steinberg::FUnknown_iid,16)) {
                *out = nullptr; return Steinberg::kNoInterface;
            }
            *out = static_cast<ProjectScales*>(this); addRef(); return Steinberg::kResultOk;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return ++references; }
        Steinberg::uint32 PLUGIN_API release() override {
            const auto n = --references;
            if (!n) delete this;
            return n;
        }
        Steinberg::tresult PLUGIN_API syncProject(ProjectWriter* writer, const char* bytes, Steinberg::int32 size) override {
            if (!writer || !bytes || size <= 0 || size > maxBytes) return Steinberg::kInvalidArgument;
            const juce::ScopedLock lock(owner.stateLock);
            const auto source = juce::String::fromUTF8(bytes,size);
            if (source != owner.document.text) {
                Document next;
                if (next.load(source).failed()) return Steinberg::kResultFalse;
                owner.document = std::move(next);
                if (!owner.submitting) ++owner.restoredStateVersion;
                owner.sendChangeMessage();
            }
            owner.writer = writer;
            owner.connected.store(true);
            return Steinberg::kResultOk;
        }
    };
    int32_t queryIAudioProcessor(const Steinberg::TUID id,void** out) override {
        if (!out) return Steinberg::kInvalidArgument;
        if (std::memcmp(id,projectScalesId,16)) return juce::VST3ClientExtensions::queryIAudioProcessor(id,out);
        *out = static_cast<ProjectScales*>(new Input(*this));
        return Steinberg::kResultOk;
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        const juce::ScopedLock lock(stateLock);
        juce::XmlElement xml("ScaleEditor");
        if (legacyScore) {
            xml.addChildElement(new juce::XmlElement(*legacyScore));
            xml.setAttribute("baseHash",legacyHash);
        }
        if (legacyDraft) xml.addChildElement(new juce::XmlElement(*legacyDraft));
        xml.setAttribute("previewMidiOut",rack.previewMidiOut());
        xml.setAttribute("midiThru",rack.midiThruEnabled());
        xml.setAttribute("editorWidth",lastEditorSize.x);
        xml.setAttribute("editorHeight",lastEditorSize.y);
        if (workspaceSettings.isObject())
            xml.createNewChildElement("View")->addTextElement(juce::JSON::toString(workspaceSettings,true));
        if (unavailableInstrument) xml.addChildElement(new juce::XmlElement(*unavailableInstrument));
        else rack.save(xml);
        copyXmlToBinary(xml,dest);
    }
    void setStateInformation(const void* data,int size) override {
        if (const auto xml = getXmlFromBinary(data,size); xml && xml->hasTagName("ScaleEditor")) {
            const juce::ScopedLock lock(stateLock);
            lastEditorSize={juce::jlimit(740,8192,xml->getIntAttribute("editorWidth",1120)),
                            juce::jlimit(550,8192,xml->getIntAttribute("editorHeight",800))};
            // Keep private edits from older versions recoverable by that version.
            // Only the connected project supplies the working score.
            auto copyChild=[&](const char* tag) -> std::unique_ptr<juce::XmlElement> {
                if (const auto* child=xml->getChildByName(tag)) return std::make_unique<juce::XmlElement>(*child);
                return {};
            };
            legacyScore=copyChild("Score");
            legacyDraft=copyChild("Workspace");
            legacyHash=xml->getStringAttribute("baseHash");
            workspaceSettings=juce::var{};
            if (auto* view=xml->getChildByName("View"))
                workspaceSettings=juce::JSON::parse(view->getAllSubText());
            else if (auto* legacy=xml->getChildByName("Workspace"))
                workspaceSettings=juce::JSON::parse(legacy->getAllSubText())["settings"];
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
        }
    }
    bool submit(const juce::String& base, const juce::String& score) {
        if (!writer || base != document.baseHash || score.getNumBytesAsUTF8() > maxBytes) return false;
        auto keepAlive=writer;
        juce::ScopedValueSetter<bool> guard(submitting,true);
        return keepAlive->applyScales(base.toRawUTF8(),score.toRawUTF8(),int(score.getNumBytesAsUTF8())) == Steinberg::kResultOk;
    }
    Steinberg::IPtr<ProjectWriter> writer;
    std::atomic<bool> connected{false};
    bool submitting=false;
    juce::CriticalSection stateLock;
    std::atomic<int> inputPitchClasses{0};
    std::array<unsigned,128> heldChannels{};
    juce::Point<int> lastEditorSize{1120,800};
    Document document;
    uint64_t restoredStateVersion=0;
    juce::var workspaceSettings;
    std::unique_ptr<juce::XmlElement> legacyScore,legacyDraft;
    juce::String legacyHash;
    juce::String rackError;
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
        const auto panel = findColour(juce::TextButton::buttonColourId);
        const auto accent = findColour(ostfstudio::console::LookAndFeel::accentColourId);
        for (int i = std::max(0,clip.getX()/cell); i < chords.size() && i <= clip.getRight()/cell; ++i) {
            juce::Rectangle<int> tile(i*cell,0,cell-2,getHeight()-1);
            const bool active = i>=std::min(anchor,end) && i<=std::max(anchor,end);
            const auto target = std::binary_search(targets.begin(),targets.end(),i);
            g.setColour(active ? panel.interpolatedWith(accent,.35f) : target ? panel.brighter(.15f) : panel);
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
    ScaleRows() { setModel(this); setRowHeight(40); }
    ~ScaleRows() override { setModel(nullptr); }
    juce::StringArray labels, subtitles;
    std::function<void(int)> choose;
    int getNumRows() override { return labels.size(); }
    void paintListBoxItem(int i,juce::Graphics& g,int w,int h,bool selected) override {
        if (i>=labels.size()) return;
        g.fillAll(findColour(selected ? juce::TextButton::buttonOnColourId : int(backgroundColourId)));
        g.setColour(findColour(selected ? int(ostfstudio::console::LookAndFeel::accentColourId) : int(juce::TextButton::buttonColourId)));
        g.fillRect(0,0,3,h-1);
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(14.f,juce::Font::bold));
        g.drawText(labels[i],10,3,w-16,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffaec0d0)); g.setFont(juce::FontOptions(12.f));
        g.drawText(subtitles[i],10,21,w-16,16,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int i) override { if (i>=0 && choose) choose(i); }
};

class Editor final : public ostfstudio::StudioPluginEditor, private juce::ChangeListener, private juce::Timer {
public:
    explicit Editor(Processor& p) : StudioPluginEditor(p), proc(p) {
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
        for (juce::Component* c : std::initializer_list<juce::Component*>{&song,&chords,&roots,&modes,&apply,&status,&back,&forward,&strategy,&lens,&genre,&choices,&details,&notes,&audition,&compare,&stop,&loadInstrument,&showInstrument})
            addAndMakeVisible(c);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&mapView,&repeats,&rangeLabel,&undo})
            addAndMakeVisible(c);
        for (auto* c : std::initializer_list<juce::Component*>{&timeline,&candidates,&timelineTitle,&candidateTitle,&inspectorTitle,&autoSet,&redo,&pin})
            addAndMakeVisible(c);
        for(auto* heading:{&timelineTitle,&candidateTitle,&inspectorTitle}) {
            heading->setColour(juce::Label::textColourId,juce::Colour(0xff7fe0c0));
            heading->setFont(juce::FontOptions(15.f,juce::Font::bold));
        }
        for (juce::Component* c : std::initializer_list<juce::Component*>{&inspectMode,&fitRows,&compareText,&scopeMode,&nextUnassigned,&resetA}) addAndMakeVisible(c);
        addAndMakeVisible(flowView);
        addAndMakeVisible(flowKey);
        addAndMakeVisible(fullWorkspace);
        fullWorkspace.onClick=[this]{openFullWorkspace();};
        addChildComponent(disconnected);
        disconnected.setJustificationType(juce::Justification::centred);
        disconnected.setColour(juce::Label::backgroundColourId,juce::Colour(0xff17232e));
        disconnected.setColour(juce::Label::textColourId,juce::Colours::white);
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
            auto before=proc.document; if (!submit(redoDocument->text)) return;
            previousDocument=std::move(before); redoDocument.reset();
            undoAtText=proc.document.text; undo.setEnabled(true); redo.setEnabled(false); sync();
            proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
        };
        autoSet.onClick=[this]{applySongPlan();};
        pin.onClick=[this]{call("pinReference",{choices.getSelectedItemIndex()});inspectMode.setSelectedId(2,juce::dontSendNotification);updateInspector();};
        undo.setEnabled(false);
        undo.onClick=[this] {
            const juce::ScopedLock lock(proc.stateLock);
            if (!previousDocument || undoAtText != proc.document.text) { sync(); return; }
            auto before=proc.document; if (!submit(previousDocument->text)) return;
            redoDocument=std::move(before); redoAtText=proc.document.text; redo.setEnabled(true);
            previousDocument.reset(); undo.setEnabled(false);
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
            auto proposal=before;
            const auto r = proposal.assign(map.targets,roots.getSelectedItemIndex(),modes.getSelectedItemIndex());
            if (r.wasOk() && submit(proposal.text)) {
                redoDocument.reset(); redo.setEnabled(false);
                previousDocument = before; undoAtText = proc.document.text; undo.setEnabled(true);
                const auto first=map.anchor, last=map.end;
                proc.updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true)); sync();
                map.anchor=first; map.end=last; updateScope();
            }
            else if (r.failed()) status.setText(r.getErrorMessage(),juce::dontSendNotification);
        };
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
    void resized() override {
        if (getWidth()>=740 && getHeight()>=550) {
            const juce::ScopedLock lock(proc.stateLock);
            proc.lastEditorSize={getWidth(),getHeight()};
        }
        auto area=getLocalBounds().reduced(12);
        auto head=area.removeFromTop(32);
        language.setBounds(head.removeFromRight(145).reduced(1)); fullWorkspace.setBounds(head.removeFromRight(145).reduced(1));
        redo.setBounds(head.removeFromRight(60).reduced(1)); undo.setBounds(head.removeFromRight(95).reduced(1));
        autoSet.setBounds(head.removeFromRight(115).reduced(1)); song.setBounds(head);
        area.removeFromTop(8);
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
        for (auto* control : std::initializer_list<juce::Component*>{&song,&language,&fullWorkspace,&autoSet,&undo,&redo,&loadInstrument,&showInstrument,
                &testSound,&previewMidiOut,&midiThru,&status})
            control->setVisible(!full);
        disconnected.setBounds(getLocalBounds());
        setEnabled(proc.connected.load());
        disconnected.setVisible(!proc.connected.load());
        if (disconnected.isVisible()) disconnected.toFront(false);
        if (full) workspace->setBounds(getLocalBounds());
    }
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
        disconnected.setText(words("Open Scale Editor on an instrument track inside OSTF Studio. In another DAW, load OSTF Studio (VST3) first.",
            "OSTF Studio内の音源トラックでScale Editorを開いてください。他のDAWでは、まずOSTF Studio（VST3）を読み込みます。"),juce::dontSendNotification);
        caption(autoSet,"Set song scales","全曲にスケールを設定"); caption(undo,"Undo apply","適用を取り消す"); caption(redo,"Redo","やり直す");
        caption(apply,"Assign scale","スケールを設定"); caption(nextUnassigned,"Next unassigned","次の未設定コード");
        caption(loadInstrument,"Instruments...","音源を選択…"); caption(testSound,"Test sound","テスト発音"); caption(previewMidiOut,"Preview MIDI out","試聴をMIDI出力");
        caption(midiThru,"MIDI thru","MIDIスルー");
        midiThru.setTooltip(words("Also send incoming MIDI to the host output, on its original channel.","鍵盤などの入力MIDIを、元のチャンネルのままホストにも送ります。"));
        caption(audition,"Hear candidate","候補を試聴"); caption(compare,"A/B with reference","基準音と比較"); caption(stop,"Stop","停止");
        caption(flowTab,"Scale Flow","スケールの流れ"); caption(connectionsTab,"Connections","つながり"); caption(reviewTab,"Review","確認");
        caption(pin,"Pin A","Aを固定"); caption(resetA,"Reset A","Aをリセット"); caption(repeats,"Include matching progressions","同じ進行を含める");
        fullWorkspace.setButtonText(workspace && workspace->isVisible() ? words("Native controls","簡易エディター") : words("Full editor","詳細エディター"));
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
        if (!chords.getNumItems()) status.setText(words("Add chords to the Studio project.","Studioのプロジェクトにコードを追加してください。"),juce::dontSendNotification);
        shownLanguage.clear();
    }
    void showInitialWorkspace() {
        if (proc.connected.load() && !openedWorkspace) {
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
        auto proposal=before;
        const auto result=proposal.assignPlan(plan);
        if(result.failed()){status.setText(result.getErrorMessage(),juce::dontSendNotification);return;}
        if (!submit(proposal.text)) return;
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
        flowView.setVisible(!showingReview);
        reviewList.setVisible(showingReview);
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
        auto picker=std::make_unique<InstrumentPicker>(proc.rack.description(),japanese,face);
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
        chooser=std::make_unique<juce::FileChooser>(words("Load a VST3 instrument","VST3音源を読み込む"),start,"*.vst3",useNativeVst3Chooser);
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
        if (!ready) { candidates.labels.clear(); candidates.subtitles.clear(); candidates.updateContent(); choices.clear(juce::dontSendNotification); updateInspector(); }
        back.setEnabled(false);forward.setEnabled(false);audition.setEnabled(ready);compare.setEnabled(ready);
        updateInstrument();
        if(ready) selectChord(); else status.setText(words("Add chords to the Studio project.","Studioのプロジェクトにコードを追加してください。"),juce::dontSendNotification);
        if(proc.unavailableInstrument) {
            const auto* info=proc.unavailableInstrument->getFirstChildElement();
            const auto name=info ? info->getStringAttribute("name") : juce::String{};
            status.setText(name+"  /  "+words("Select the same instrument to recover its preset.",
                                               "同じ音源を選ぶと、保存した音色を復元できます。"),juce::dontSendNotification);
            status.setTooltip(proc.rackError);
        } else status.setTooltip({});
    }
    int shownPitchClasses=-1, shownMidiRoute=-1, shownHostFlags=-1;
    juce::String shownInstrument,shownLanguageChoice,shownStatus;
    void handleHostAction(const juce::var& request) {
        const auto action=request["action"].toString();
        const auto value=request["value"];
        if (action=="native") openFullWorkspace();
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
        const auto flags=(int(showInstrument.isEnabled())<<1)
            | (route<<2) | (int(proc.rack.midiThruEnabled())<<3);
        if (flags!=shownHostFlags || languageChoice!=shownLanguageChoice) {
            juce::var state(new juce::DynamicObject());
            auto& fields=*state.getDynamicObject();
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
            return;
        }
        const juce::ScopedLock lock(proc.stateLock);
        const auto snapshot=proc.document.text;
        if (workspace && workspaceHash==digest(snapshot)) {
            workspace->setVisible(true); workspace->toFront(false);
            resized(); return;
        }
        auto html=juce::String::fromUTF8(ScaleAssets::ScaleWorkspace_html,ScaleAssets::ScaleWorkspace_htmlSize)
            .replace("<!--STUDIO_THEME-->", "<style>:root{--bg:#"+juce::Colour(face.background).toDisplayString(false)
                +";--raised:#"+juce::Colour(face.panel).toDisplayString(false)
                +";--panel:#"+juce::Colour(face.background).interpolatedWith(juce::Colour(face.panel),.5f).toDisplayString(false)
                +";--line:#"+juce::Colour(face.panel).brighter(.16f).toDisplayString(false)
                +";--control-text:#"+findColour(juce::ComboBox::textColourId).toDisplayString(false)
                +";--menu-selection:#"+findColour(juce::PopupMenu::highlightedBackgroundColourId).toDisplayString(false)
                +";--accent:#"+juce::Colour(face.accent).toDisplayString(false)+"}</style>")
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
            juce::var draft(new juce::DynamicObject());
            draft.getDynamicObject()->setProperty("settings",proc.workspaceSettings);
            html=html.replace("<!--STUDIO_DRAFT-->",juce::Base64::toBase64(juce::JSON::toString(draft,true)));
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
                .withEventListener("ostfScaleChooseInstrument", [self=juce::Component::SafePointer<Editor>(this),page](juce::var pos) {
                    if (!self || self->workspaceGeneration!=page || !self->workspace) return;
                    const auto bounds=self->workspace->getScreenBounds();
                    const auto x=juce::jlimit(0.0,1.0,double(pos["x"]));
                    const auto y=juce::jlimit(0.0,1.0,double(pos["y"]));
                    self->chooseInstrument({bounds.getX()+juce::roundToInt(bounds.getWidth()*x),
                        bounds.getY()+juce::roundToInt(bounds.getHeight()*y),1,1});
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
            return;
        }
       #endif
        status.setText(words("Using native scale controls.","簡易エディターでスケールを編集できます。"),juce::dontSendNotification);
    }
    void rememberWorkspace(const juce::var& patch) {
        const juce::ScopedLock lock(proc.stateLock);
        if (seenStateVersion!=proc.restoredStateVersion || workspaceSource!=proc.document.text || !patch["settings"].isObject()) return;
        proc.workspaceSettings=patch["settings"];
        if (!patch["score"].isString()) return;
        if (!submit(patch["score"].toString())) return;
        workspaceSource=proc.document.text;
        workspaceHash=proc.document.baseHash;
        sync();
    }
    bool submit(const juce::String& proposed) {
        if (proc.submit(proc.document.baseHash,proposed)) return true;
        ++proc.restoredStateVersion;
        proc.sendChangeMessage();
        status.setText(words("Edit was not applied. Check the current Studio project and try again.",
                             "編集を反映できませんでした。Studioの現在の曲と録音状態を確認してください。"),juce::dontSendNotification);
        return false;
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
    juce::TextButton fullWorkspace{"Full editor"};
    std::unique_ptr<juce::WebBrowserComponent> workspace;
    juce::String workspaceHash,workspaceSource;
    int workspaceGeneration=0;
    uint64_t seenStateVersion=0;
    bool openedWorkspace = false;
    juce::Label flowKey;
    bool showConnections=false;
    ScaleRows fitRows;
    juce::ComboBox inspectMode,scopeMode;
    juce::Label compareText;
    juce::TextButton nextUnassigned{"Next unassigned"},resetA{"Reset A"};
    juce::Label timelineTitle{"","Chord timeline"},candidateTitle{"","Compare scales"},inspectorTitle{"","Scale inspector"};
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
    juce::TextButton apply{"Assign scale"};
    juce::Label disconnected;
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
