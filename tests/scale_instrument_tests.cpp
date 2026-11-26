#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

juce::AudioProcessor* createScaleEditorWithPreferences(const juce::File&);

static const juce::File& testLanguageFile() {
    static const juce::TemporaryFile preference(".txt");
    return preference.getFile();
}
static juce::AudioProcessor* createTestFilter(){return createScaleEditorWithPreferences(testLanguageFile());}

static bool sameMidi(const juce::MidiBuffer& actual,const juce::MidiBuffer& expected) {
    if (actual.getNumEvents()!=expected.getNumEvents()) return false;
    auto match=expected.begin();
    for (const auto event:actual) {
        const auto other=*match++;
        if (event.samplePosition!=other.samplePosition || event.numBytes!=other.numBytes
            || !std::equal(event.data,event.data+event.numBytes,other.data)) return false;
    }
    return true;
}

static bool testMidiThruBus(juce::AudioProcessor& plugin,juce::AudioProcessor& receiver) {
    plugin.setRateAndBufferSizeDetails(48000,256);
    plugin.prepareToPlay(48000,256);
    juce::AudioBuffer<float> block(2,256);
    juce::MidiBuffer expected;
    expected.addEvent(juce::MidiMessage::noteOn(3,67,juce::uint8(96)),35);
    expected.addEvent(juce::MidiMessage::noteOn(8,60,juce::uint8(85)),79);
    expected.addEvent(juce::MidiMessage::noteOff(3,67),141);
    expected.addEvent(juce::MidiMessage::noteOff(8,60),224);
    auto midi=expected;
    plugin.processBlock(block,midi);
    const bool passed=sameMidi(midi,expected) && block.getMagnitude(0,35)==0.f && block.getMagnitude(35,189)>.001f;
    plugin.releaseResources();
    // Discard Scale Editor's own audio so only the downstream synth can sound.
    block.clear();
    receiver.setRateAndBufferSizeDetails(48000,256);
    receiver.prepareToPlay(48000,256);
    receiver.processBlock(block,midi);
    const auto peak=block.getMagnitude(35,189);
    std::cout<<"Downstream FM Synth peak: "<<peak<<'\n';
    const bool downstreamPlayed=block.getMagnitude(0,35)==0.f && peak>.001f;
    receiver.releaseResources();
    return passed && downstreamPlayed;
}

static bool testKeyboardAndPreview(juce::AudioProcessor& processor,juce::AudioProcessorEditor& editor,
                                   juce::TextButton& testSound,juce::ToggleButton& preview) {
    auto* thru=dynamic_cast<juce::ToggleButton*>(editor.findChildWithID("scale-midi-thru"));
    if (!thru || thru->getToggleState()) return false; // Legacy state had no MIDI thru.
    auto enable=[](juce::ToggleButton& button,bool on){
        button.setToggleState(on,juce::dontSendNotification); button.onClick();
    };
    enable(*thru,true); enable(preview,true);
    juce::MemoryBlock bytes;
    processor.getStateInformation(bytes);
    const auto saved=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
    if (!saved || !saved->getBoolAttribute("midiThru")) return false;
    const auto key=juce::MidiMessage::noteOn(1,60,juce::uint8(88)),off=juce::MidiMessage::noteOff(1,60);
    const auto pedal=juce::MidiMessage::controllerEvent(3,64,127);
    const auto bend=juce::MidiMessage::pitchWheel(8,13000);
    juce::AudioBuffer<float> block(2,256);
    juce::MidiBuffer midi,expected;
    testSound.onClick();
    midi.addEvent(key,63); midi.addEvent(pedal,90); midi.addEvent(bend,201);
    expected.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(94)),0);
    expected.addEvent(off,63); expected.addEvents(midi,0,-1,0);
    processor.processBlock(block,midi);
    if (!sameMidi(midi,expected)) return false;
    // Restart and then cancel the audition while the live player holds C4.
    testSound.onClick(); midi.clear(); processor.processBlock(block,midi);
    if (!midi.isEmpty()) return false;
    enable(preview,false); processor.processBlock(block,midi);
    if (!midi.isEmpty()) return false;
    enable(*thru,false);
    expected.clear(); expected.addEvent(off,0);
    expected.addEvent(juce::MidiMessage::controllerEvent(3,64,0),0);
    processor.processBlock(block,midi);
    if (!sameMidi(midi,expected)) return false;
    midi.clear(); midi.addEvent(key,33); processor.processBlock(block,midi);
    if (!midi.isEmpty()) return false;
    midi.addEvent(off,0); processor.processBlock(block,midi);
    return midi.isEmpty();
}

