#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate { class ServiceLocator; }

namespace BeatMate::UI {


class HomeView : public juce::Component, private juce::Timer, public BeatMate::UI::IRetranslatable
{
public:
    // Module index used by onNavigateToModule for the "Preparation Soiree" page.
    static constexpr int kModuleIndexPreparationSoiree = 7;

    HomeView();
    explicit HomeView(Services::Library::TrackDataProvider* provider);
    ~HomeView() override;

    void refreshStats();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

    // Setters used to wire callbacks from MainWindow without exposing raw cards.
    using TrackClickedFn = std::function<void(const juce::String& filePath,
                                              const juce::String& title,
                                              const juce::String& artist)>;
    using SuggestionClickedFn = std::function<void(const juce::String& title,
                                                   const juce::String& artist)>;
    void setRecentAddedTrackClickHandler(TrackClickedFn fn);
    void setSuggestionClickHandler(SuggestionClickedFn fn);

    /** Listener for navigation requests from HomeView */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void navigateToLibrary() {}
        virtual void navigateToAnalysis() {}
        virtual void navigateToImport() {}
        virtual void trackSelected(int64_t trackId) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    // Direct navigation callback (used by cards like "Creer un evenement")
    std::function<void(int moduleIndex)> onNavigateToModule;

private:
    void setupUI();
    void timerCallback() override;

    // Debounce onDataChanged bursts.
    class DataChangeDebouncer : public juce::Timer {
    public:
        std::function<void()> onFire;
        void timerCallback() override {
            stopTimer();
            if (onFire) onFire();
        }
    };
    std::unique_ptr<DataChangeDebouncer> m_refreshDebouncer;

    class BentoCard : public juce::Component
    {
    public:
        juce::String cardTitle;
        juce::Colour accentColor;
        void paintCardBackground(juce::Graphics& g);
    };

    class CollectionCard : public BentoCard
    {
    public:
        int totalTracks = 0;
        int analyzedCount = 0;
        int analyzedPercent = 0;
        int genreCount = 0;
        juce::String lastSync;
        std::vector<int> bpmHistogram;
        void paint(juce::Graphics& g) override;
    };

    class RecentAddedCard : public BentoCard
    {
    public:
        // Layout constants used both by paint() and hit-testing — keeps both in sync.
        static constexpr int kListTopY   = 34;
        static constexpr int kRowHeight  = 34;

        struct TrackMini { juce::String title, artist, filePath; float bpm; juce::String key; int64_t id = 0; };
        std::vector<TrackMini> tracks;
        std::function<void(const juce::String& filePath, const juce::String& title, const juce::String& artist)> onTrackClicked;
        int hoveredIndex = -1;

        int visibleRows() const noexcept {
            return juce::jmin(static_cast<int>(tracks.size()),
                              juce::jmax(0, (getHeight() - kListTopY - 6) / kRowHeight));
        }

        // Returns the row index under point or -1 when out of the list area.
        int rowIndexAt(juce::Point<int> p) const noexcept {
            if (p.y < kListTopY) return -1;
            const int idx = (p.y - kListTopY) / kRowHeight;
            if (idx < 0 || idx >= visibleRows()) return -1;
            return idx;
        }

        void paint(juce::Graphics& g) override;
        void mouseMove(const juce::MouseEvent& e) override {
            const int idx = rowIndexAt(e.getPosition());
            if (idx != hoveredIndex) { hoveredIndex = idx; repaint(); }
        }
        void mouseExit(const juce::MouseEvent&) override {
            hoveredIndex = -1; repaint();
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            const int idx = rowIndexAt(e.getPosition());
            if (idx >= 0 && onTrackClicked)
                onTrackClicked(tracks[idx].filePath, tracks[idx].title, tracks[idx].artist);
        }
    };

    class RecentPlayedCard : public BentoCard
    {
    public:
        struct PlayedTrack { juce::String title, artist, timestamp; };
        std::vector<PlayedTrack> tracks;
        void paint(juce::Graphics& g) override;
    };

    class SuggestionsCard : public BentoCard
    {
    public:
        // Layout constants kept in sync with paint().
        static constexpr int kListTopY   = 34;
        static constexpr int kRowHeight  = 44;

        struct Suggestion { juce::String title, artist, reason; int score; };
        std::vector<Suggestion> suggestions;
        std::function<void(const juce::String& title, const juce::String& artist)> onTrackClicked;

        int visibleRows() const noexcept {
            return juce::jmin(static_cast<int>(suggestions.size()),
                              juce::jmax(0, (getHeight() - kListTopY - 6) / kRowHeight));
        }

        int rowIndexAt(juce::Point<int> p) const noexcept {
            if (p.y < kListTopY) return -1;
            const int idx = (p.y - kListTopY) / kRowHeight;
            if (idx < 0 || idx >= visibleRows()) return -1;
            return idx;
        }

        void paint(juce::Graphics& g) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            const int idx = rowIndexAt(e.getPosition());
            if (idx >= 0 && onTrackClicked)
                onTrackClicked(suggestions[idx].title, suggestions[idx].artist);
        }
    };

    class DJSoftwareCard : public BentoCard
    {
    public:
        struct Platform { juce::String name; bool connected; };
        std::vector<Platform> platforms;
        void paint(juce::Graphics& g) override;
    };

    class NextEventCard : public BentoCard
    {
    public:
        juce::String eventName, venue, date;
        int phasesCount = 0;
        bool hasEvent = false;
        juce::Rectangle<int> createEventBtnArea;
        std::function<void()> onCreateEvent;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override {
            if (!hasEvent && createEventBtnArea.contains(e.getPosition()) && onCreateEvent)
                onCreateEvent();
        }
    };

    class PerformanceCard : public BentoCard
    {
    public:
        float cpuPercent  = 0.0f;
        float ramMB       = 0.0f;
        float ramTotalMB  = 0.0f;
        float latencyMs   = 0.0f;
        // Disk gauge values in GB.
        float diskUsedGB  = 0.0f;
        float diskTotalGB = 0.0f;
        void paint(juce::Graphics& g) override;
    };

    // Cards (private storage, accessed via friend MainWindow).
    std::unique_ptr<CollectionCard>     m_collectionCard;
public:
    std::unique_ptr<RecentAddedCard>    m_recentAddedCard;
private:
    std::unique_ptr<RecentPlayedCard>   m_recentPlayedCard;
public:
    std::unique_ptr<SuggestionsCard>    m_suggestionsCard;
private:
    std::unique_ptr<DJSoftwareCard>     m_djSoftwareCard;
    std::unique_ptr<NextEventCard>      m_nextEventCard;
    std::unique_ptr<PerformanceCard>    m_performanceCard;

    std::unique_ptr<juce::Label> m_titleLabel;

    std::unique_ptr<juce::TextButton> m_viewAllBtn;
    std::unique_ptr<juce::TextButton> m_connectBtn;

    juce::ListenerList<Listener> m_listeners;

    Services::Library::TrackDataProvider* m_provider = nullptr;

    float m_pulsePhase = 0.0f;

#ifdef _WIN32
    // Previous CPU FILETIME samples.
    uint64_t m_prevIdleFt   = 0;
    uint64_t m_prevKernelFt = 0;
    uint64_t m_prevUserFt   = 0;
#endif

    // Alive flag captured by the data-changed callback, reset in dtor.
    std::shared_ptr<std::atomic<bool>> m_aliveFlag;

    // Cached pointer to the global ServiceLocator.
    static BeatMate::ServiceLocator* serviceLocator() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HomeView)
};

} // namespace BeatMate::UI
