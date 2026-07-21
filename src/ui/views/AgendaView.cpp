#include "AgendaView.h"
#include "../styles/ColorPalette.h"
#include "../utils/ViewPrefs.h"
#include "../../services/preparation/IcsExport.h"
#include "../../services/preparation/AgendaExport.h"
#include "../../services/config/I18n.h"
#include "../../app/ServiceLocator.h"
#include <algorithm>
#include <map>
#include <functional>
#include <cctype>

extern BeatMate::ServiceLocator* g_serviceLocator;

namespace BeatMate::UI {

using Services::Preparation::EventPlan;

namespace {

juce::String fmtDate(int64_t unixSeconds)
{
    if (unixSeconds <= 0) return "-";
    juce::Time t(unixSeconds * (int64_t) 1000);
    static const char* jours[] = { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" };
    return juce::String(jours[t.getDayOfWeek() % 7]) + " "
         + juce::String(t.getDayOfMonth()).paddedLeft('0', 2) + "/"
         + juce::String(t.getMonth() + 1).paddedLeft('0', 2) + "/"
         + juce::String(t.getYear());
}

juce::String fmtTime(int64_t unixSeconds)
{
    if (unixSeconds <= 0) return "";
    juce::Time t(unixSeconds * (int64_t) 1000);
    return juce::String(t.getHours()).paddedLeft('0', 2) + ":"
         + juce::String(t.getMinutes()).paddedLeft('0', 2);
}

int64_t dayStartUnix(int year, int month0, int day)
{
    juce::Time t(year, month0, day, 0, 0, 0, 0, true);
    return (int64_t) (t.toMilliseconds() / 1000);
}

int64_t eventDayStart(int64_t startUnix)
{
    juce::Time t(startUnix * (int64_t) 1000);
    return dayStartUnix(t.getYear(), t.getMonth(), t.getDayOfMonth());
}

const char* moisFr(int m0)
{
    static const char* m[] = { "Janvier", "F\xc3\xa9vrier", "Mars", "Avril", "Mai", "Juin",
                               "Juillet", "Ao\xc3\xbbt", "Septembre", "Octobre", "Novembre", "D\xc3\xa9""cembre" };
    return (m0 >= 0 && m0 < 12) ? m[m0] : "";
}

int daysInMonthOf(int year, int month0)
{
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month0 == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return (month0 >= 0 && month0 < 12) ? d[month0] : 30;
}

int64_t parseDateTime(const juce::String& date, const juce::String& time)
{
    auto d = juce::StringArray::fromTokens(date.trim(), "-/", "");
    if (d.size() < 3) return 0;
    const int y = d[0].getIntValue();
    const int mo = d[1].getIntValue();
    const int day = d[2].getIntValue();
    int h = 22, mi = 0;
    auto tt = juce::StringArray::fromTokens(time.trim(), ":hH", "");
    if (tt.size() >= 1 && tt[0].isNotEmpty()) h = tt[0].getIntValue();
    if (tt.size() >= 2) mi = tt[1].getIntValue();
    if (y < 1970 || mo < 1 || mo > 12 || day < 1 || day > 31) return 0;
    juce::Time t(y, mo - 1, day, h, mi, 0, 0, true);
    return (int64_t) (t.toMilliseconds() / 1000);
}

juce::Colour statusColour(const std::string& s)
{
    if (s == "cancelled") return Colors::error();
    if (s == "planned")   return Colors::warning();
    if (s == "completed") return Colors::textMuted();
    return Colors::success();
}

juce::String statusLabel(const std::string& s)
{
    if (s == "cancelled") return juce::String::fromUTF8("Annul\xc3\xa9");
    if (s == "planned")   return juce::String::fromUTF8("\xc3\x80 confirmer");
    if (s == "completed") return juce::String::fromUTF8("Pass\xc3\xa9");
    return juce::String::fromUTF8("Confirm\xc3\xa9");
}

class ReminderPickerComponent : public juce::Component
{
public:
    static constexpr int kCount = 8;
    static constexpr int kDelays[kCount] = { 15, 30, 60, 120, 1440, 10080, 43200, 86400 };

    ReminderPickerComponent()
    {
        static const char* labels[kCount] = { "15 min", "30 min", "1 h", "2 h",
                                              "1 jour", "1 sem.", "1 mois", "2 mois" };
        for (int i = 0; i < kCount; ++i)
        {
            m_checks[i] = std::make_unique<juce::ToggleButton>(juce::String::fromUTF8(labels[i]));
            m_checks[i]->setColour(juce::ToggleButton::textColourId, Colors::textPrimary());
            m_checks[i]->setColour(juce::ToggleButton::tickColourId, Colors::primary());
            addAndMakeVisible(*m_checks[i]);
        }
        setSize(430, 76);
    }

    void setFromCsv(const juce::String& csv)
    {
        auto parts = juce::StringArray::fromTokens(csv, ",", "");
        for (int i = 0; i < kCount; ++i)
            m_checks[i]->setToggleState(parts.contains(juce::String(kDelays[i])),
                                        juce::dontSendNotification);
    }

    juce::String getCsv() const
    {
        juce::StringArray out;
        for (int i = 0; i < kCount; ++i)
            if (m_checks[i]->getToggleState()) out.add(juce::String(kDelays[i]));
        return out.joinIntoString(",");
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(12.5f));
        g.drawText(juce::String::fromUTF8("\xf0\x9f\x94\x94 Rappels avant la prestation :"),
                   0, 0, getWidth(), 16, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int colW = getWidth() / 4;
        for (int i = 0; i < kCount; ++i)
            m_checks[i]->setBounds((i % 4) * colW, 18 + (i / 4) * 28, colW - 4, 26);
    }

private:
    std::unique_ptr<juce::ToggleButton> m_checks[kCount];
};

constexpr int ReminderPickerComponent::kDelays[];

juce::Colour eventColour(const EventPlan& e)
{
    if (e.status == "cancelled") return juce::Colour(0xff6b7280);
    static const juce::Colour palette[] = {
        juce::Colour(0xff6366f1), juce::Colour(0xff8b5cf6), juce::Colour(0xffec4899),
        juce::Colour(0xffef4444), juce::Colour(0xfff59e0b), juce::Colour(0xff10b981),
        juce::Colour(0xff06b6d4), juce::Colour(0xff3b82f6), juce::Colour(0xff14b8a6),
        juce::Colour(0xfff97316), juce::Colour(0xffa855f7), juce::Colour(0xff22c55e)
    };
    std::string key = ! e.style.empty() ? e.style : (! e.venue.empty() ? e.venue : e.name);
    for (auto& ch : key) ch = (char) std::tolower((unsigned char) ch);
    std::size_t h = std::hash<std::string>{}(key);
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

}

AgendaView::AgendaView() { setupUI(); }
AgendaView::AgendaView(Services::Library::TrackDataProvider* provider) : m_provider(provider) { setupUI(); }

Services::Preparation::EventPlanner* AgendaView::planner()
{
    return g_serviceLocator ? g_serviceLocator->tryGet<Services::Preparation::EventPlanner>() : nullptr;
}

void AgendaView::setupUI()
{
    auto mkBtn = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& text, juce::Colour c) {
        b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, c);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
    };
    mkBtn(m_newBtn,    juce::String::fromUTF8("+ Nouvelle date"), Colors::primary());
    mkBtn(m_editBtn,   juce::String::fromUTF8("Modifier"),        Colors::bgLighter());
    mkBtn(m_delBtn,    juce::String::fromUTF8("Supprimer"),       Colors::bgLighter());
    mkBtn(m_icsBtn,    juce::String::fromUTF8("Exporter \xe2\x96\xbe"), Colors::primary());
    mkBtn(m_importBtn, juce::String::fromUTF8("Importer .ics"),   Colors::bgLighter());

