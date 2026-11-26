#include "StudioInstrument.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    using namespace ostfstudio;
    return makeStudioInstrument(OSTF_ENGINE, JucePlugin_Name);
}
