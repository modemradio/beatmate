#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <cstdint>

#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

// Detachable "mirror" of the library shown as a small floating popup window.
class LibraryMirrorWindow : public juce::DocumentWindow {
public:
    LibraryMirrorWindow(Services::Library::TrackDataProvider* provider,
                        std::function<void(int64_t trackId)> onTrackChosen);
    ~LibraryMirrorWindow() override;

    void closeButtonPressed() override;

    // Re-read the library from the provider (call when tracks change).
    void refresh();

    std::function<void()> onClosed;

private:
    class Content;
    std::unique_ptr<Content> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryMirrorWindow)
};

} // namespace BeatMate::UI