    m_newBtn->onClick    = [this] { openEditor(true); };
    m_editBtn->onClick   = [this] { openEditor(false); };
    m_delBtn->onClick    = [this] { deleteSelected(); };
    m_icsBtn->onClick    = [this] { showExportMenu(); };
    m_importBtn->onClick = [this] { importIcs(); };

    m_prevMonthBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x97\x80"));
    m_nextMonthBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x96\xb6"));
    m_todayBtn     = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Aujourd'hui"));
    for (auto* b : { m_prevMonthBtn.get(), m_nextMonthBtn.get(), m_todayBtn.get() })
    {
        b->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
    }
    m_prevMonthBtn->onClick = [this] { navigate(-1); };
    m_nextMonthBtn->onClick = [this] { navigate(1); };
    m_todayBtn->onClick = [this]
    {
        juce::Time n = juce::Time::getCurrentTime();
        m_dispYear = n.getYear(); m_dispMonth = n.getMonth();
        m_selectedDay = dayStartUnix(n.getYear(), n.getMonth(), n.getDayOfMonth());
        updateMonthLabel(); repaint();
    };

    m_monthViewBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Mois"));
    m_weekViewBtn  = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Semaine"));
    m_yearViewBtn  = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Ann\xc3\xa9""e"));
    for (auto* b : { m_monthViewBtn.get(), m_weekViewBtn.get(), m_yearViewBtn.get() })
    {
        b->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        b->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
        b->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
        addAndMakeVisible(*b);
    }
    m_monthViewBtn->onClick = [this] { setMode(ViewMode::Month); };
    m_weekViewBtn->onClick  = [this] { setMode(ViewMode::Week); };
    m_yearViewBtn->onClick  = [this] { setMode(ViewMode::Year); };

    m_reminderBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xf0\x9f\x94\x94 Rappels \xe2\x96\xbe"));
    m_reminderBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_reminderBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_reminderBtn->setTooltip(juce::String::fromUTF8(
        "Notifications en haut de BeatMate avant chaque prestation \xe2\x80\x94 "
        "cochez plusieurs d\xc3\xa9lais (15 min \xc3\xa0 2 mois)"));
    m_reminderBtn->onClick = [this] { showReminderMenu(); };
    addAndMakeVisible(*m_reminderBtn);

    m_monthLabel = std::make_unique<juce::Label>();
    m_monthLabel->setJustificationType(juce::Justification::centred);
    m_monthLabel->setFont(juce::Font(17.0f, juce::Font::bold));
    m_monthLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_monthLabel);

    juce::Time now = juce::Time::getCurrentTime();
    m_dispYear = now.getYear();
    m_dispMonth = now.getMonth();
    updateMonthLabel();

    m_search = std::make_unique<juce::TextEditor>();
    m_search->setTextToShowWhenEmpty(
        juce::String::fromUTF8("Rechercher par nom, lieu ou date (ex. 2026-08, ao\xc3\xbbt, Ibiza)"),
        Colors::textSecondary());
    m_search->setColour(juce::TextEditor::backgroundColourId, Colors::bgCard());
    m_search->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_search->setColour(juce::TextEditor::outlineColourId, Colors::glassBorder());
    m_search->onTextChange = [this] { m_page = 0; applyFilter(); };
    addAndMakeVisible(*m_search);

    mkBtn(m_sortBtn, sortButtonText(), Colors::bgLighter());
    m_sortBtn->onClick = [this] { showSortMenu(); };

    m_prevPageBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x97\x80"));
    m_nextPageBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x96\xb6"));
    for (auto* b : { m_prevPageBtn.get(), m_nextPageBtn.get() })
    {
        b->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
    }
    m_prevPageBtn->onClick = [this] { setPage(m_page - 1); };
    m_nextPageBtn->onClick = [this] { setPage(m_page + 1); };

    m_pageLabel = std::make_unique<juce::Label>();
    m_pageLabel->setJustificationType(juce::Justification::centred);
    m_pageLabel->setFont(juce::Font(12.5f));
    m_pageLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_pageLabel);

    m_list = std::make_unique<juce::ListBox>("agendaList", this);
    m_list->setRowHeight(58);
    m_list->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_list->setMultipleSelectionEnabled(true);
    addAndMakeVisible(*m_list);

    setMode(ViewMode::Month);
    reload();
}

juce::String AgendaView::sortButtonText() const
{
    juce::String s(juce::String::fromUTF8("Trier : "));
    switch (m_sort)
    {
        case SortMode::DateDesc: s << juce::String::fromUTF8("date \xe2\x86\x93"); break;
        case SortMode::NameAsc:  s << juce::String::fromUTF8("nom A\xe2\x86\x92Z"); break;
        case SortMode::NameDesc: s << juce::String::fromUTF8("nom Z\xe2\x86\x92""A"); break;
        case SortMode::DateAsc:
        default:                 s << juce::String::fromUTF8("date \xe2\x86\x91"); break;
    }
    return s + juce::String::fromUTF8("  \xe2\x96\xbe");
}

void AgendaView::showSortMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Trier les dates"));
    m.addItem(1, juce::String::fromUTF8("Date croissante (la plus ancienne d'abord)"), true, m_sort == SortMode::DateAsc);
    m.addItem(2, juce::String::fromUTF8("Date d\xc3\xa9""croissante (la plus r\xc3\xa9""cente d'abord)"), true, m_sort == SortMode::DateDesc);
    m.addSeparator();
    m.addItem(3, juce::String::fromUTF8("Nom A \xe2\x86\x92 Z"), true, m_sort == SortMode::NameAsc);
    m.addItem(4, juce::String::fromUTF8("Nom Z \xe2\x86\x92 A"), true, m_sort == SortMode::NameDesc);

    juce::Component::SafePointer<AgendaView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_sortBtn.get()),
                    [safe](int r)
                    {
                        if (r <= 0 || safe == nullptr) return;
                        safe->m_sort = r == 2 ? SortMode::DateDesc
                                     : r == 3 ? SortMode::NameAsc
                                     : r == 4 ? SortMode::NameDesc
                                              : SortMode::DateAsc;
                        if (safe->m_sortBtn) safe->m_sortBtn->setButtonText(safe->sortButtonText());
                        safe->m_page = 0;
                        safe->applyFilter();
                    });
}

