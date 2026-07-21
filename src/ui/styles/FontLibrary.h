#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace BeatMate::UI::Fonts {

enum class Weight { Regular, Medium, SemiBold, Bold };

juce::Typeface::Ptr ui(Weight weight);
juce::Typeface::Ptr mono(Weight weight);

juce::FontOptions uiFont(float height, Weight weight = Weight::Regular);
juce::FontOptions monoFont(float height, Weight weight = Weight::Regular);

}
