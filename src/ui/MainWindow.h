#pragma once
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "widgets/favorites/FavoritesBar.h"
#include "widgets/ToastNotifier.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI { class BeatMateLiveWindow; }

namespace BeatMate::UI {

class NowPlayingBar;

// HoverableListBox: ListBox that detects hover on its own rows
class HoverableListBox : public juce::ListBox
{
public:
    HoverableListBox(const juce::String& name, juce::ListBoxModel* model)
        : juce::ListBox(name, model)
    {
        // Create a helper listener to intercept mouse events from child components (viewport)
        m_mouseHelper = std::make_unique<MouseHelper>(*this);
    }

    ~HoverableListBox() override
    {
        if (m_mouseHelper)
            removeMouseListener(m_mouseHelper.get());
    }

    void parentHierarchyChanged() override
    {
        juce::ListBox::parentHierarchyChanged();
        // Re-attach listener with wantsEventsForAllNestedChildComponents = true
        if (m_mouseHelper)
        {
            removeMouseListener(m_mouseHelper.get());
            addMouseListener(m_mouseHelper.get(), true);
        }
    }

    std::function<void(int row)> onRowHovered;
    std::function<void()> onMouseExited;

private:
    struct MouseHelper : public juce::MouseListener
    {
        HoverableListBox& owner;
        explicit MouseHelper(HoverableListBox& o) : owner(o) {}

        void mouseMove(const juce::MouseEvent& e) override
        {
            auto localEvent = e.getEventRelativeTo(&owner);
            int row = owner.getRowContainingPosition(localEvent.x, localEvent.y);
            if (row != owner.m_lastHoveredRow)
            {
                owner.m_lastHoveredRow = row;
                if (owner.onRowHovered) owner.onRowHovered(row);
            }
        }

        void mouseExit(const juce::MouseEvent& e) override
        {
            auto pos = e.getEventRelativeTo(&owner).getPosition();
            if (!owner.getLocalBounds().contains(pos))
            {
                owner.m_lastHoveredRow = -1;
                if (owner.onMouseExited) owner.onMouseExited();
            }
        }
    };

    std::unique_ptr<MouseHelper> m_mouseHelper;
    int m_lastHoveredRow = -1;
};

class MainWindow : public juce::DocumentWindow,
                   public juce::Timer
{
public:
    explicit MainWindow(const juce::String& name = "BeatMate V12");
    ~MainWindow() override;

    enum NavigationItem {
        Nav_Home = 0,
        Nav_Library,
        Nav_Import,
        Nav_Analysis,
        Nav_HotCues,
        Nav_Normalization,
        Nav_SetPreparation,
        Nav_SoireePreparation,
        Nav_PlaylistPreparation,
        Nav_BeatMateLive,
        Nav_Export,
        Nav_PerfDJ,
        Nav_Streaming,
        Nav_Settings,
        Nav_Help,
        Nav_SonicDeck,
        Nav_Jingle,
        Nav_Mix,
        Nav_Agenda,
        Nav_Compare
    };

    void navigateTo(NavigationItem item);
    void closeButtonPressed() override;

    /** Listener interface for navigation changes */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void navigationChanged(int index) = 0;
        virtual void djSoftwareConnected(const juce::String& name) {}
        virtual void djSoftwareDisconnected() {}
    };

    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void updateNowPlaying(const juce::String& title, const juce::String& artist,
                          double bpm, const juce::String& key);
    void showFirstTimeSetup();

    void timerCallback() override;

private:
    /** The main content component that holds sidebar + central area + now playing bar */
    class MainContentComponent : public juce::Component,
                                 public juce::ListBox,
                                 public juce::ListBoxModel
    {
    public:
    };

    class ContentComponent;

    juce::ListenerList<Listener> m_listeners;