void AgendaView::navigate(int delta)
{
    switch (m_mode)
    {
        case ViewMode::Week:
        {
            const int64_t anchor = (m_selectedDay > 0 ? m_selectedDay
                                    : dayStartUnix(m_dispYear, m_dispMonth, 1)) + (int64_t) delta * 7 * 24 * 3600;
            juce::Time t(anchor * (int64_t) 1000);
            m_dispYear = t.getYear(); m_dispMonth = t.getMonth();
            m_selectedDay = eventDayStart(anchor);
            break;
        }
        case ViewMode::Year:
            m_dispYear += delta;
            break;
        case ViewMode::Month:
        default:
            m_dispMonth += delta;
            while (m_dispMonth < 0)  { m_dispMonth += 12; --m_dispYear; }
            while (m_dispMonth > 11) { m_dispMonth -= 12; ++m_dispYear; }
            break;
    }
    updateMonthLabel();
    repaint();
}

void AgendaView::setMode(ViewMode m)
{
    m_mode = m;
    for (auto* b : { m_monthViewBtn.get(), m_weekViewBtn.get(), m_yearViewBtn.get() })
        if (b) b->setToggleState(false, juce::dontSendNotification);
    if (m == ViewMode::Month && m_monthViewBtn) m_monthViewBtn->setToggleState(true, juce::dontSendNotification);
    if (m == ViewMode::Week  && m_weekViewBtn)  m_weekViewBtn->setToggleState(true, juce::dontSendNotification);
    if (m == ViewMode::Year  && m_yearViewBtn)  m_yearViewBtn->setToggleState(true, juce::dontSendNotification);
    for (auto* b : { m_monthViewBtn.get(), m_weekViewBtn.get(), m_yearViewBtn.get() })
        if (b) b->setColour(juce::TextButton::buttonColourId,
                            b->getToggleState() ? Colors::primary() : Colors::bgLighter());
    updateMonthLabel();
    resized();
    repaint();
}

void AgendaView::updateMonthLabel()
{
    if (! m_monthLabel) return;
    juce::String txt;
    if (m_mode == ViewMode::Year)
        txt = juce::String(m_dispYear);
    else if (m_mode == ViewMode::Week)
    {
        const int64_t anchor = m_selectedDay > 0 ? m_selectedDay : dayStartUnix(m_dispYear, m_dispMonth, 1);
        juce::Time a(anchor * (int64_t) 1000);
        const int dow = (a.getDayOfWeek() + 6) % 7;
        const int64_t weekStart = anchor - (int64_t) dow * 24 * 3600;
        const int64_t weekEnd   = weekStart + (int64_t) 6 * 24 * 3600;
        juce::Time ws(weekStart * (int64_t) 1000), we(weekEnd * (int64_t) 1000);
        txt = juce::String(ws.getDayOfMonth()) + " " + juce::String::fromUTF8(moisFr(ws.getMonth())).substring(0, 3)
            + " \xe2\x80\x93 " + juce::String(we.getDayOfMonth()) + " " + juce::String::fromUTF8(moisFr(we.getMonth())).substring(0, 3)
            + " " + juce::String(we.getYear());
    }
    else
        txt = juce::String::fromUTF8(moisFr(m_dispMonth)) + " " + juce::String(m_dispYear);
    m_monthLabel->setText(txt, juce::dontSendNotification);
}

void AgendaView::reload()
{
    if (auto* p = planner())
    {
        m_events = p->getEvents();
        std::sort(m_events.begin(), m_events.end(),
                  [](const EventPlan& a, const EventPlan& b) { return a.startTime < b.startTime; });
    }
    applyFilter();
}

void AgendaView::applyFilter()
{
    juce::StringArray terms;
    if (m_search)
    {
        const juce::String q = m_search->getText().trim();
        if (q.isNotEmpty()) terms.addTokens(q, " ", "");
        terms.removeEmptyStrings();
    }

    m_view.clear();
    m_view.reserve(m_events.size());
    for (const auto& e : m_events)
    {
        bool keep = true;
        if (! terms.isEmpty())
        {
            juce::String hay;
            hay << juce::String::fromUTF8(e.name.c_str())  << " "
                << juce::String::fromUTF8(e.venue.c_str()) << " "
                << juce::String::fromUTF8(e.city.c_str())  << " "
                << juce::String::fromUTF8(e.style.c_str()) << " "
                << fmtDate(e.startTime) << " "
                << juce::Time(e.startTime * (int64_t) 1000).formatted("%Y-%m-%d");
            for (const auto& t : terms)
                if (! hay.containsIgnoreCase(t)) { keep = false; break; }
        }
        if (keep) m_view.push_back(e);
    }

    const SortMode mode = m_sort;
    std::sort(m_view.begin(), m_view.end(),
              [mode](const EventPlan& a, const EventPlan& b)
              {
                  switch (mode)
                  {
                      case SortMode::DateDesc: return a.startTime > b.startTime;
                      case SortMode::NameAsc:
                          return juce::String::fromUTF8(a.name.c_str())
                                 .compareIgnoreCase(juce::String::fromUTF8(b.name.c_str())) < 0;
                      case SortMode::NameDesc:
                          return juce::String::fromUTF8(a.name.c_str())
                                 .compareIgnoreCase(juce::String::fromUTF8(b.name.c_str())) > 0;
                      case SortMode::DateAsc:
                      default: return a.startTime < b.startTime;
                  }
              });

    if (m_page >= pageCount()) m_page = pageCount() - 1;
    if (m_page < 0) m_page = 0;
    if (m_list) { m_list->deselectAllRows(); m_list->updateContent(); }
    updatePager();
    repaint();
}

int AgendaView::pageCount() const
{
    const int n = (int) m_view.size();
    return n <= 0 ? 1 : (n + kPerPage - 1) / kPerPage;
}

int AgendaView::globalRow(int row) const { return m_page * kPerPage + row; }

const Services::Preparation::EventPlan* AgendaView::eventAtRow(int row) const
{
    if (row < 0) return nullptr;
    const int g = globalRow(row);
    if (g < 0 || g >= (int) m_view.size()) return nullptr;
    return &m_view[(size_t) g];
}

void AgendaView::setPage(int page)
{
    const int p = juce::jlimit(0, pageCount() - 1, page);
    if (p == m_page) return;
    m_page = p;
    if (m_list) { m_list->deselectAllRows(); m_list->updateContent(); m_list->scrollToEnsureRowIsOnscreen(0); }
    updatePager();
}

