#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ImportStagingStore.h"
#include "../../IRetranslatable.h"

namespace BeatMate::UI {

class ImportStagingList : public juce::Component, public IRetranslatable {
public:
    explicit ImportStagingList(std::vector<StagedFile>& entries);

    void refresh();
    void refreshRow(int index);
    int selectedRow() const;
    void selectRow(int index);

    std::function<void(int)> onToggleRow;
    std::function<void(int)> onRemoveRow;
    std::function<void(int)> onRowSelected;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    struct Columns {
        int check = 30;
        int cover = 52;
        int duration = 56;
        int format = 52;
        int status = 148;
        int remove = 30;
    };

    class Model : public juce::ListBoxModel {
    public:
        explicit Model(ImportStagingList& owner) : m_owner(owner) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void selectedRowsChanged(int lastRowSelected) override;
    private:
        ImportStagingList& m_owner;
    };

    std::vector<StagedFile>& m_entries;
    Columns m_cols;
    std::unique_ptr<Model> m_model;
    std::unique_ptr<juce::ListBox> m_listBox;

    friend class Model;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportStagingList)
};

}
