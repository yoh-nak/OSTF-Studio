#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int testWorkspaceRecall(const juce::MemoryBlock& initial) {
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::WebBrowserComponent* browser=nullptr;
    auto open=[&](const juce::MemoryBlock& bytes) {
        editor.reset();
        processor.reset(createPluginFilter());
        processor->setStateInformation(bytes.getData(),int(bytes.getSize()));
        editor.reset(processor->createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        browser=dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
        return browser!=nullptr;
    };
    if (!open(initial)) return 50;
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
            juce::MemoryBlock bytes;
            processor->getStateInformation(bytes);
            const auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
            auto* draft=xml->getChildByName("Workspace");
            if (!draft || !juce::JSON::parse(draft->getAllSubText()).hasProperty("text")) return;
            if (!juce::JSON::parse(xml->getChildByName("Score")->getAllSubText())["harmony"]["scales"].size()) return;
            if (open(bytes)) ++stage;
        } else if (stage==2) {
            checkPage(R"JS((()=>{
                const app=window.ScaleAdviserApp,s=app?.state;
                return !!(s?.textDirty && s.view==='text' && s.doc.harmony.scales.length===0 &&
                    s.selected===s.doc.harmony.chords[1].id && s.reference && s.reference===s.scale &&
                    document.querySelector('#textArea').value==='\u672a\u78ba\u5b9a <draft> &');
            })())JS");
        } else if (stage==3) {
            for (auto* child:editor->getChildren())
                if (auto* b=dynamic_cast<juce::TextButton*>(child); b && b->getButtonText()=="Apply scales") {
                    b->onClick(); ++stage; break;
                }
        } else if (stage==4) {
            juce::MemoryBlock bytes;
            processor->getStateInformation(bytes);
            const auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
            if (juce::JSON::parse(xml->getChildByName("Score")->getAllSubText())["harmony"]["scales"].size()) return;
            auto* draft=xml->getChildByName("Workspace");
            if (!draft || juce::JSON::parse(draft->getAllSubText()).hasProperty("score")) return;
            draft->setAttribute("source","stale");
            juce::AudioProcessor::copyXmlToBinary(*xml,bytes);
            std::unique_ptr<juce::AudioProcessor> stale(createPluginFilter());
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
        } else if (stage==5) {
            browser=dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
            if (browser) checkPage("Boolean(window.ScaleAdviserApp?.state.doc?.harmony.scales.length && !window.ScaleAdviserApp.state.textDirty)");
        } else {
            result=0;
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
    const juce::File file{juce::String::fromUTF8(argv[1])};
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types,file.getFullPathName());
    if(types.size()!=1 || !types[0]->isInstrument) return 2;
    juce::XmlElement state("ScaleEditor");
    state.createNewChildElement("Score")->addTextElement(juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString());
    auto* nested=state.createNewChildElement("Instrument");
    nested->setAttribute("state",juce::MemoryBlock{}.toBase64Encoding());
    nested->addChildElement(types[0]->createXml().release());
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(state,bytes);
    if (argc==4 && juce::String(argv[3])=="--recall") return testWorkspaceRecall(bytes);
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    if(!processor->acceptsMidi() || processor->getTotalNumInputChannels()!=0) return 3;
    processor->setStateInformation(bytes.getData(),int(bytes.getSize()));
    processor->setRateAndBufferSizeDetails(48000,256);
    processor->prepareToPlay(48000,256);
    processor->getStateInformation(bytes);
    auto saved=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
    if(!saved || !saved->getChildByName("Instrument")) return 4;
    juce::PluginDescription actual;
    if(!actual.loadFromXml(*saved->getChildByName("Instrument")->getFirstChildElement()) || actual.name!=types[0]->name) return 5;
    juce::AudioBuffer<float> block(2,256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,64,(juce::uint8)110),48);
    processor->processBlock(block,midi);
    if(block.getMagnitude(0,0,48)>1.e-6f || block.getMagnitude(0,48,208)<.00001f) return 6;
    processor->releaseResources();
    processor->setStateInformation(bytes.getData(),int(bytes.getSize()));
    processor->prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    if (argc == 4) {
        auto* browser = dynamic_cast<juce::WebBrowserComponent*>(editor->findChildWithID("scale-full-workspace"));
        juce::TextButton* applyScales = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText() == "Apply scales")
                applyScales = button;
        if (!browser || !applyScales) return 43;
        if (!editor->isResizable()) return 45;
        editor->setSize(760,580);
        auto narrow = browser->getBounds();
        editor->setSize(1800,950);
        if (browser->getWidth()-narrow.getWidth()!=1040 || browser->getHeight()-narrow.getHeight()!=370) return 46;
        editor->setSize(920,640);
        processor->getStateInformation(bytes);
        const auto sized=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),int(bytes.getSize()));
        if (!sized || sized->getIntAttribute("editorWidth")!=920 || sized->getIntAttribute("editorHeight")!=640) return 47;
        {
            std::unique_ptr<juce::AudioProcessor> recalled(createPluginFilter());
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
                if (heardPreview && doc["song"]["title"].toString() == title && doc["harmony"]["scales"].size() == 0) {
                    result = 0; juce::MessageManager::getInstance()->stopDispatchLoop();
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
                    return app.state.doc.song.title;
                })())JS", [&](auto evaluated) {
                    pending = false;
                    const auto* value = evaluated.getResult();
                    if (value && value->toString() == title) { applying = true; applyScales->onClick(); }
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
        if(b->getButtonText()=="Auto-set song") automatic=b;
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