void AgendaView::updatePager()
{
    const int total = (int) m_view.size();
    if (m_pageLabel)
    {
        juce::String t;
        if (total == 0)
            t = m_events.empty() ? juce::String::fromUTF8("Aucune date")
                                 : juce::String::fromUTF8("Aucun r\xc3\xa9sultat");
        else
            t << juce::String::fromUTF8("Page ") << (m_page + 1) << " / " << pageCount()
              << juce::String::fromUTF8("   \xe2\x80\xa2   ") << total
              << juce::String::fromUTF8(total > 1 ? " dates" : " date");
        m_pageLabel->setText(t, juce::dontSendNotification);
    }
    if (m_prevPageBtn) m_prevPageBtn->setEnabled(m_page > 0);
    if (m_nextPageBtn) m_nextPageBtn->setEnabled(m_page + 1 < pageCount());
}

int AgendaView::selectedRow() const { return m_list ? m_list->getSelectedRow() : -1; }

int AgendaView::getNumRows()
{
    const int total = (int) m_view.size();
    const int start = m_page * kPerPage;
    return juce::jlimit(0, kPerPage, total - start);
}

void AgendaView::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto* ev = eventAtRow(row);
    if (ev == nullptr) return;
    const auto& e = *ev;

    g.setColour(selected ? Colors::primary().withAlpha(0.18f) : Colors::bgCard());
    g.fillRoundedRectangle(4.0f, 3.0f, (float) w - 8.0f, (float) h - 6.0f, 8.0f);

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText(fmtDate(e.startTime), 16, 8, 150, 18, juce::Justification::centredLeft);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.0f));
    juce::String heure = fmtTime(e.startTime);
    if (e.endTime > e.startTime) heure << " \xe2\x80\x93 " << fmtTime(e.endTime);
    g.drawText(heure, 16, 30, 150, 16, juce::Justification::centredLeft);

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(15.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8(e.name.c_str()), 180, 8, w - 340, 20, juce::Justification::centredLeft);
    juce::String lieu = juce::String::fromUTF8(e.venue.c_str());
    if (! e.city.empty()) lieu << (lieu.isNotEmpty() ? " \xc2\xb7 " : "") << juce::String::fromUTF8(e.city.c_str());
    if (e.fee > 0.0) lieu << "   \xe2\x80\xa2   " << juce::String(e.fee, 0) << " " << juce::String::fromUTF8(e.currency.c_str());
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.5f));
    g.drawText(lieu, 180, 30, w - 340, 18, juce::Justification::centredLeft);

    const auto sc = statusColour(e.status);
    juce::Rectangle<float> pill((float) w - 150.0f, (float) h * 0.5f - 11.0f, 130.0f, 22.0f);
    g.setColour(sc.withAlpha(0.16f));
    g.fillRoundedRectangle(pill, 11.0f);
    g.setColour(sc);
    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.drawText(statusLabel(e.status), pill.toNearestInt(), juce::Justification::centred);
}

void AgendaView::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0) openEditor(false);
}

void AgendaView::openEditor(bool isNew, int64_t presetDayStart)
{
    auto* p = planner();
    if (! p) return;

    EventPlan base;
    if (! isNew)
    {
        const auto* ev = eventAtRow(selectedRow());
        if (ev == nullptr) return;
        base = *ev;
    }
    else
    {
        const int64_t day = presetDayStart > 0 ? presetDayStart
                          : (m_selectedDay > 0 ? m_selectedDay
                          : eventDayStart((int64_t) (juce::Time::getCurrentTime().toMilliseconds() / 1000)));
        base.startTime = day + 22 * 3600;
        base.endTime   = day + 24 * 3600 + 4 * 3600;
    }

    auto* win = new juce::AlertWindow(
        isNew ? juce::String::fromUTF8("Nouvelle date") : juce::String::fromUTF8("Modifier la date"),
        juce::String::fromUTF8("Renseignez votre prestation :"),
        juce::MessageBoxIconType::NoIcon, this);

    win->addTextEditor("name",  juce::String::fromUTF8(base.name.c_str()),  juce::String::fromUTF8("Titre / soir\xc3\xa9""e"));
    win->addTextEditor("date",  base.startTime > 0 ? juce::Time(base.startTime * 1000).formatted("%Y-%m-%d") : juce::String(),
                       juce::String::fromUTF8("Date (AAAA-MM-JJ)"));
    win->addTextEditor("start", base.startTime > 0 ? fmtTime(base.startTime) : "22:00", juce::String::fromUTF8("D\xc3\xa9""but (HH:MM)"));
    win->addTextEditor("end",   base.endTime > 0 ? fmtTime(base.endTime) : "04:00", juce::String::fromUTF8("Fin (HH:MM)"));
    win->addTextEditor("venue", juce::String::fromUTF8(base.venue.c_str()), juce::String::fromUTF8("Lieu / club"));
    win->addTextEditor("city",  juce::String::fromUTF8(base.city.c_str()),  juce::String::fromUTF8("Ville"));
    win->addTextEditor("fee",   base.fee > 0.0 ? juce::String(base.fee, 0) : juce::String(), juce::String::fromUTF8("Cachet (€)"));
    win->addTextEditor("style", juce::String::fromUTF8(base.style.c_str()), juce::String::fromUTF8("Style / genre"));

    juce::StringArray statuts { juce::String::fromUTF8("\xc3\x80 confirmer"), juce::String::fromUTF8("Confirm\xc3\xa9"),
                                juce::String::fromUTF8("Pass\xc3\xa9"), juce::String::fromUTF8("Annul\xc3\xa9") };
    win->addComboBox("status", statuts, juce::String::fromUTF8("Statut"));
    if (auto* cb = win->getComboBoxComponent("status"))
    {
        const std::string s = base.status.empty() ? "confirmed" : base.status;
        cb->setSelectedItemIndex(s == "planned" ? 0 : s == "completed" ? 2 : s == "cancelled" ? 3 : 1,
                                 juce::dontSendNotification);
    }

    win->addTextEditor("notes", juce::String::fromUTF8(base.notes.c_str()), juce::String::fromUTF8("Notes"));

    auto reminderPicker = std::make_shared<ReminderPickerComponent>();
    {
        juce::String csv = juce::String::fromUTF8(base.reminders.c_str());
        if (csv.isEmpty())
        {
            csv = juce::String(Prefs::getString("agenda.reminders", ""));
            if (csv.isEmpty())
            {
                const int legacy = Prefs::getInt("agenda.reminderMinutes", 60);
                if (legacy > 0) csv = juce::String(legacy);
            }
        }
        reminderPicker->setFromCsv(csv);
    }
    win->addCustomComponent(reminderPicker.get());

    win->addButton(juce::String::fromUTF8("Enregistrer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    win->addButton(juce::String::fromUTF8("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<AgendaView> safe(this);
    win->enterModalState(true, juce::ModalCallbackFunction::create([safe, win, base, isNew, reminderPicker](int result)
    {
        std::unique_ptr<juce::AlertWindow> w(win);
        if (result != 1 || safe == nullptr) return;
        auto* p2 = safe->planner();
        if (! p2) return;

        EventPlan e = base;
        e.reminders = reminderPicker->getCsv().toStdString();
        e.name  = w->getTextEditorContents("name").trim().toStdString();
        e.venue = w->getTextEditorContents("venue").trim().toStdString();
        e.city  = w->getTextEditorContents("city").trim().toStdString();
        e.style = w->getTextEditorContents("style").trim().toStdString();
        e.notes = w->getTextEditorContents("notes").toStdString();
        e.fee   = w->getTextEditorContents("fee").getDoubleValue();
        if (auto* cb = w->getComboBoxComponent("status"))
        {
            switch (cb->getSelectedItemIndex())
            {
                case 0: e.status = "planned";   break;
                case 2: e.status = "completed"; break;
                case 3: e.status = "cancelled"; break;
                default: e.status = "confirmed"; break;
            }
        }
        e.startTime = parseDateTime(w->getTextEditorContents("date"), w->getTextEditorContents("start"));
        e.endTime   = parseDateTime(w->getTextEditorContents("date"), w->getTextEditorContents("end"));
        if (e.endTime > 0 && e.endTime <= e.startTime) e.endTime += 24 * 3600;

        if (e.name.empty() || e.startTime <= 0) return;

        if (isNew) { e.id = p2->createEvent(e); }
        else       { p2->updateEvent(e); }
        safe->reload();
        safe->focusOnEvent(e);
    }), false);
}

void AgendaView::showReminderMenu()
{
    static const int kDelays[] = { 15, 30, 60, 120, 1440, 10080, 43200, 86400 };
    static const char* kLabels[] = {
        "15 min avant", "30 min avant", "1 h avant", "2 h avant",
        "1 jour avant", "1 semaine avant", "1 mois avant", "2 mois avant"
    };

    juce::String csv = juce::String(Prefs::getString("agenda.reminders", ""));
    if (csv.isEmpty())
    {
        const int legacy = Prefs::getInt("agenda.reminderMinutes", 60);
        csv = legacy > 0 ? juce::String(legacy) : juce::String();
    }
    auto active = juce::StringArray::fromTokens(csv, ",", "");
    active.removeEmptyStrings();

    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Rappels avant chaque prestation"));
    for (int i = 0; i < 8; ++i)
        m.addItem(1 + i, juce::String::fromUTF8(kLabels[i]), true,
                  active.contains(juce::String(kDelays[i])));
    m.addSeparator();
    m.addItem(100, juce::String::fromUTF8("Tout d\xc3\xa9sactiver"), ! active.isEmpty());

    juce::Component::SafePointer<AgendaView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_reminderBtn.get()),
        [safe, active](int r) mutable
        {
            if (safe == nullptr || r == 0) return;
            if (r == 100)
            {
                Prefs::setString("agenda.reminders", "");
                Prefs::setInt("agenda.reminderMinutes", 0);
                return;
            }
            if (r >= 1 && r <= 8)
            {
                const juce::String v(kDelays[r - 1]);
                if (active.contains(v)) active.removeString(v);
                else active.add(v);
                Prefs::setString("agenda.reminders", active.joinIntoString(",").toStdString());
                safe->showReminderMenu();
            }
        });
}

