#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setRateAndBufferSizeDetails(44100, 256);
    processor->prepareToPlay(44100, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());
    auto* capture = [&]() -> juce::Button* {
        for (auto* c : editor->getChildren())
            if (auto* b = dynamic_cast<juce::Button*>(c); b && b->getButtonText() == "Capture track") return b;
        return nullptr;
    }();
    if (!capture) return 1;
    capture->onClick();
    juce::AudioBuffer<float> block(2,256);
    juce::MidiBuffer midi;
    auto sample = [](int i) {
        double x = 0;
        for (double f : {261.63, 329.63, 392.0}) x += .13 * std::sin(i * juce::MathConstants<double>::twoPi * f / 44100.);
        return float(x);
    };
    constexpr int blocks = 192;
    for (int b = 0; b < blocks; ++b) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i) block.setSample(ch, i, sample(b * 256 + i));
        processor->processBlock(block, midi);
        for (int i = 0; i < 256; ++i) if (block.getSample(0,i) != sample(b * 256 + i)) return 2;
    }
    capture->onClick();
    juce::File folder;
    auto current = [&]() {
        juce::MemoryBlock state;
        processor->getStateInformation(state);
        auto path = juce::MemoryInputStream(state, false).readString();
        return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File{};
    };
    auto awaitResult = [&](juce::File previous) {
        for (int i = 0; i < 300; ++i) {
            juce::Thread::sleep(40);
            juce::Timer::callPendingTimersSynchronously();
            auto next = current();
            if (next != juce::File{} && next != previous) return next;
        }
        return juce::File{};
    };
    folder = awaitResult({});
    if (!folder.isDirectory()) return 3;
    const auto parent = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("OSTF Studio/Stems");
    if (folder.getParentDirectory() != parent) return 4;
    juce::WavAudioFormat wav;
    juce::AudioBuffer<float> sum(2,blocks * 256);
    sum.clear();
    for (auto name : {"Drums","Bass","Other","Vocals"}) {
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(folder.getChildFile(juce::String(name) + ".wav").createInputStream().release(), true));
        if (!reader || reader->lengthInSamples != sum.getNumSamples() || reader->numChannels != 2 || reader->sampleRate != 44100) return 5;
        juce::AudioBuffer<float> stem(2,sum.getNumSamples());
        if (!reader->read(&stem,0,stem.getNumSamples(),0,true,true)) return 6;
        for (int ch = 0; ch < 2; ++ch) sum.addFrom(ch,0,stem,ch,0,stem.getNumSamples());
    }
    for (int i = 0; i < sum.getNumSamples(); ++i) if (std::abs(sum.getSample(0,i) - sample(i)) > .0003f) return 7;
    juce::MidiFile chords;
    juce::FileInputStream input(folder.getChildFile("Chords.mid"));
    if (!input.openedOk() || !chords.readFrom(input) || chords.getNumTracks() != 1) return 8;
    int chordNotes = 0;
    bool major = false;
    for (auto* e : *chords.getTrack(0)) {
        if (e->message.isNoteOn()) ++chordNotes;
        if (e->message.isMetaEvent() && e->message.getMetaEventType() == 6 && e->message.getTextFromTextMetaEvent() == "C") major = true;
    }
    if (!major || chordNotes < 3 || !folder.getChildFile("Drums.mid").existsAsFile()) return 9;
    auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(editor.get());
    juce::StringArray files{folder.getChildFile("Vocals.wav").getFullPathName()};
    if (!target || !target->isInterestedInFileDrag(files)) return 10;
    target->filesDropped(files, 10, 10);
    const auto second = awaitResult(folder);
    if (second.getParentDirectory() != parent || !second.getChildFile("Bass.wav").existsAsFile()) return 11;
    // Delete only the two directories created by this processor during this test.
    second.deleteRecursively();
    folder.deleteRecursively();
    return 0;
}
