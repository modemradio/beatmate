#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ImportStagingStore.h"
#include "ImportStagingList.h"
#include "ImportInspectorPanel.h"
#include "ImportSummaryCard.h"
#include "../../IRetranslatable.h"
#include "../../../services/library/TrackImporter.h"

namespace BeatMate::UI {

class FileImportPanel : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public IRetranslatable {
public:
    FileImportPanel();
    ~FileImportPanel() override;

    void addPaths(const juce::StringArray& absolutePaths);

    void setImportProgress(int current, int total, const juce::String& fileName);
    void showImportReport(const Services::Library::FileImportReport& report);

    std::function<void(const std::vector<Services::Library::StagedImportEntry>&,
                       const Services::Library::FileImportOptions&)> onImportRequested;
    std::function<void()> onCancelRequested;
    std::function<void()> onNavigateToLibrary;
    std::function<void()> onAnalyzeImported;

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { m_dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { m_dragOver = false; repaint(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    struct LibrarySnapshot {
        std::mutex mutex;
        int generation = -1;
        std::vector<Models::Track> tracks;
    };

    void browseFiles();
    void browseFolder();
    void browseLibraryFolder();
    void startImport();
    void clearStaging();
    void toggleAll();
    void scheduleMetaJob(const juce::String& path);
    void updateCountLabel();
    void updateImportingState(bool importing);
    int indexOfPath(const juce::String& path) const;
    juce::Rectangle<int> dropZoneBounds() const;

    std::vector<StagedFile> m_entries;
    int m_generation = 0;
    std::shared_ptr<LibrarySnapshot> m_snapshot = std::make_shared<LibrarySnapshot>();

    std::unique_ptr<ImportStagingList> m_list;
    std::unique_ptr<ImportInspectorPanel> m_inspector;
    std::unique_ptr<ImportSummaryCard> m_summary;

    std::unique_ptr<juce::TextButton> m_browseFilesBtn, m_browseFolderBtn;
    std::unique_ptr<juce::ToggleButton> m_autoAnalyzeCheck, m_detectDuplicatesCheck;
    std::unique_ptr<juce::ToggleButton> m_copyToLibraryCheck, m_readTagsCheck;
    std::unique_ptr<juce::Label> m_libraryFolderLabel;
    std::unique_ptr<juce::TextButton> m_libraryFolderBtn;
    std::unique_ptr<juce::Label> m_countLabel;
    std::unique_ptr<juce::TextButton> m_toggleAllBtn, m_clearBtn, m_cancelBtn, m_importBtn;
    std::unique_ptr<juce::Label> m_statusLabel;

    bool m_dragOver = false;
    bool m_importing = false;
    double m_progress = 0.0;

    juce::ThreadPool m_metaPool{2};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileImportPanel)
};

} // namespace BeatMate::UI
