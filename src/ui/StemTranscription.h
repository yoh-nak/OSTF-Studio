#pragma once
#include "ui/ScoreImportData.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace ostfstudio::scoreimport {
juce::Result detectTriads(juce::AudioFormatReader&, int ppq, List&);
juce::Result transcribe(juce::AudioFormatReader&, ImportSong&, bool boundedSolo = true, int rate = 8000);
void draftDrumHits(juce::AudioFormatReader&, ImportSong&, List&);
}