void AgendaView::focusOnEvent(const Services::Preparation::EventPlan& e)
{
    juce::Time t(e.startTime * (int64_t) 1000);
    m_dispYear = t.getYear();
    m_dispMonth = t.getMonth();
    m_selectedDay = eventDayStart(e.startTime);
    updateMonthLabel();
    for (int i = 0; i < (int) m_view.size(); ++i)
    {
        if (m_view[(size_t) i].id == e.id || eventDayStart(m_view[(size_t) i].startTime) == m_selectedDay)
        {
            setPage(i / kPerPage);
            if (m_list) m_list->selectRow(i - m_page * kPerPage);
            break;
        }
    }
    repaint();
}

void AgendaView::deleteSelected()
{
    if (! m_list) return;
    const auto rows = m_list->getSelectedRows();

    std::vector<int64_t> ids;
    juce::String firstName;
    for (int i = 0; i < rows.size(); ++i)
    {
        if (const auto* e = eventAtRow(rows[i]))
        {
            if (ids.empty()) firstName = juce::String::fromUTF8(e->name.c_str());
            ids.push_back(e->id);
        }
    }
    if (ids.empty()) return;

    const int n = (int) ids.size();
    juce::String msg;
    if (n == 1)
        msg = juce::String::fromUTF8("Supprimer \xc2\xab ") + firstName + juce::String::fromUTF8(" \xc2\xbb ?");
    else
        msg = juce::String::fromUTF8("Supprimer ") + juce::String(n)
            + juce::String::fromUTF8(" dates s\xc3\xa9lectionn\xc3\xa9""es ? Cette action est d\xc3\xa9""finitive.");

    juce::Component::SafePointer<AgendaView> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        n == 1 ? juce::String::fromUTF8("Supprimer la date") : juce::String::fromUTF8("Supprimer les dates"),
        msg,
        juce::String::fromUTF8("Supprimer"), juce::String::fromUTF8("Annuler"), nullptr,
        juce::ModalCallbackFunction::create([safe, ids](int result)
        {
            if (result != 1 || safe == nullptr) return;
            if (auto* p = safe->planner())
                for (const auto& id : ids)
                    p->deleteEvent(id);
            safe->reload();
        }));
}

void AgendaView::showExportMenu()
{
    if (m_events.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Agenda vide"),
            juce::String::fromUTF8("Ajoutez au moins une date avant d'exporter."));
        return;
    }
    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Exporter l'agenda"));
    m.addItem(1, juce::String::fromUTF8("Calendrier .ics  \xe2\x80\x94  Google, Outlook, Apple"));
    m.addSeparator();
    m.addItem(2, juce::String::fromUTF8("Page HTML  \xe2\x80\x94  imprimable"));
    m.addItem(3, juce::String::fromUTF8("Document PDF"));
    m.addItem(4, juce::String::fromUTF8("Document Word  (.docx)"));
    m.addItem(5, juce::String::fromUTF8("Tableur CSV  \xe2\x80\x94  Excel"));

    juce::Component::SafePointer<AgendaView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_icsBtn.get()),
        [safe](int r)
        {
            if (safe == nullptr || r == 0) return;
            switch (r)
            {
                case 1: safe->exportIcs();       break;
                case 2: safe->exportAgendaAs(1); break;
                case 3: safe->exportAgendaAs(2); break;
                case 4: safe->exportAgendaAs(4); break;
                case 5: safe->exportAgendaAs(3); break;
                default: break;
            }
        });
}

