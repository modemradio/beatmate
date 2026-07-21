#pragma once

#include <functional>
#include <memory>

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace BeatMate::UI {

// Batch-edit form for the library.
class LibraryBatchEditDialog : public juce::Component
{
public:
    LibraryBatchEditDialog();
    ~LibraryBatchEditDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int trackCount = 0;

    std::unique_ptr<juce::ToggleButton> enableTitle, enableArtist, enableAlbum, enableGenre;
    std::unique_ptr<juce::ToggleButton> enableBPM, enableKey, enableEnergy, enableRating;
    std::unique_ptr<juce::ToggleButton> enableColor, enableLabel, enableComment;
    std::unique_ptr<juce::TextEditor>   titleEdit, artistEdit, albumEdit, genreEdit;
    std::unique_ptr<juce::TextEditor>   bpmEdit, keyEdit, commentEdit, labelEdit;
    std::unique_ptr<juce::Slider>       energySlider, ratingSlider;
    std::unique_ptr<juce::TextButton>   colorBtn;
    std::unique_ptr<juce::TextButton>   applyBtn, cancelBtn, closeBtn;
    std::unique_ptr<juce::Label>        headerLabel;

    // Color chosen via ColorPickerDialog (default = black/unset).
    juce::Colour selectedColor { juce::Colours::transparentBlack };

    std::function<void()> onApply;
    std::function<void()> onCancel;

    struct EditResult {
        bool hasTitle   = false; juce::String title;
        bool hasArtist  = false; juce::String artist;
        bool hasAlbum   = false; juce::String album;
        bool hasGenre   = false; juce::String genre;
        bool hasBPM     = false; double       bpm = 0;
        bool hasKey     = false; juce::String key;
        bool hasEnergy  = false; float        energy = 0;
        bool hasRating  = false; int          rating = 0;
        bool hasColor   = false; juce::String color;
        bool hasLabel   = false; juce::String label;
        bool hasComment = false; juce::String comment;
    };
    EditResult getResult() const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBatchEditDialog)
};

} // namespace BeatMate::UI
