#include <juce_audio_processors/juce_audio_processors.h>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> gm(createPluginFilter());
    gm->setRateAndBufferSizeDetails(48000,256);
    gm->prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(gm->createEditorIfNeeded());
    auto shows = [&](juce::String text) {
        for (auto* c : editor->getChildren())
            if (auto* l = dynamic_cast<juce::Label*>(c); l && l->getText().contains(text)) return true;
        return false;
    };
    juce::MidiBuffer events;
    juce::AudioBuffer<float> block(2,256);
    for (int ch : {2,4}) events.addEvent(juce::MidiMessage::noteOn(ch, 60 + ch, juce::uint8(100)), 0);
    gm->processBlock(block, events);
    events.clear();
    juce::Thread::sleep(400);
    gm->processBlock(block, events);
    juce::Timer::callPendingTimersSynchronously();
    if (!shows("EDIT CH 1") || !shows("Playing Ch: 2, 4")) return 1;
    gm->reset();
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    return shows("Playing Ch: -") ? 0 : 2;
}
