#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ostfstudio {
enum class Engine { FM, Analog, Drums, Wavetable };
juce::AudioProcessor* makeStudioInstrument(Engine engine, const juce::String& name);
}
