#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3) return 1;
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
    editor.reset(); processor->releaseResources();
    return 0;
}
