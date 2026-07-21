#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace BeatMate::UI::Widgets {

class YouTubePreview {
public:
    static void showForTrack(const juce::String& artist, const juce::String& title);
    static void stopPlayback();
    static void close();
};

} // namespace BeatMate::UI::Widgets