    bool m_djConnected = false;
    juce::String m_connectedDJSoftware;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

// ContentComponent: the main content inside the DocumentWindow
class MainWindow::ContentComponent : public juce::Component,
                                     public juce::ChangeListener
{
public:
    ContentComponent(MainWindow& owner);
    ~ContentComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    // mouseMove/mouseExit for nav hover are handled by HoverableListBox

    void navigateTo(int index);
    void createViews();

    // Sidebar navigation list with Path-based icons
    class NavListModel : public juce::ListBoxModel
    {
    public:
        NavListModel(ContentComponent& owner) : m_owner(owner) {}
        int getNumRows() override { return static_cast<int>(m_items.size()); }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;

        static juce::Path getIconPath(int index, float cx, float cy, float sz);

        struct NavItem { juce::String text; int navTarget; };
        std::vector<NavItem> m_items;
        ContentComponent& m_owner;
        int m_hoveredRow = -1;
        int m_precachePending = 0;
        int m_analysisDone = 0;
        int m_analysisTotal = 0;
    };

    MainWindow& m_owner;

    std::unique_ptr<HoverableListBox> m_navList;
    std::unique_ptr<NavListModel> m_navModel;

    // Central view stack (only one visible at a time), wrapped in a Viewport for scrolling
    std::map<int, std::unique_ptr<juce::Component>> m_views;
    std::unique_ptr<juce::Viewport> m_viewport;
    int m_currentViewIndex = 0;

    // LocalizationService emits a ChangeMessage at boot (async). Our listener
    bool m_firstLangCallbackConsumed = false;

    std::unique_ptr<NowPlayingBar> m_nowPlayingBar;

    // Favorites bar (12 pinned playlist slots, global)
    std::unique_ptr<Widgets::FavoritesBar> m_favoritesBar;

    std::unique_ptr<juce::Label> m_statusDJSoftware;
    std::unique_ptr<juce::Label> m_statusCPU;
    std::unique_ptr<juce::Label> m_statusRAM;
    std::unique_ptr<juce::Label> m_statusLatency;
    std::unique_ptr<juce::Label> m_statusTrack;
    std::unique_ptr<juce::Label> m_statusCreator;

    juce::Rectangle<int> m_cpuBarRect;
    juce::Rectangle<int> m_ramBarRect;
    float m_cpuFill = 0.0f;
    float m_ramFill = 0.0f;
    std::map<juce::TextButton*, int> m_toolbarTargets;

    std::unique_ptr<juce::TextButton> m_tbImport;
    std::unique_ptr<juce::TextButton> m_tbAnalyze;
    std::unique_ptr<juce::TextButton> m_tbHotCues;
    std::unique_ptr<juce::TextButton> m_tbNormalize;
    std::unique_ptr<juce::TextButton> m_tbExport;
    std::unique_ptr<juce::TextButton> m_tbAgenda;

    // Rappel d'événement Agenda (cloche, tout à droite de la toolbar)
    std::unique_ptr<juce::TextButton> m_agendaReminderBtn;
    std::vector<int64_t> m_notifiedEventIds;
    juce::uint32 m_lastReminderCheckMs = 0;

    // Rappels acquittes au clic sur la cloche : cle = id evenement * 1000000 + palier.
    std::vector<int64_t> m_ackedReminderKeys;
    std::vector<int64_t> m_activeReminderKeys;
    void acknowledgeReminders();
    void updateAgendaReminder();

    std::unique_ptr<juce::Label> m_logoLabel;

    std::unique_ptr<BeatMateLiveWindow> m_beatMateLiveWindow;

    std::unique_ptr<juce::DocumentWindow> m_perfDJWindow;

    // Toast notifications overlay (top of Z-order)
    std::unique_ptr<Widgets::ToastNotifier> m_toastNotifier;

    std::unique_ptr<juce::TooltipWindow> m_tooltipWindow;

    static constexpr int kSidebarWidth = 220;
    static constexpr int kToolbarHeight = 44;
    static constexpr int kStatusBarHeight = 24;
    static constexpr int kNowPlayingHeight = 72;

    // Listener implementations that wire view events to real services
    struct ListenerStorage;
    std::unique_ptr<ListenerStorage> m_listenerStorage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
};

} // namespace BeatMate::UI