static bool testRelink(juce::AudioProcessor& editor,juce::VST3PluginFormat& format,
                       const juce::PluginDescription& instrument,juce::Button& load) {
    juce::String error;
    auto synth=format.createInstanceFromDescription(instrument,48000,256,error);
    if (!synth) return false;
    const auto found=std::find_if(synth->getParameters().begin(),synth->getParameters().end(),
        [](auto* parameter){return parameter->getName(32)=="Tone";});
    if (found==synth->getParameters().end()) return false;
    auto* tone=*found;
    tone->setValueNotifyingHost(.19f);
    juce::MemoryBlock preset,session;
    synth->getStateInformation(preset);
    editor.getStateInformation(session);
    auto saved=juce::AudioProcessor::getXmlFromBinary(session.getData(),int(session.getSize()));
    auto* entry=saved ? saved->getChildByName("Instrument") : nullptr;
    if (!entry || !entry->getFirstChildElement()) return false;
    juce::TemporaryFile missing(".vst3");
    entry->getFirstChildElement()->setAttribute("file",missing.getFile().getFullPathName());
    entry->setAttribute("state",preset.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary(*saved,session);
    editor.setStateInformation(session.getData(),int(session.getSize()));
    load.onClick();
    editor.getStateInformation(session);
    saved=juce::AudioProcessor::getXmlFromBinary(session.getData(),int(session.getSize()));
    entry=saved ? saved->getChildByName("Instrument") : nullptr;
    juce::PluginDescription relinked;
    if (!entry || !entry->getFirstChildElement() || !relinked.loadFromXml(*entry->getFirstChildElement())
        || !juce::File(relinked.fileOrIdentifier).exists() || !preset.fromBase64Encoding(entry->getStringAttribute("state"))) return false;
    tone->setValueNotifyingHost(.8f);
    synth->setStateInformation(preset.getData(),int(preset.getSize()));
    return std::abs(tone->getValue()-.19f)<.001f;
}

static int testLinkedSave(const juce::File& source) {
    juce::TemporaryFile copy(".ostf");
    const auto file=copy.getFile();
    auto fixture=juce::JSON::parse(source.loadFileAsString());
    auto* production=fixture["extensions"]["org.ostf.studio"]["data"].getDynamicObject();
    if (!production) return 70;
    fixture["harmony"].getDynamicObject()->setProperty("scales",juce::Array<juce::var>{});
    const auto firstChord=fixture["harmony"]["chords"][0];
    production->setProperty("testDecimal","EXACT_DECIMAL_FIXTURE");
    const juce::String exactDecimal="0.123456789012345678901";
    if (!file.replaceWithText(juce::JSON::toString(fixture).replace("\"EXACT_DECIMAL_FIXTURE\"",exactDecimal))) return 70;
    std::unique_ptr<juce::AudioProcessor> processor(createTestFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    auto* drop=dynamic_cast<juce::FileDragAndDropTarget*>(editor.get());
    if (!drop) return 71;
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    auto* browser=dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
    if (!browser) return 72;
    struct Poll final : juce::Timer {
        std::function<void()> step;
        void timerCallback() override {step();}
    } timer;
    const auto deadline=juce::Time::getMillisecondCounterHiRes()+45000.;
    int stage=-2; bool pending=false,externallyEdited=false;
    const auto check=[&](const juce::String& script) {
        pending=true;
        browser->evaluateJavascript(script,[&](auto result){
            pending=false;
            if (const auto* value=result.getResult(); value && value->isBool() && bool(*value)) ++stage;
        });
    };
    timer.step=[&] {
        if (stage==6 || juce::Time::getMillisecondCounterHiRes()>deadline) {
            juce::MessageManager::getInstance()->stopDispatchLoop(); return;
        }
        if (pending) return;
        if (stage==-2) {
            check(R"JS(Boolean(document.querySelector('body.studio-host.no-score #emptyScore')?.offsetHeight
                && document.querySelector('header #hostRead:disabled')?.offsetWidth
                && document.querySelector('header #studioInstrumentPicker')?.offsetWidth
                && document.querySelector('header #hostDrag:disabled') && !window.ScaleAdviserApp.state.doc && !document.getElementById('demos')))JS");
        } else if (stage==-1) {
            drop->filesDropped({file.getFullPathName()},4,4);
            browser=dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
            if (!browser) {juce::MessageManager::getInstance()->stopDispatchLoop();return;}
            ++stage;
        } else if (stage==0) {
            check(R"JS((()=>{
                const app=window.ScaleAdviserApp;
                if(!app?.state.fileBound || !app.state.doc.harmony.chords.length)return false;
                if(!document.querySelector('header #hostRead:disabled'))return false;
                document.getElementById('textTab').click();
                const source=document.getElementById('textSource'), text=document.getElementById('textArea');
                if(!source.offsetWidth || !source.textContent.includes(app.state.doc.song.title)
                   || JSON.parse(text.value).song.title!==app.state.doc.song.title)return false;
                document.getElementById('visualTab').click();
                app.selectChord(app.state.doc.harmony.chords[0].id);
                app.apply();return true;
            })())JS");
        } else if (stage==1) {
            check(R"JS((()=>{const app=window.ScaleAdviserApp;
                if(!app.state.dirty || app.state.doc.harmony.scales.length!==1)return false;
                app.save();return true;})())JS");
        } else if (stage==2 || stage==4) {
            check(R"JS((()=>{const b=document.querySelector('[data-export="confirm"]');
                if(!b || b.disabled || b.textContent!=='Save linked file')return false;
                b.click();return true;})())JS");
        } else if (stage==3) {
            if (!externallyEdited) {
                const auto written=file.loadFileAsString();
                const auto scales=juce::JSON::parse(written)["harmony"]["scales"];
                if (scales.size()!=1 || scales[0]["start"]!=firstChord["start"] || scales[0]["duration"]!=firstChord["duration"]
                    || scales[0]["intervals"].size()<5 || !written.contains(exactDecimal)) return;
                if (!file.replaceWithText("Externally edited")) return;
                externallyEdited=true;
            }
            check(R"JS((()=>{const app=window.ScaleAdviserApp;
                if(app.state.savePendingText || app.state.dirty)return false;
                app.state.dirty=true;app.save();return true;})())JS");
        } else {
            check(R"JS((()=>{const s=window.ScaleAdviserApp.state;
                return !s.savePendingText && s.dirty && document.body.innerText.includes('linked file changed');})())JS");
        }
    };
    timer.startTimer(100);
    juce::MessageManager::getInstance()->runDispatchLoop();
    timer.stopTimer();
    if (stage!=6 || file.loadFileAsString()!="Externally edited") {
        std::cerr<<"Linked Save failed at stage "<<stage<<'\n'; return 73;
    }
    return 0;
}

