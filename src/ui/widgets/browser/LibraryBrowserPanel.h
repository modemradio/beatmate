#pragma once
#include <memory>
#include <vector>
#include <string>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include "../../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class LibraryBrowserPanel : public juce::Component,
                            public juce::DragAndDropContainer,
                            public juce::Timer
{
public:
    explicit LibraryBrowserPanel(Services::Library::TrackDataProvider* provider);
    ~LibraryBrowserPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void trackDoubleClicked(const Models::Track& track) {}
        virtual void addTrackRequested(const Models::Track& track) {}
        virtual void tracksDropped(const std::vector<Models::Track>& tracks) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void refreshResults();
    void clearFilters();
    void setProvider(Services::Library::TrackDataProvider* p);
    Services::Library::TrackDataProvider* getProvider() const noexcept { return m_provider; }
    int getResultCount() const { return static_cast<int>(m_results.size()); }
    const std::vector<Models::Track>& getResults() const { return m_results; }
    std::vector<Models::Track> getSelectedTracks() const;

    void timerCallback() override;

private:
    void setupUI();
    void performSearch();
    void populateGenreCombo();
    void populateKeyCombo();

    class TrackTableModel : public juce::TableListBoxModel
    {
    public:
        std::vector<Models::Track>* tracks = nullptr;
        LibraryBrowserPanel* owner = nullptr;

        int getNumRows() override { return tracks ? static_cast<int>(tracks->size()) : 0; }
        void paintRowBackground(juce::Graphics& g, int row, int w, int h, bool selected) override;
        void paintCell(juce::Graphics& g, int row, int columnId, int w, int h, bool selected) override;
        void cellDoubleClicked(int row, int columnId, const juce::MouseEvent&) override;
        void sortOrderChanged(int newSortColumnId, bool isForwards) override;

        juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    };

    class DragSourceTable : public juce::TableListBox
    {
    public:
        DragSourceTable(const juce::String& name, juce::TableListBoxModel* model)
            : juce::TableListBox(name, model) {}

        void mouseDrag(const juce::MouseEvent& e) override;
    };

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::vector<Models::Track> m_results;

    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_resultCountLabel;

    std::unique_ptr<juce::TextEditor> m_searchEdit;

    std::unique_ptr<juce::Label> m_bpmLabel;
    std::unique_ptr<juce::Slider> m_bpmMin;
    std::unique_ptr<juce::Slider> m_bpmMax;

    std::unique_ptr<juce::ComboBox> m_keyCombo;
    std::unique_ptr<juce::ComboBox> m_genreCombo;

    std::unique_ptr<juce::Label> m_energyLabel;
    std::unique_ptr<juce::Slider> m_energyMin;
    std::unique_ptr<juce::Slider> m_energyMax;
    std::unique_ptr<juce::ComboBox> m_ratingCombo;

    std::unique_ptr<juce::TextButton> m_resetBtn;

    std::unique_ptr<DragSourceTable> m_table;
    std::unique_ptr<TrackTableModel> m_tableModel;

    int m_sortColumnId = 1;
    bool m_sortForward = true;

    juce::ListenerList<Listener> m_listeners;

    enum ColumnIds {
        Col_Title = 1,
        Col_Artist = 2,
        Col_BPM = 3,
        Col_Key = 4,
        Col_Energy = 5,
        Col_Duration = 6,
        Col_Genre = 7,
        Col_Rating = 8
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowserPanel)
};

} // namespace BeatMate::UI
