#pragma once
#include "ui/Notation.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ostfstudio {
juce::Result printSheet(juce::Component&,const NotationScore&,int,StaffStyle,const MeasureNotes&,bool,const SheetOptions&);
}
