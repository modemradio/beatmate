#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class PreparationPlaylistView : public juce::Component,
                                 public juce::ListBoxModel,
                                 private juce::Button::Listener
{
public:
    PreparationPlaylistView();
    explicit PreparationPlaylistView(Services::Library::TrackDataProvider* provider);
    ~PreparationPlaylistView() override = default;

    void addTrack(const Models::Track& track);
    void removeTrackAt(int index);
    void clearList();

    void setDiamondIndex(int index);
    int  getDiamondIndex() const { return m_diamondIndex; }

    int  getNumTracks() const { return static_cast<int>(m_tracks.size()); }
    const std::vector<Models::Track>& getTracks() const { return m_tracks; }

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void preparationTrackPreviewRequested(const juce::String& /*filePath*/) {}
        virtual void preparationDiamondAdvanced(int /*newIndex*/) {}
    };
    void addListener(Listener* l)    { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int  getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g,
                          int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    void deleteKeyPressed(int lastRowSelected) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void buttonClicked(juce::Button* b) override;

    void setupUI();
    void drawDiamond(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour color) const;
    void firePreviewForIndex(int index);
    void advanceDiamond();

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::vector<Models::Track> m_tracks;
    int m_diamondIndex = -1;

    std::unique_ptr<juce::ListBox>    m_list;
    std::unique_ptr<juce::TextButton> m_btnPlay;
    std::unique_ptr<juce::TextButton> m_btnRemove;
    std::unique_ptr<juce::TextButton> m_btnClear;
    std::unique_ptr<juce::TextButton> m_btnAdvance;
    std::unique_ptr<juce::Label>      m_headerLabel;
    std::unique_ptr<juce::Label>      m_countLabel;

    juce::ListenerList<Listener> m_listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreparationPlaylistView)
};

}
