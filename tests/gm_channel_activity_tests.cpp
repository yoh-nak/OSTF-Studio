#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/ui/InstrumentProgramWatch.h"
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
    std::vector<std::pair<int,int>> changes;
    ostfstudio::InstrumentProgramWatch watcher(*gm, [] { return true; }, [] { return true; },
        [&](int ch, int program) { changes.emplace_back(ch, program); });
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::programChange(2, 17), 0);
    juce::AudioBuffer<float> block(2,256);
    for (int ch : {2,4}) events.addEvent(juce::MidiMessage::noteOn(ch, 60 + ch, juce::uint8(100)), 0);
    gm->processBlock(block, events);
    events.clear();
    juce::Thread::sleep(400);
    gm->processBlock(block, events);
    juce::Timer::callPendingTimersSynchronously();
    if (!shows("EDIT CH 1") || !shows("Playing Ch: 2, 4")) return 1;
    auto meters = editor->createComponentSnapshot(editor->getLocalBounds());
    for (int ch = 0; ch < 16; ++ch) {
        const int x = 16 + (2 * ch + 1) * (meters.getWidth() - 32) / 32;
        int lit = 0;
        for (int y = 0; y < meters.getHeight(); ++y)
            if (meters.getPixelAt(x,y) == juce::Colour(0xff69dfb4)) ++lit;
        if ((lit > 0) != (ch == 1 || ch == 3)) return 6;
    }
    if (!changes.empty()) return 3;
    juce::ComboBox* picker = nullptr;
    for (auto* child : editor->getChildren()) if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) picker = combo;
    if (!picker) return 4;
    picker->setSelectedId(36, juce::sendNotificationSync);
    juce::Thread::sleep(100);
    juce::Timer::callPendingTimersSynchronously();
    if (changes != std::vector<std::pair<int,int>>{{1,35}}) return 5;
    gm->reset();
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    return shows("Playing Ch: -") ? 0 : 2;
}
