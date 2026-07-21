#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../IRetranslatable.h"
#include "../../services/library/TrackImporter.h"

namespace BeatMate::Services::Library {
    class TrackDataProvider;
}

namespace BeatMate::UI {

class FileImportPanel;
class DJImportPanel;

class ImportView : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public BeatMate::UI::IRetranslatable
{
public:
    ImportView();
    explicit ImportView(Services::Library::TrackDataProvider* provider);
    ~ImportView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void importRequested(const std::vector<Services::Library::StagedImportEntry>&,
                                     const Services::Library::FileImportOptions&) {}
        virtual void importCancelRequested() {}
        virtual void analyzeImportedRequested() {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void onImportProgress(int current, int total, const juce::String& fileName);
    void onImportFinished(const Services::Library::FileImportReport& report);

    std::function<void()> onNavigateToLibrary;

private:
    void setupUI();
    void switchTab(bool showDj);

    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextButton> m_filesTabBtn, m_djTabBtn;
    std::unique_ptr<FileImportPanel> m_filePanel;
    std::unique_ptr<DJImportPanel> m_djPanel;
    bool m_showDjTab = false;

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportView)
};

} // namespace BeatMate::UI