void AgendaView::exportAgendaAs(int kind)
{
    const char* filter; juce::String def;
    switch (kind)
    {
        case 1: filter = "*.html"; def = "agenda-dj.html"; break;
        case 2: filter = "*.pdf";  def = "agenda-dj.pdf";  break;
        case 3: filter = "*.csv";  def = "agenda-dj.csv";  break;
        case 4: filter = "*.docx"; def = "agenda-dj.docx"; break;
        default: return;
    }
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter l'agenda"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(def), filter);
    auto events = m_events;
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [events, kind](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            std::string data;
            switch (kind)
            {
                case 1: data = Services::Preparation::buildAgendaHtml(events); break;
                case 2: data = Services::Preparation::buildAgendaPdf(events);  break;
                case 3: data = Services::Preparation::buildAgendaCsv(events);  break;
                case 4: data = Services::Preparation::buildAgendaDocx(events); break;
                default: return;
            }
            f.replaceWithData(data.data(), data.size());
            f.startAsProcess();
        });
}

void AgendaView::exportIcs()
{
    if (m_events.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Agenda vide"),
            juce::String::fromUTF8("Ajoutez au moins une date avant d'exporter."));
        return;
    }
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter l'agenda (.ics)"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("agenda-dj.ics"),
        "*.ics");
    auto events = m_events;
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [events](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f.replaceWithText(Services::Preparation::buildIcsCalendar(events));
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("Agenda export\xc3\xa9"),
                juce::String::fromUTF8("Importez ce fichier .ics dans Google Agenda "
                                       "(Param\xc3\xa8tres \xe2\x80\xba Importer & exporter), "
                                       "ou hebergez-le pour un abonnement automatique."));
        });
}

void AgendaView::importIcs()
{
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Importer un agenda (.ics)"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.ics");
    juce::Component::SafePointer<AgendaView> safe(this);
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode,
        [safe](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File() || safe == nullptr) return;
            auto* p = safe->planner();
            if (! p) return;
            auto imported = Services::Preparation::parseIcsCalendar(f.loadFileAsString());
            int n = 0;
            for (auto& e : imported) { e.id = 0; p->createEvent(e); ++n; }
            safe->reload();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("Import termin\xc3\xa9"),
                juce::String(n) + juce::String::fromUTF8(" date(s) import\xc3\xa9""e(s)."));
        });
}

juce::Rectangle<int> AgendaView::calendarBounds() const
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(60);
    area.removeFromTop(36);
    area.removeFromTop(8);
    area.removeFromTop(34);
    area.removeFromTop(8);
    const int listH = juce::jmax(220, area.getHeight() * 32 / 100);
    area.removeFromBottom(listH + 12);
    return area;
}

void AgendaView::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDark());
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("Agenda"), 24, 18, 400, 30, juce::Justification::centredLeft);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(13.0f));
    g.drawText(juce::String::fromUTF8("Vos dates de prestation \xe2\x80\x94 mois \xc2\xb7 semaine \xc2\xb7 ann\xc3\xa9""e, export .ics"),
               24, 48, 700, 20, juce::Justification::centredLeft);

    auto cal = calendarBounds();
    g.setColour(Colors::bgCard());
    g.fillRoundedRectangle(cal.toFloat(), 12.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(cal.toFloat(), 12.0f, 1.0f);

    switch (m_mode)
    {
        case ViewMode::Week: paintWeek(g, cal.reduced(14)); break;
        case ViewMode::Year: paintYear(g, cal.reduced(12)); break;
        case ViewMode::Month:
        default:             paintMonth(g, cal.reduced(14)); break;
    }
}

