#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "../IRetranslatable.h"
#include "../../services/preparation/EventPlanner.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

// Agenda du DJ : liste de ses dates de prestation (soirées, clubs, mariages…),
class AgendaView : public juce::Component,
                   public BeatMate::UI::IRetranslatable,
                   public juce::ListBoxModel
{
public:
    AgendaView();
    explicit AgendaView(Services::Library::TrackDataProvider* provider);
    ~AgendaView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void retranslateUi() override;

    enum class ViewMode { Month, Week, Year };
    enum class SortMode { DateAsc, DateDesc, NameAsc, NameDesc };

private:
    void setupUI();
    void reload();
    void applyFilter();
    void showSortMenu();
    void setPage(int page);
    void updatePager();
    int pageCount() const;
    int globalRow(int row) const;
    juce::String sortButtonText() const;
    const Services::Preparation::EventPlan* eventAtRow(int row) const;
    void openEditor(bool isNew, int64_t presetDayStart = 0);
    void focusOnEvent(const Services::Preparation::EventPlan& e);
    void deleteSelected();
    void showExportMenu();
    void exportIcs();
    void exportAgendaAs(int kind);   // 1=HTML 2=PDF 3=CSV 4=Word
    void importIcs();
    void setMode(ViewMode m);
    void navigate(int delta);
    void updateMonthLabel();

    void paintMonth(juce::Graphics& g, juce::Rectangle<int> area);
    void paintWeek(juce::Graphics& g, juce::Rectangle<int> area);
    void paintYear(juce::Graphics& g, juce::Rectangle<int> area);
    void mouseInMonth(const juce::MouseEvent& e, juce::Rectangle<int> area);
    void mouseInWeek(const juce::MouseEvent& e, juce::Rectangle<int> area);
    void mouseInYear(const juce::MouseEvent& e, juce::Rectangle<int> area);

    juce::Rectangle<int> calendarBounds() const;
    Services::Preparation::EventPlanner* planner();
    int selectedRow() const;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::vector<Services::Preparation::EventPlan> m_events;
    std::vector<Services::Preparation::EventPlan> m_view;   // m_events après recherche + tri

    std::unique_ptr<juce::ListBox> m_list;
    std::unique_ptr<juce::TextEditor> m_search;
    std::unique_ptr<juce::TextButton> m_sortBtn, m_prevPageBtn, m_nextPageBtn;
    std::unique_ptr<juce::Label> m_pageLabel;
    std::unique_ptr<juce::TextButton> m_newBtn, m_editBtn, m_delBtn, m_icsBtn, m_importBtn;
    std::unique_ptr<juce::TextButton> m_prevMonthBtn, m_nextMonthBtn, m_todayBtn;
    std::unique_ptr<juce::TextButton> m_monthViewBtn, m_weekViewBtn, m_yearViewBtn;
    std::unique_ptr<juce::TextButton> m_reminderBtn;   // rappels multiples (façon Google Agenda)
    void showReminderMenu();
    std::unique_ptr<juce::Label> m_monthLabel;
    std::unique_ptr<juce::FileChooser> m_chooser;

    static constexpr int kPerPage = 20;
    SortMode m_sort = SortMode::DateAsc;
    int m_page = 0;

    ViewMode m_mode = ViewMode::Month;
    int m_dispYear = 2026;
    int m_dispMonth = 0;         // 0-11
    int64_t m_selectedDay = 0;   // début de journée (unix), 0 = aucun

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgendaView)
};

} // namespace BeatMate::UI
