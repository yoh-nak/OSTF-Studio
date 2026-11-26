#include <juce_audio_processors/juce_audio_processors.h>

juce::AudioProcessor* createOstfRack(bool instrument);
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {return createOstfRack(JucePlugin_IsSynth != 0);}