void AgendaView::paintMonth(juce::Graphics& g, juce::Rectangle<int> area)
{
    static const char* jj[] = { "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim" };
    const int cols = 7;
    const int cellW = area.getWidth() / cols;
    auto head = area.removeFromTop(24);
    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    for (int c = 0; c < cols; ++c)
        g.drawText(jj[c], head.getX() + c * cellW, head.getY(), cellW, head.getHeight(), juce::Justification::centred);

    std::map<int, std::vector<const EventPlan*>> byDay;
    for (const auto& e : m_events)
    {
        juce::Time t(e.startTime * (int64_t) 1000);
        if (t.getYear() == m_dispYear && t.getMonth() == m_dispMonth)
            byDay[t.getDayOfMonth()].push_back(&e);
    }

    const int daysInMonth = daysInMonthOf(m_dispYear, m_dispMonth);
    const int firstDow = (juce::Time(m_dispYear, m_dispMonth, 1, 0, 0).getDayOfWeek() + 6) % 7;
    juce::Time today = juce::Time::getCurrentTime();

    const int rows = 6;
    const int cellH = area.getHeight() / rows;
    for (int d = 1; d <= daysInMonth; ++d)
    {
        const int idx = firstDow + d - 1;
        const int r = idx / cols, c = idx % cols;
        juce::Rectangle<int> cell(area.getX() + c * cellW, area.getY() + r * cellH, cellW, cellH);
        auto inner = cell.reduced(2);

        const int64_t ds = dayStartUnix(m_dispYear, m_dispMonth, d);
        const bool isToday = (today.getYear() == m_dispYear && today.getMonth() == m_dispMonth && today.getDayOfMonth() == d);
        const bool isSel = (m_selectedDay == ds);

        g.setColour(isSel ? Colors::primary().withAlpha(0.14f) : Colors::glassWhite());
        g.fillRoundedRectangle(inner.toFloat(), 6.0f);
        if (isSel)   { g.setColour(Colors::primary());          g.drawRoundedRectangle(inner.toFloat(), 6.0f, 1.6f); }
        else         { g.setColour(Colors::glassBorder());      g.drawRoundedRectangle(inner.toFloat(), 6.0f, 0.8f); }

        auto numStrip = inner.reduced(6, 4).removeFromTop(18);
        if (isToday)
        {
            g.setColour(Colors::primary());
            g.fillEllipse((float) numStrip.getX() - 2.0f, (float) numStrip.getY() - 1.0f, 20.0f, 20.0f);
            g.setColour(juce::Colours::white);
        }
        else g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.drawText(juce::String(d), numStrip.withWidth(20), juce::Justification::centred);

        auto it = byDay.find(d);
        if (it == byDay.end()) continue;
        auto evs = it->second;
        std::sort(evs.begin(), evs.end(), [](const EventPlan* a, const EventPlan* b){ return a->startTime < b->startTime; });

        auto chipsArea = inner.reduced(4, 2);
        chipsArea.removeFromTop(20);
        const int chipH = 15;
        const int maxChips = juce::jmax(0, chipsArea.getHeight() / (chipH + 2));
        int shown = 0;
        for (auto* e : evs)
        {
            if (shown >= maxChips) break;
            auto chip = chipsArea.removeFromTop(chipH); chipsArea.removeFromTop(2);
            g.setColour(eventColour(*e).withAlpha(e->status == "cancelled" ? 0.45f : 0.92f));
            g.fillRoundedRectangle(chip.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            juce::String label = fmtTime(e->startTime) + " " + juce::String::fromUTF8(e->name.c_str());
            g.drawText(label, chip.reduced(4, 0), juce::Justification::centredLeft, true);
            ++shown;
        }
        if ((int) evs.size() > shown)
        {
            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText("+" + juce::String((int) evs.size() - shown),
                       chipsArea.removeFromTop(12), juce::Justification::centredLeft);
        }
    }
}

void AgendaView::paintWeek(juce::Graphics& g, juce::Rectangle<int> area)
{
    static const char* jj[] = { "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim" };
    const int64_t anchor = m_selectedDay > 0 ? m_selectedDay : dayStartUnix(m_dispYear, m_dispMonth, 1);
    juce::Time a(anchor * (int64_t) 1000);
    const int dow = (a.getDayOfWeek() + 6) % 7;
    const int64_t weekStart = anchor - (int64_t) dow * 24 * 3600;
    juce::Time today = juce::Time::getCurrentTime();
    const int64_t todayDs = dayStartUnix(today.getYear(), today.getMonth(), today.getDayOfMonth());

    const int cols = 7;
    const int colW = area.getWidth() / cols;
    const int headH = 42;

    for (int c = 0; c < cols; ++c)
    {
        const int64_t dayDs = weekStart + (int64_t) c * 24 * 3600;
        juce::Time dt(dayDs * (int64_t) 1000);
        juce::Rectangle<int> col(area.getX() + c * colW, area.getY(), colW, area.getHeight());
        auto colIn = col.reduced(3);

        const bool isToday = (dayDs == todayDs);
        const bool isSel = (dayDs == m_selectedDay);
        if (isSel) { g.setColour(Colors::primary().withAlpha(0.08f)); g.fillRoundedRectangle(colIn.toFloat(), 7.0f); }

        auto header = colIn.removeFromTop(headH);
        g.setColour(isToday ? Colors::primary() : Colors::textMuted());
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawText(juce::String(jj[c]), header.removeFromTop(16), juce::Justification::centred);
        if (isToday)
        {
            auto circ = header.removeFromTop(22).withSizeKeepingCentre(22, 22);
            g.setColour(Colors::primary()); g.fillEllipse(circ.toFloat());
            g.setColour(juce::Colours::white);
        }
        else g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(juce::String(dt.getDayOfMonth()), header, juce::Justification::centred);

        g.setColour(Colors::glassBorder());
        if (c > 0) g.fillRect(col.getX(), area.getY() + headH, 1, area.getHeight() - headH);

        std::vector<const EventPlan*> evs;
        for (const auto& e : m_events)
            if (eventDayStart(e.startTime) == dayDs) evs.push_back(&e);
        std::sort(evs.begin(), evs.end(), [](const EventPlan* a2, const EventPlan* b2){ return a2->startTime < b2->startTime; });

        auto lane = colIn.reduced(2, 4);
        const int blockH = 46;
        for (auto* e : evs)
        {
            if (lane.getHeight() < 24) break;
            auto block = lane.removeFromTop(juce::jmin(blockH, lane.getHeight())); lane.removeFromTop(4);
            const auto col2 = eventColour(*e);
            g.setColour(col2.withAlpha(e->status == "cancelled" ? 0.25f : 0.22f));
            g.fillRoundedRectangle(block.toFloat(), 5.0f);
            g.setColour(col2); g.fillRoundedRectangle((float) block.getX(), (float) block.getY(), 3.0f, (float) block.getHeight(), 1.5f);
            auto txt = block.reduced(8, 4);
            g.setColour(Colors::textPrimary());
            g.setFont(juce::Font(11.5f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8(e->name.c_str()), txt.removeFromTop(15), juce::Justification::centredLeft, true);
            juce::String sub = fmtTime(e->startTime);
            if (e->endTime > e->startTime) sub << " \xe2\x80\x93 " << fmtTime(e->endTime);
            if (! e->venue.empty()) sub << "  \xc2\xb7  " << juce::String::fromUTF8(e->venue.c_str());
            g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(10.5f));
            g.drawText(sub, txt, juce::Justification::centredLeft, true);
        }
    }
}

void AgendaView::paintYear(juce::Graphics& g, juce::Rectangle<int> area)
{
    std::map<int, std::map<int, juce::Colour>> evByMonthDay;
    for (const auto& e : m_events)
    {
        juce::Time t(e.startTime * (int64_t) 1000);
        if (t.getYear() == m_dispYear) evByMonthDay[t.getMonth()][t.getDayOfMonth()] = eventColour(e);
    }
    juce::Time today = juce::Time::getCurrentTime();

    const int mcols = 4, mrows = 3;
    const int mw = area.getWidth() / mcols;
    const int mh = area.getHeight() / mrows;
    for (int m0 = 0; m0 < 12; ++m0)
    {
        const int mc = m0 % mcols, mr = m0 / mcols;
        juce::Rectangle<int> mcell(area.getX() + mc * mw, area.getY() + mr * mh, mw, mh);
        auto in = mcell.reduced(7);

        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8(moisFr(m0)), in.removeFromTop(18), juce::Justification::centredLeft);

        const int cols = 7;
        const int cw = in.getWidth() / cols;
        auto dh = in.removeFromTop(12);
        g.setColour(Colors::textDim());
        g.setFont(juce::Font(8.5f));
        static const char* d1[] = { "L", "M", "M", "J", "V", "S", "D" };
        for (int c = 0; c < cols; ++c)
            g.drawText(d1[c], dh.getX() + c * cw, dh.getY(), cw, dh.getHeight(), juce::Justification::centred);

        const int dim = daysInMonthOf(m_dispYear, m0);
        const int firstDow = (juce::Time(m_dispYear, m0, 1, 0, 0).getDayOfWeek() + 6) % 7;
        const int rows = 6;
        const int ch = juce::jmax(10, in.getHeight() / rows);
        for (int d = 1; d <= dim; ++d)
        {
            const int idx = firstDow + d - 1;
            const int r = idx / cols, c = idx % cols;
            juce::Rectangle<int> dc(in.getX() + c * cw, in.getY() + r * ch, cw, ch);
            const bool isToday = (today.getYear() == m_dispYear && today.getMonth() == m0 && today.getDayOfMonth() == d);
            auto ev = evByMonthDay.find(m0);
            const bool hasEv = ev != evByMonthDay.end() && ev->second.count(d) > 0;

            if (hasEv)
            {
                g.setColour(ev->second.at(d));
                g.fillEllipse(dc.withSizeKeepingCentre(juce::jmin(dc.getWidth(), ch) - 2, juce::jmin(dc.getWidth(), ch) - 2).toFloat());
                g.setColour(juce::Colours::white);
            }
            else if (isToday)
            {
                g.setColour(Colors::primary());
                g.drawEllipse(dc.withSizeKeepingCentre(juce::jmin(dc.getWidth(), ch) - 2, juce::jmin(dc.getWidth(), ch) - 2).toFloat(), 1.2f);
                g.setColour(Colors::primary());
            }
            else g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(9.0f, hasEv ? juce::Font::bold : juce::Font::plain));
            g.drawText(juce::String(d), dc, juce::Justification::centred);
        }
    }
}

