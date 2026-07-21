#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "../../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class TagEditorPanel : public juce::Component,
                       public juce::ListBoxModel
{
public:
    explicit TagEditorPanel(Services::Library::TrackDataProvider* provider);
    ~TagEditorPanel() override = default;

    // preChecked : la Bibliotheque a transmis un choix explicite (cases cochees
    // ou selection) — ces titres arrivent coches. Sinon rien n'est coche.
    void setTracks(const std::vector<int64_t>& trackIds, bool preChecked = false);

    std::function<void()> onClose;
    std::function<void()> onTracksChanged;
    std::function<void(std::vector<int64_t>)> onAnalyzeRequested;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

private:
    struct Item {
        Models::Track track;
        juce::String tagsCsv;    // tags perso, séparés par des virgules
        bool dirty = false;
        bool included = false;
    };

    void setupUI();
    void loadFieldsFromSelection();
    std::vector<int> selectedRows() const;
    std::vector<int> includedRows() const;
    void updateBatchUi();
    int m_lastToggledRow = -1;
    void writeTags();
    void renameFromMask();
    void tagsFromFilename();
    void applyCase(int mode);        // 0=Title Case 1=MAJUSCULES 2=minuscules 3=Première lettre
    void cleanupTags();
    void importCover();
    void exportCover();
    void removeCover();
    void searchCoversOnline();
    void searchTagsOnline();
    void applyCoverToRows(const std::vector<uint8_t>& bytes, const std::vector<int>& rows);
    static void invalidateCoverCache(int64_t trackId);
    void refreshCoverPreview();
    void showCaseMenu();
    void showCleanMenu();
    void showCoverMenu();
    static juce::String applyMask(const juce::String& mask, const Models::Track& t);

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::vector<Item> m_items;

    std::unique_ptr<juce::ListBox> m_fileList;
    std::unique_ptr<juce::TextButton> m_backBtn, m_writeBtn, m_renameBtn, m_fromNameBtn,
                                      m_caseBtn, m_cleanBtn, m_coverBtn, m_onlineBtn, m_selectAllBtn;
    std::unique_ptr<juce::TextEditor> m_maskEdit;

    struct Field {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::TextEditor> editor;
        bool multiple = false;   // valeurs différentes dans la sélection
        bool edited = false;     // modifié par l'utilisateur
    };
    enum FieldId { FTitle = 0, FArtist, FAlbum, FGenre, FYear, FBpm, FKey, FEnergy, FLabel, FMood, FComment, FMyTags, FCount };
    Field m_fields[FCount];

    juce::Image m_coverPreview;
    bool m_coverLoaded = false;
    std::unique_ptr<juce::FileChooser> m_chooser;

    class BatchProgressComponent;
    juce::Component::SafePointer<BatchProgressComponent> m_progressPopup;
    std::shared_ptr<std::atomic<bool>> m_batchCancel;
    std::function<void()> m_afterTagsChain;
    void openBatchProgress(const juce::String& title, std::shared_ptr<std::atomic<bool>> cancelFlag);
    void updateBatchProgress(int done, int total, const juce::String& line);
    void closeBatchProgress();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TagEditorPanel)
};

} // namespace BeatMate::UI