static int testWorkspaceRecall(const juce::MemoryBlock& initial) {
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::WebBrowserComponent* browser=nullptr;
    auto open=[&](const juce::MemoryBlock& bytes) {
        editor.reset();
        processor.reset(createTestFilter());
        processor->setStateInformation(bytes.getData(),int(bytes.getSize()));
        editor.reset(processor->createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        browser=dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
        return browser!=nullptr;
    };
    if (!open(initial)) return 50;
    auto language=[&]{return dynamic_cast<juce::ComboBox*>(editor->findChildWithID("scale-language"));};
    if (!language() || language()->getSelectedId()!=1 || testLanguageFile().existsAsFile()) return 75;
    struct Check : juce::Timer {
        std::function<void()> step;
        void timerCallback() override { step(); }
    } timer;
    int stage=0,result=51;
    bool pending=false;
    auto checkPage=[&](const juce::String& script) {
        pending=true;
        browser->evaluateJavascript(script,[&](auto evaluated){
            pending=false;
            if (auto* value=evaluated.getResult(); value && value->isBool() && bool(*value)) ++stage;
        });
    };
    const auto deadline=juce::Time::getMillisecondCounterHiRes()+35000.;
    timer.step=[&] {
        if (juce::Time::getMillisecondCounterHiRes()>deadline) {
            std::cerr<<"Workspace recall timed out at stage "<<stage<<'\n';
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
        if (pending) return;
        if (stage==0) {
            checkPage(R"JS((()=>{
                const app=window.ScaleAdviserApp;
                if(!app?.state.doc?.harmony.scales.length)return false;
                const edited=JSON.parse(app.exportText());edited.harmony.scales=[];
                app.importString(JSON.stringify(edited),'Working.ostf',false);
                app.selectChord(edited.harmony.chords[1].id);
                document.querySelector('#pinReference').click();
                app.view('text');
                const field=document.querySelector('#textArea');field.value='\u672a\u78ba\u5b9a <draft> &';
                field.dispatchEvent(new Event('input'));
                return true;
            })())JS");
        } else if (stage==1) {
            language()->setSelectedId(3,juce::sendNotificationSync);
            checkPage(R"JS(document.documentElement.lang==='ja' && document.querySelector('#save').textContent==='\u7de8\u96c6\u4e2d\u306eOSTF\u3092\u4fdd\u5b58'
                && document.querySelector('#songTitle').textContent==='Save'
                && document.querySelector('#textArea').value==='\u672a\u78ba\u5b9a <draft> &')JS");
        } else if (stage==2) {
            juce::MemoryBlock bytes;
            processor->getStateInformation(bytes);
            const auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
            auto* draft=xml->getChildByName("Workspace");
            if (!draft || !juce::JSON::parse(draft->getAllSubText()).hasProperty("text")) return;
            if (!juce::JSON::parse(xml->getChildByName("Score")->getAllSubText())["harmony"]["scales"].size()) return;
            if (open(bytes)) ++stage;
        } else if (stage==3) {
            checkPage(R"JS((()=>{
                const app=window.ScaleAdviserApp,s=app?.state;
                return !!(document.documentElement.lang==='ja' && s?.textDirty && s.view==='text' && s.doc.harmony.scales.length===0 &&
                    s.selected===s.doc.harmony.chords[1].id && s.reference && s.reference===s.scale &&
                    document.querySelector('#textArea').value==='\u672a\u78ba\u5b9a <draft> &');
            })())JS");
        } else if (stage==4) {
            language()->setSelectedId(2,juce::sendNotificationSync);
            checkPage("document.documentElement.lang==='en' && document.querySelector('#save').textContent==='Save edited OSTF'");
        } else if (stage==5) {
            for (auto* child:editor->getChildren())
                if (auto* b=dynamic_cast<juce::TextButton*>(child); b && b->getButtonText()=="Apply scales") {
                    b->onClick(); ++stage; break;
                }
        } else if (stage==6) {
            juce::MemoryBlock bytes;
            processor->getStateInformation(bytes);
            const auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
            if (juce::JSON::parse(xml->getChildByName("Score")->getAllSubText())["harmony"]["scales"].size()) return;
            auto* draft=xml->getChildByName("Workspace");
            if (!draft || juce::JSON::parse(draft->getAllSubText()).hasProperty("score")) return;
            draft->setAttribute("source","stale");
            juce::AudioProcessor::copyXmlToBinary(*xml,bytes);
            std::unique_ptr<juce::AudioProcessor> stale(createTestFilter());
            stale->setStateInformation(bytes.getData(),int(bytes.getSize()));
            stale->getStateInformation(bytes);
            const auto checked=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
            if (checked->getChildByName("Workspace")) {
                result=52;
                juce::MessageManager::getInstance()->stopDispatchLoop();
                return;
            }
            processor->setStateInformation(initial.getData(),int(initial.getSize()));
            ++stage;
        } else if (stage==7) {
            browser=dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
            if (browser) checkPage("Boolean(window.ScaleAdviserApp?.state.doc?.harmony.scales.length && !window.ScaleAdviserApp.state.textDirty)");
        } else {
            language()->setSelectedId(1,juce::sendNotificationSync);
            result=testLanguageFile().loadFileAsString()=="system" ? 0 : 76;
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    };
    timer.startTimer(100);
    juce::MessageManager::getInstance()->runDispatchLoop();
    timer.stopTimer();
    return result;
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3 && argc!=4) return 1;
    if (!(argc==4 && juce::String(argv[3])=="--recall") && !testLanguageFile().replaceWithText("en")) return 75;
    const juce::File file{juce::String::fromUTF8(argv[1])};
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types,file.getFullPathName());
    juce::PluginDescription* instrument=nullptr;
    for (auto* type:types) if (type->isInstrument) {instrument=type; break;}
    if (!instrument || !instrument->isInstrument) {
        std::cerr<<"No instrument in "<<file.getFullPathName()<<" ("<<types.size()<<" classes)\n";
        return 2;
    }
    // Interactive verification of the shipped VST3 in a host with no Studio
    // score interface. Open an OSTF in the plug-in, edit and copy back to Studio.
    if (argc==3 && juce::String(argv[2])=="--host-window") {
        juce::String error;
        auto plugin=format.createInstanceFromDescription(*instrument,48000,256,error);
        if (!plugin) { std::cerr<<error<<'\n'; return 93; }
        plugin->prepareToPlay(48000,256);
        struct HostWindow final : juce::DocumentWindow {
            HostWindow() : DocumentWindow("Scale Editor - VST3 host check",juce::Colours::black,closeButton) {}
            void closeButtonPressed() override { juce::MessageManager::getInstance()->stopDispatchLoop(); }
        } window;
        auto* editor=plugin->createEditorIfNeeded();
        if (!editor) return 94;
        window.setUsingNativeTitleBar(true);
        window.setContentOwned(editor,true);
        window.centreWithSize(editor->getWidth(),editor->getHeight());
        window.setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoop();
        window.clearContentComponent();
        plugin->releaseResources();
        return 0;
    }
    if (argc==4 && juce::String(argv[3])=="--linked-save") {
        juce::String error;
        auto plugin=format.createInstanceFromDescription(*instrument,48000,256,error);
        if (!plugin || !plugin->acceptsMidi() || !plugin->producesMidi()) {
            std::cerr<<"Scale Editor VST3 MIDI buses unavailable: "<<error<<'\n'; return 74;
        }
        juce::OwnedArray<juce::PluginDescription> receivers;
        format.findAllTypesForFile(receivers,file.getSiblingFile("OSTF FM Synth.vst3").getFullPathName());
        if (receivers.isEmpty()) {std::cerr<<"FM Synth VST3 unavailable\n";return 77;}
        auto receiver=format.createInstanceFromDescription(*receivers[0],48000,256,error);
        if (!receiver || !testMidiThruBus(*plugin,*receiver)) {
            std::cerr<<"Packaged VST3 MIDI thru to FM Synth failed: "<<error<<'\n';return 77;
        }
        return testLinkedSave(juce::File(juce::String::fromUTF8(argv[2])));
    }
    const bool auditionOnly=argc==4 && juce::String(argv[3])=="--audition";
    juce::XmlElement state("ScaleEditor");
    auto fixtureText=juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString();
    if (argc==4 && juce::String(argv[3])=="--recall") {
        auto fixture=juce::JSON::parse(fixtureText);
        fixture["song"].getDynamicObject()->setProperty("title","Save");
        fixtureText=juce::JSON::toString(fixture);
    }
    state.createNewChildElement("Score")->addTextElement(fixtureText);
    auto* nested=state.createNewChildElement("Instrument");
    nested->setAttribute("state",juce::MemoryBlock{}.toBase64Encoding());
    nested->addChildElement(instrument->createXml().release());
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(state,bytes);
    if (argc==4 && juce::String(argv[3])=="--recall") return testWorkspaceRecall(bytes);
    std::unique_ptr<juce::AudioProcessor> processor(createTestFilter());
    if(!processor->acceptsMidi() || processor->getTotalNumInputChannels()!=0) return 3;
    processor->setStateInformation(bytes.getData(),int(bytes.getSize()));
    processor->setRateAndBufferSizeDetails(48000,256);
    processor->prepareToPlay(48000,256);
    processor->getStateInformation(bytes);
    auto saved=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
    if(!saved || !saved->getChildByName("Instrument")) return 4;
    juce::PluginDescription actual;
    if(!actual.loadFromXml(*saved->getChildByName("Instrument")->getFirstChildElement()) || actual.name!=instrument->name) return 5;
    juce::AudioBuffer<float> block(2,256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,64,(juce::uint8)110),48);
    processor->processBlock(block,midi);
    if(!auditionOnly && (block.getMagnitude(0,0,48)>1.e-6f || block.getMagnitude(0,48,208)<.00001f)) return 6;
    processor->releaseResources();
    processor->setStateInformation(bytes.getData(),int(bytes.getSize()));
    processor->prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    if (argc==3 || auditionOnly) {
        juce::TextButton *instruments=nullptr, *testSound=nullptr;
        for (auto* child:editor->getChildren()) if (auto* b=dynamic_cast<juce::TextButton*>(child)) {
            if (b->getButtonText()=="Instruments...") instruments=b;
            if (b->getButtonText()=="Test sound") testSound=b;
        }
        if (!instruments || !testSound) return 55;
        juce::Component::SafePointer<juce::DocumentWindow> hostedEditor;
        if (!auditionOnly) {
            for (auto* c : editor->getChildren())
                if (auto* button=dynamic_cast<juce::TextButton*>(c); button && button->getButtonText()==instrument->name)
                    button->onClick();
            auto& desktop=juce::Desktop::getInstance();
            for (int i=0;i<desktop.getNumComponents();++i)
                if (auto* window=dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)); window && window->getName()==instrument->name)
                    hostedEditor=window;
            if (!hostedEditor) return 89;
        }
        instruments->onClick();
        juce::Component* picker=nullptr;
        auto& desktop=juce::Desktop::getInstance();
        for (int i=0;i<desktop.getNumComponents();++i)
            if (auto* box=dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)))
                for (auto* child:box->getChildren()) if (child->findChildWithID("matching-instruments")) picker=child;
        if (!picker) return 56;
        auto* list=dynamic_cast<juce::ListBox*>(picker->findChildWithID("matching-instruments"));
        juce::TextEditor* search=nullptr;
        for (auto* c:picker->getChildren()) if (c->getTitle()=="Search instruments") search=dynamic_cast<juce::TextEditor*>(c);
        if (!search || !list) return 57;
        if (list->getModel()->getNumRows()>1) {
            list->selectRow(0);
            if (!search->keyPressed(juce::KeyPress(juce::KeyPress::downKey)) || list->getSelectedRow()!=1
                || !search->keyPressed(juce::KeyPress(juce::KeyPress::upKey)) || list->getSelectedRow()!=0) return 92;
            search->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
            search->setText("__missing_instrument__",false); search->onTextChange();
            search->clear(); search->onTextChange();
            if (list->getSelectedRow()!=1) return 88;
        }
        search->setText("__missing_instrument__",false); search->onTextChange();
        if (list->getModel()->getNumRows()!=0) return 58;
        search->setText(instrument->name+" "+instrument->manufacturerName,false); search->onTextChange();
        if (list->getModel()->getNumRows()<1) return 59;
        juce::ToggleButton* route=nullptr;
        for (auto* child:editor->getChildren())
            if (auto* b=dynamic_cast<juce::ToggleButton*>(child); b && b->getButtonText()=="Preview MIDI out") route=b;
        if (!route || route->getToggleState()) return 61;
        route->setToggleState(true,juce::dontSendNotification); route->onClick();
        auto* loadAndTest=dynamic_cast<juce::TextButton*>(picker->findChildWithID("load-instrument-and-audition"));
        if (!loadAndTest || !loadAndTest->isEnabled()) return 85;
        if (!search->keyPressed(juce::KeyPress(juce::KeyPress::returnKey,juce::ModifierKeys::shiftModifier,0))) return 92;
        bool auditionShownInPicker=false;
        for (auto* child : picker->getChildren())
            if (auto* label=dynamic_cast<juce::Label*>(child); label && label->getText().contains(instrument->name)
                && label->getText().contains("Playing C4")) auditionShownInPicker=true;
        if (!auditionShownInPicker || search->getText()!=instrument->name+" "+instrument->manufacturerName) return 91;
        if (!auditionOnly && !hostedEditor) return 90;
        if (hostedEditor) hostedEditor->closeButtonPressed();
        float peak=0;
        for (int i=0;i<80;++i) {
            midi.clear(); processor->processBlock(block,midi);
            if (!midi.isEmpty()) return 86;
            peak=std::max(peak,block.getMagnitude(0,block.getNumSamples()));
        }
        std::cout<<"Instrument picker / test note: "<<instrument->name<<" peak "<<peak<<"\n";
        if (peak<.001f) return 60;
        auto* load=dynamic_cast<juce::TextButton*>(picker->findChildWithID("load-instrument"));
        if (!load) return 92;
        if (auditionOnly) load->onClick();
        else if (!testRelink(*processor,format,*instrument,*load)) return 95;
        if (!route->getToggleState()) return 87;
        route->setToggleState(false,juce::dontSendNotification); route->onClick();
        if (auditionOnly) { editor.reset(); processor->releaseResources(); return 0; }
        if (!processor->producesMidi() || !route || route->getToggleState() || !midi.isEmpty()) return 61;
        processor->releaseResources(); processor->setRateAndBufferSizeDetails(44100,256);
        processor->prepareToPlay(44100,256);
        route->setToggleState(true,juce::dontSendNotification); route->onClick();
        testSound->onClick();
        unsigned noteOns=0, noteOffs=0;
        for (int pos=0; pos<=21168; pos+=256) {
            midi.clear();
            if (pos==0) midi.addEvent(juce::MidiMessage::noteOn(3,77,juce::uint8(100)),73);
            processor->processBlock(block,midi);
            for (const auto event:midi) {
                const auto msg=event.getMessage();
                if (msg.getChannel()!=1 || msg.getNoteNumber()!=60) return 62;
                if (msg.isNoteOn() && pos+event.samplePosition==0) ++noteOns;
                else if (msg.isNoteOff() && pos+event.samplePosition==21168) ++noteOffs;
                else return 63;
            }
        }
        if (noteOns!=1 || noteOffs!=1) return 64;
        processor->getStateInformation(bytes);
        const auto routed=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
        if (!routed || !routed->getBoolAttribute("previewMidiOut")) return 65;
        testSound->onClick(); midi.clear(); processor->processBlock(block,midi);
        route->setToggleState(false,juce::dontSendNotification); route->onClick();
        midi.clear(); processor->processBlock(block,midi);
        if (midi.getNumEvents()!=1 || !(*midi.begin()).getMessage().isNoteOff()
                || (*midi.begin()).samplePosition!=0) return 66;
        midi.clear(); processor->processBlock(block,midi);
        if (!midi.isEmpty()) return 67;
        if (!testKeyboardAndPreview(*processor,*editor,*testSound,*route)) return 78;
    }
    if (argc == 4) {
        auto* browser = dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
        juce::TextButton* applyScales = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText() == "Apply scales")
                applyScales = button;
        if (!browser || !applyScales || applyScales->isVisible()) return 43;
        if (!editor->isResizable()) return 45;
        editor->setSize(760,580);
        auto narrow = browser->getBounds();
        editor->setSize(1800,950);
        if (narrow!=juce::Rectangle<int>(760,580) || browser->getBounds()!=editor->getLocalBounds()) return 46;
        editor->setSize(920,640);
        processor->getStateInformation(bytes);
        const auto sized=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
        if (!sized || sized->getIntAttribute("editorWidth")!=920 || sized->getIntAttribute("editorHeight")!=640) return 47;
        {
            std::unique_ptr<juce::AudioProcessor> recalled(createTestFilter());
            recalled->setStateInformation(bytes.getData(),int(bytes.getSize()));
            std::unique_ptr<juce::AudioProcessorEditor> reopened(recalled->createEditorAndMakeActive());
            const auto* display=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (display && display->userArea.getWidth()>=944 && display->userArea.getHeight()>=700
                && (reopened->getWidth()!=920 || reopened->getHeight()!=640)) return 48;
        }
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        const auto title = juce::JSON::parse(juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString())["song"]["title"].toString();
        bool pending = false, applying = false, heardPreview = false;
        int inputStage=0, sentStage=-1;
        bool requestedPicker=false, checkedPicker=false;
        int result = 44;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 45000.;
        struct Poll : juce::Timer {
            std::function<void()> poll;
            void timerCallback() override { poll(); }
        } timer;
        timer.poll = [&] {
            if (juce::Time::getMillisecondCounterHiRes() > deadline) {
                juce::MessageManager::getInstance()->stopDispatchLoop(); return;
            }
            if (applying) {
                midi.clear(); block.clear();
                processor->processBlock(block, midi);
                heardPreview |= block.getMagnitude(0, block.getNumSamples()) > 0.0001f;
                processor->getStateInformation(bytes);
                const auto stateXml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), int(bytes.getSize()));
                const auto doc = juce::JSON::parse(stateXml->getChildByName("Score")->getAllSubText());
                const auto directions = doc["tracks"][0]["directions"];
                if (heardPreview && doc["song"]["title"].toString() == title && doc["harmony"]["scales"].size() == 0
                    && directions.size() > 0 && directions[directions.size()-1]["parameters"]["pitchSource"]["type"] == "explicitScale") {
                    result = 0; juce::MessageManager::getInstance()->stopDispatchLoop();
                }
            } else if (!pending && !requestedPicker) {
                pending=true;
                const auto openPicker=juce::String(R"JS((()=>{
                    const b=document.getElementById('studioInstrumentPicker');
                    if(!b || !b.textContent.includes(INSTRUMENT_NAME)) return false;
                    if(getComputedStyle(document.documentElement).getPropertyValue('--bg').trim().toLowerCase()!==STUDIO_BG) return false;
                    const palette=getComputedStyle(document.documentElement);
                    const rgb=key=>{
                        const hex=palette.getPropertyValue(key).trim().slice(1);
                        return 'rgb('+hex.match(/../g).map(v=>parseInt(v,16)).join(', ')+')';
                    };
                    if(!matchMedia('(forced-colors: active)').matches){
                        for(const id of ['genreStyle']){
                            const select=document.getElementById(id), field=getComputedStyle(select);
                            if(field.colorScheme!=='dark' || field.color!==rgb('--control-text')
                               || field.backgroundColor!==rgb('--raised')) return false;
                            for(const option of select.options){
                                const item=getComputedStyle(option);
                                if(item.color!==rgb('--control-text')
                                   || item.backgroundColor!==rgb(option.selected?'--menu-selection':'--bg')) return false;
                            }
                        }
                    }
                    const header=document.querySelector('header'), read=header.querySelector('#hostRead');
                    const radios=[...header.querySelectorAll('#hostLanguage input[type=radio]')];
                    if(!header.contains(b) || !b.offsetHeight || !read?.disabled || !read.offsetHeight
                       || radios.length!==3 || !radios.some(r=>r.checked)
                       || !header.querySelector('#hostDrag:not(:disabled)')?.offsetWidth
                       || header.querySelector('details') || document.getElementById('demos')
                       || document.querySelectorAll('#open, #save, #saveAs').length!==3) return false;
                    if(!matchMedia('(forced-colors: active)').matches && radios.some(r=>
                        getComputedStyle(r.parentElement).color!==rgb('--control-text'))) return false;
                    const sections=[...document.querySelectorAll('.section-title')];
                    if(!sections.length || sections.some(section=>{
                        const name=section.querySelector('.section-name'), range=document.createRange();
                        range.selectNodeContents(name);
                        return Math.abs(range.getBoundingClientRect().left-name.getBoundingClientRect().left)>2
                            || name.getBoundingClientRect().left-section.getBoundingClientRect().left>40;
                    })) return false;
                    if(!document.getElementById('textSource').textContent.includes(window.ScaleAdviserApp.state.doc.song.title)) return false;
                    b.click();return true;
                })())JS").replace("INSTRUMENT_NAME",juce::JSON::toString(juce::var(instrument->name)))
                    .replace("STUDIO_BG",juce::JSON::toString(juce::var("#"+editor->getLookAndFeel()
                        .findColour(juce::ListBox::backgroundColourId).toDisplayString(false).toLowerCase())));
                browser->evaluateJavascript(openPicker,[&](auto evaluation){
                    pending=false;
                    if (const auto* value=evaluation.getResult()) requestedPicker=bool(*value);
                });
            } else if (!pending && !checkedPicker) {
                auto& desktop=juce::Desktop::getInstance();
                for (int i=0;i<desktop.getNumComponents();++i)
                    if (auto* box=dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)))
                        for (auto* child:box->getChildren()) if (child->findChildWithID("matching-instruments")) {
                            box->dismiss();
                            checkedPicker=true;
                            return;
                        }
            } else if (!pending && inputStage<3) {
                if (sentStage!=inputStage) {
                    midi.clear();
                    if (inputStage==0) {
                        for (const auto key : {48,60,64}) midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(100)),0);
                        midi.addEvent(juce::MidiMessage::noteOn(2,60,juce::uint8(100)),0);
                    } else if (inputStage==1) {
                        midi.addEvent(juce::MidiMessage::noteOff(1,48),0);
                        midi.addEvent(juce::MidiMessage::allNotesOff(1),0);
                    } else midi.addEvent(juce::MidiMessage::allSoundOff(2),0);
                    processor->processBlock(block,midi);
                    sentStage=inputStage;
                }
                pending=true;
                browser->evaluateJavascript(R"JS((() => {
                    const keys=document.querySelectorAll('#keyboard g[data-pitch]');
                    if(keys.length!==12 || !document.querySelector('#studioInput'))return -1;
                    return [...keys].reduce((mask,k)=>mask|(k.classList.contains('input-held')?1<<(k.dataset.pitch%12):0),0);
                })())JS",[&](auto evaluated){
                    pending=false;
                    const auto* value=evaluated.getResult();
                    const int expectedMasks[]{17,1,0};
                    if (value && (value->isInt() || value->isDouble()) && int(*value)==expectedMasks[inputStage]) ++inputStage;
                });
            } else if (!pending) {
                pending = true;
                browser->evaluateJavascript(R"JS((() => {
                    const app = window.ScaleAdviserApp;
                    if (!app?.state?.doc?.harmony?.scales?.length || !app.renderScaleMap || !app.renderNoteFit) return '';
                    app.playCompare('A');
                    app.state.doc.harmony.scales = [];
                    document.querySelector('#targets button[data-target]:nth-child(2)').click();
                    document.getElementById('apply').click();
                    document.getElementById('hostCommit').click();
                    return app.state.doc.song.title;
                })())JS", [&](auto evaluated) {
                    pending = false;
                    const auto* value = evaluated.getResult();
                    if (value && value->toString() == title) applying = true;
                });
            }
        };
        timer.startTimer(200);
        juce::MessageManager::getInstance()->runDispatchLoop();
        timer.stopTimer();
        editor.reset(); processor->releaseResources();
        return result;
    }
    juce::ComboBox *choices=nullptr, *genre=nullptr;
    juce::TextButton* ab=nullptr;
    for(auto* c:editor->getChildren()) {
        if(c->getTitle()=="Genre for scale ranking and audition") genre=dynamic_cast<juce::ComboBox*>(c);
        if(c->getTitle()=="Ranked scale candidates") choices=dynamic_cast<juce::ComboBox*>(c);
        if(auto* b=dynamic_cast<juce::TextButton*>(c); b && b->getButtonText()=="A/B with reference") ab=b;
    }
    if(!choices || choices->getNumItems()<2 || !ab) {
        for(auto* c:editor->getChildren()) if(auto* label=dynamic_cast<juce::Label*>(c)) std::cerr<<label->getText()<<"\n";
        return 7;
    }
    if(!genre || genre->getNumItems()<3) return 19;
    auto* flow=editor->findChildWithID("scale-flow-view");
    if (!flow || flow->getTitle()!="Scale Flow" || flow->getDescription().endsWith(" / 0 chords")) return 30;
    for (auto* c:editor->getChildren()) if (auto* tab=dynamic_cast<juce::TextButton*>(c); tab && tab->getButtonText()=="Connections")
        tab->onClick();
    if (flow->getTitle()!="Connections" || flow->getDescription().endsWith(" / 0 chords")) return 31;
    for(auto* child:editor->getChildren()) if(auto* tab=dynamic_cast<juce::TextButton*>(child);tab && tab->getButtonText()=="Review") tab->onClick();
    auto* review=editor->findChildWithID("scale-review-list");
    if(!review || !review->isVisible() || flow->isVisible()) return 42;
    genre->setSelectedId(3,juce::sendNotificationSync);
    if(genre->getSelectedId()!=3 || choices->getNumItems()<2) return 19;
    choices->setSelectedItemIndex(1,juce::sendNotificationSync);
    ab->onClick();
    float peak=0;
    for(int i=0;i<200;++i) {midi.clear();processor->processBlock(block,midi);peak=std::max(peak,block.getMagnitude(0,0,256));}
    if(peak<.001f) return 8;
    juce::ComboBox* chordTrack = nullptr;
    juce::ToggleButton* repeats = nullptr;
    juce::TextButton *apply = nullptr, *undo = nullptr;
    for (auto* child : editor->getChildren()) {
        if (child->getTitle() == "Chord track") chordTrack = dynamic_cast<juce::ComboBox*>(child);
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child); toggle && toggle->getButtonText() == "Include matching progressions")
            repeats = toggle;
        if (auto* button = dynamic_cast<juce::TextButton*>(child)) {
            if (button->getButtonText() == "Assign scale") apply = button;
            if (button->getButtonText() == "Undo apply") undo = button;
        }
    }
    if (!chordTrack || !repeats || !apply || !undo || undo->isEnabled()) return 9;
    const auto scoreText = [&] {
        processor->getStateInformation(bytes);
        auto xml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), int(bytes.getSize()));
        return xml->getChildByName("Score")->getAllSubText();
    };
    const auto before = scoreText();
    const auto scales = [](const juce::String& text) { return juce::JSON::parse(text)["harmony"]["scales"].size(); };
    chordTrack->setSelectedItemIndex(3, juce::sendNotificationSync);
    repeats->setToggleState(true, juce::dontSendNotification);
    repeats->onClick(); apply->onClick();
    if (!undo->isEnabled() || scales(scoreText()) < scales(before) + 2) return 10;
    const auto imagePath = juce::SystemStats::getEnvironmentVariable("OSTF_SCALE_MAP_PNG", {});
    if (imagePath.isNotEmpty()) {
        juce::FileOutputStream image{juce::File(imagePath)};
        if (!image.openedOk() || !image.setPosition(0) || image.truncate().failed()
            || !juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),image)) return 12;
    }
    undo->onClick();
    if (undo->isEnabled() || scoreText() != before) return 11;
    juce::TextButton *redo=nullptr,*automatic=nullptr;
    for(auto* c:editor->getChildren()) if(auto* b=dynamic_cast<juce::TextButton*>(c)) {
        if(b->getButtonText()=="Redo") redo=b;
        if(b->getButtonText()=="Set scales in editor") automatic=b;
    }
    if(!redo || !redo->isEnabled() || !automatic) return 22;
    redo->onClick(); if(!undo->isEnabled() || scales(scoreText())<scales(before)+2)return 23;
    undo->onClick();automatic->onClick();
    if(scales(scoreText())<chordTrack->getNumItems()) return 24;
    undo->onClick(); if(scoreText()!=before) return 25;
    juce::ComboBox *workspace=nullptr,*scope=nullptr;
    for(auto* c:editor->getChildren()) {
        if(c->getTitle()=="Scale workspace") workspace=dynamic_cast<juce::ComboBox*>(c);
        if(c->getTitle()=="Scale assignment scope") scope=dynamic_cast<juce::ComboBox*>(c);
    }
    if(!workspace || !scope) return 26;
    workspace->setSelectedId(3,juce::sendNotificationSync);
    auto* fitList=dynamic_cast<juce::ListBox*>(editor->findChildWithID("scale-note-fit"));
    if(!fitList || !fitList->isVisible() || !fitList->getModel() || fitList->getModel()->getNumRows()==0) return 27;
    scope->setSelectedId(3,juce::sendNotificationSync);
    apply->onClick();
    if(scales(scoreText())<chordTrack->getNumItems()) return 28;
    undo->onClick(); if(scoreText()!=before) return 29;
    editor.reset(); processor->releaseResources();
    return 0;
}
