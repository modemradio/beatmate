#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../IRetranslatable.h"
#include "../../../services/djsoftware/CollectionSyncService.h"

namespace BeatMate::UI {

class DJPlaylistPicker : public juce::Component, public IRetranslatable {
public:
    DJPlaylistPicker();

    void setPlaylists(std::vector<Services::DJSoftware::ExternalPlaylistDescriptor> playlists);
    std::vector<std::string> selectedIds() const;
    bool wholeCollection() const;
    int selectedTrackCount() const;

    std::function<void()> onSelectionChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    class Model : public juce::ListBoxModel {
    public:
        explicit Model(DJPlaylistPicker& owner) : m_owner(owner) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    private:
        DJPlaylistPicker& m_owner;
    };

    void rebuildFiltered();
    void notifySelection();

    std::vector<Services::DJSoftware::ExternalPlaylistDescriptor> m_all;
    std::vector<int> m_filtered;
    std::set<std::string> m_checked;

    std::unique_ptr<juce::ToggleButton> m_wholeCollectionCheck;
    std::unique_ptr<juce::TextEditor> m_searchEditor;
    std::unique_ptr<Model> m_model;
    std::unique_ptr<juce::ListBox> m_listBox;

    friend class Model;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DJPlaylistPicker)
};

} // namespace BeatMate::UI