void AgendaView::mouseDown(const juce::MouseEvent& e)
{
    auto cal = calendarBounds();
    if (! cal.contains(e.getPosition())) return;
    switch (m_mode)
    {
        case ViewMode::Week: mouseInWeek(e, cal.reduced(14)); break;
        case ViewMode::Year: mouseInYear(e, cal.reduced(12)); break;
        case ViewMode::Month:
        default:             mouseInMonth(e, cal.reduced(14)); break;
    }
}

void AgendaView::mouseInMonth(const juce::MouseEvent& e, juce::Rectangle<int> area)
{
    area.removeFromTop(24);
    const int cols = 7, rows = 6;
    const int cellW = area.getWidth() / cols;
    const int cellH = area.getHeight() / rows;
    const int c = (e.x - area.getX()) / juce::jmax(1, cellW);
    const int r = (e.y - area.getY()) / juce::jmax(1, cellH);
    if (c < 0 || c >= cols || r < 0 || r >= rows) return;

    const int daysInMonth = daysInMonthOf(m_dispYear, m_dispMonth);
    const int firstDow = (juce::Time(m_dispYear, m_dispMonth, 1, 0, 0).getDayOfWeek() + 6) % 7;
    const int day = r * cols + c - firstDow + 1;
    if (day < 1 || day > daysInMonth) return;

    m_selectedDay = dayStartUnix(m_dispYear, m_dispMonth, day);
    int existingRow = -1;
    for (int i = 0; i < (int) m_events.size(); ++i)
        if (eventDayStart(m_events[(size_t) i].startTime) == m_selectedDay) { existingRow = i; break; }
    if (existingRow >= 0 && m_list) m_list->selectRow(existingRow);
    repaint();

    if (e.getNumberOfClicks() >= 2)
    {
        if (existingRow >= 0) openEditor(false);
        else                  openEditor(true, m_selectedDay);
    }
}

void AgendaView::mouseInWeek(const juce::MouseEvent& e, juce::Rectangle<int> area)
{
    const int64_t anchor = m_selectedDay > 0 ? m_selectedDay : dayStartUnix(m_dispYear, m_dispMonth, 1);
    juce::Time a(anchor * (int64_t) 1000);
    const int dow = (a.getDayOfWeek() + 6) % 7;
    const int64_t weekStart = anchor - (int64_t) dow * 24 * 3600;

    const int cols = 7;
    const int colW = area.getWidth() / cols;
    const int c = (e.x - area.getX()) / juce::jmax(1, colW);
    if (c < 0 || c >= cols) return;

    m_selectedDay = weekStart + (int64_t) c * 24 * 3600;
    int existingRow = -1;
    for (int i = 0; i < (int) m_events.size(); ++i)
        if (eventDayStart(m_events[(size_t) i].startTime) == m_selectedDay) { existingRow = i; break; }
    if (existingRow >= 0 && m_list) m_list->selectRow(existingRow);
    repaint();

    if (e.getNumberOfClicks() >= 2)
    {
        if (existingRow >= 0) openEditor(false);
        else                  openEditor(true, m_selectedDay);
    }
}

void AgendaView::mouseInYear(const juce::MouseEvent& e, juce::Rectangle<int> area)
{
    const int mcols = 4, mrows = 3;
    const int mw = area.getWidth() / mcols;
    const int mh = area.getHeight() / mrows;
    const int mc = (e.x - area.getX()) / juce::jmax(1, mw);
    const int mr = (e.y - area.getY()) / juce::jmax(1, mh);
    if (mc < 0 || mc >= mcols || mr < 0 || mr >= mrows) return;
    const int m0 = mr * mcols + mc;
    if (m0 < 0 || m0 > 11) return;
    m_dispMonth = m0;
    setMode(ViewMode::Month);
}

void AgendaView::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(60);

    auto bar = area.removeFromTop(36);
    m_newBtn->setBounds(bar.removeFromLeft(140)); bar.removeFromLeft(8);
    m_editBtn->setBounds(bar.removeFromLeft(90)); bar.removeFromLeft(6);
    m_delBtn->setBounds(bar.removeFromLeft(90));  bar.removeFromLeft(6);
    m_icsBtn->setBounds(bar.removeFromRight(120));    bar.removeFromRight(6);
    m_importBtn->setBounds(bar.removeFromRight(120));

    area.removeFromTop(8);
    auto ctrl = area.removeFromTop(34);
    if (m_monthViewBtn) { m_monthViewBtn->setBounds(ctrl.removeFromLeft(66).reduced(1)); ctrl.removeFromLeft(2); }
    if (m_weekViewBtn)  { m_weekViewBtn->setBounds(ctrl.removeFromLeft(78).reduced(1));  ctrl.removeFromLeft(2); }
    if (m_yearViewBtn)  { m_yearViewBtn->setBounds(ctrl.removeFromLeft(66).reduced(1)); }
    if (m_reminderBtn) { m_reminderBtn->setBounds(ctrl.removeFromRight(120).reduced(1)); ctrl.removeFromRight(6); }
    if (m_todayBtn)     { m_todayBtn->setBounds(ctrl.removeFromRight(96).reduced(1)); ctrl.removeFromRight(8); }
    if (m_prevMonthBtn) m_prevMonthBtn->setBounds(ctrl.removeFromLeft(34).reduced(1));
    if (m_nextMonthBtn) m_nextMonthBtn->setBounds(ctrl.removeFromRight(34).reduced(1));
    if (m_monthLabel)   m_monthLabel->setBounds(ctrl);

    const int top = calendarBounds().getBottom() + 12;
    auto full = getLocalBounds().reduced(16);
    juce::Rectangle<int> listArea(full.getX(), top, full.getWidth(), full.getBottom() - top);

    auto tools = listArea.removeFromTop(30);
    if (m_search) m_search->setBounds(tools.removeFromLeft(juce::jmax(180, tools.getWidth() / 2)));
    if (m_sortBtn) { tools.removeFromLeft(8); m_sortBtn->setBounds(tools.removeFromLeft(170)); }
    listArea.removeFromTop(6);

    auto pager = listArea.removeFromBottom(30);
    listArea.removeFromBottom(6);
    if (m_prevPageBtn) m_prevPageBtn->setBounds(pager.removeFromLeft(34).reduced(1));
    if (m_nextPageBtn) m_nextPageBtn->setBounds(pager.removeFromRight(34).reduced(1));
    if (m_pageLabel)   m_pageLabel->setBounds(pager);

    if (m_list) m_list->setBounds(listArea);
}

void AgendaView::retranslateUi() { repaint(); }

}
