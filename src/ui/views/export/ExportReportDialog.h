#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../services/export/BatchExportService.h"

namespace BeatMate::UI {

class ExportReportDialog : public juce::Component {
public:
    explicit ExportReportDialog(Services::Export::BatchExportReport report);

    static void show(const Services::Export::BatchExportReport& report);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class RowModel : public juce::ListBoxModel {
    public:
        explicit RowModel(ExportReportDialog& owner) : m_owner(owner) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    private:
        ExportReportDialog& m_owner;
    };

    Services::Export::BatchExportReport m_report;
    std::unique_ptr<RowModel> m_model;
    std::unique_ptr<juce::ListBox> m_listBox;
    std::unique_ptr<juce::TextButton> m_openFolderBtn, m_closeBtn;

    friend class RowModel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportReportDialog)
};

} // namespace BeatMate::UI
