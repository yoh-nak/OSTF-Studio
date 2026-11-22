#include <juce_audio_processors/juce_audio_processors.h>

juce::AudioProcessor* makeStudioEffect(const char* type, const char* name);

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return makeStudioEffect(OSTF_EFFECT_TYPE, JucePlugin_Name);
}
