#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <map>
#include <memory>

namespace BeatMate::Services::Streaming { struct LiveChart; }
namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::Services::Live { class NowPlayingService; struct NowPlayingTrack; }
namespace BeatMate::Services::Suggestions { class SmartSuggestEngine; class DJProfileService; }
namespace BeatMate::Models { struct DJProfile; struct Track; }
namespace BeatMate::UI::Widgets { class SuggestionPanel; class RediscoverPanel; class CrowdEnergyMeter; }
namespace BeatMate::UI {

class BeatMateLiveView : public juce::Component,
                         private juce::Timer
{
public:
    BeatMateLiveView();
    explicit BeatMateLiveView(Services::Library::TrackDataProvider* provider);
    ~BeatMateLiveView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    struct SuggestionPayload {
        juce::String title;
        juce::String artist;
        juce::String filePath;
        double bpm = 0.0;
        juce::String key;
    };
    std::function<void(const SuggestionPayload&)> onSendSuggestionToPrep;

private:
    void timerCallback() override;

    struct NowPlayingInfo
    {
        juce::String title = "---";
        juce::String artist = "---";
        double bpm = 0.0;
        juce::String key;
        float energy = 0.0f;
        bool connected = false;
        juce::String deck = "DECK A";
    };

    struct SuggestionEntry
    {
        juce::String title;
        juce::String artist;
        double bpm = 0.0;
        juce::String key;
        float score = 0.0f;
        float relevance = 0.0f;
        juce::String streamingSource;
        juce::String filePath;
        juce::String reason;
        juce::String baseReason;
        juce::String previewUrl;
        bool inLibrary = false;
        float energy = 0.0f;
        int chartPos = 0;
        int chartDelta = 0;
        bool chartNew = false;
        bool alreadyPlayed = false;
    };

    struct HistoryEntry
    {
        juce::String title;
        juce::String artist;
        double bpm = 0.0;
        juce::String key;
        float energy = 0.0f;
        juce::String timestamp;
        juce::String durationPlayed;
    };

    struct TrendingTrack
    {
        int position = 0;
        juce::String title;
        juce::String artist;
        double bpm = 0.0;
        juce::String key;
        juce::String genre;
        float energy = 0.0f;
        float trendScore = 0.0f;
        int playCount = 0;
        int delta = 0;
        int prevPosition = 0;
        bool isNew = false;
        bool inLibrary = false;
        juce::String source;
        juce::String localPath;
        juce::String previewUrl;
    };

    NowPlayingInfo nowPlaying_;
    std::vector<SuggestionEntry> suggestions_;
    std::vector<HistoryEntry> history_;
    std::vector<TrendingTrack> trending_;

    bool monitoring_ = false;
    bool compactMode_ = false;
    bool alwaysOnTop_ = false;

    std::vector<float> waveformLow_;
    std::vector<float> waveformMid_;
    std::vector<float> waveformHigh_;
    float playheadPosition_ = 0.0f;
    int waveformAnimOffset_ = 0;

    int totalTracksPlayed_ = 0;
    juce::String totalSetDuration_ = "0:00";
    double averageBPM_ = 0.0;

    enum class BottomTab { Suggestions, Trending, History };
    BottomTab activeBottomTab_ = BottomTab::Suggestions;


    std::unique_ptr<juce::ToggleButton> alwaysOnTopToggle_;
    std::unique_ptr<juce::TextButton> compactToggle_;
    std::unique_ptr<juce::TextButton> settingsButton_;
    std::unique_ptr<juce::TextButton> helpButton_;
    std::unique_ptr<juce::TextButton> compactBackButton_;
    std::unique_ptr<juce::TextButton> importRekordboxXmlButton_;
    std::unique_ptr<juce::Label>      profileLbl_;
    std::unique_ptr<juce::ComboBox>   profileCb_;
    std::unique_ptr<juce::TextButton> profileSaveAsBtn_;
    void rebuildProfileCombo();
    void onProfileSelected();
    void onSaveProfileAs();
    std::string activeProfileName_;

    std::unique_ptr<juce::TextButton> connectButton_;
    std::unique_ptr<juce::TextButton> disconnectButton_;
    std::unique_ptr<juce::FileChooser> importRekordboxXmlChooser_;
    std::shared_ptr<std::atomic<bool>> importBusy_ { std::make_shared<std::atomic<bool>>(false) };
    void importRekordboxXmlFlow();
    std::unique_ptr<juce::ComboBox> sourceSelector_;

    std::unique_ptr<juce::TextButton> tabSuggestions_;
    std::unique_ptr<juce::TextButton> tabTrending_;
    std::unique_ptr<juce::TextButton> tabHistory_;

    std::unique_ptr<juce::ComboBox> genreFilter_;
    std::unique_ptr<juce::ComboBox> countryFilter_;
    std::unique_ptr<juce::ComboBox> trendingSortCb_;
    std::unique_ptr<juce::TextButton> trendingRefreshBtn_;
    std::unique_ptr<juce::Label> trendingDateLbl_;
    bool trendingLoadedOnce_ = false;

    class SuggestionListModel : public juce::ListBoxModel
    {
    public:
        std::vector<SuggestionEntry>* entries = nullptr;
        int hoveredRow = -1;
        bool cabin = false;
        std::function<void(int row, const juce::MouseEvent&)> onItemClicked;
        std::function<void(int row)> onItemDoubleClicked;
        std::function<void(int row)> onPinClicked;
        std::function<void(int row)> onPlayClicked;
        std::function<void(int row)> onSendClicked;
        int entryIndexForEvent(int row, const juce::MouseEvent& e) const;
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                              int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    };

    class HistoryListModel : public juce::ListBoxModel
    {
    public:
        std::vector<HistoryEntry>* entries = nullptr;
        int hoveredRow = -1;
        std::function<void(int row)> onItemDoubleClicked;
        std::function<void(int row)> onSendClicked;
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                              int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    };

    class TrendingListModel : public juce::ListBoxModel
    {
    public:
        std::vector<TrendingTrack>* entries = nullptr;
        int hoveredRow = -1;
        std::function<void(int row)> onOpenSendMenu;
        std::function<void(int row)> onQuickSend;
        std::function<void(int row)> onSendClicked;
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                              int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    };

    std::unique_ptr<SuggestionListModel> suggestionModel_;
    std::unique_ptr<juce::ListBox> suggestionList_;

    std::unique_ptr<HistoryListModel> historyModel_;
    std::unique_ptr<juce::ListBox> historyList_;

    std::unique_ptr<TrendingListModel> trendingModel_;
    std::unique_ptr<juce::ListBox> trendingList_;

    class RowHover;
    std::unique_ptr<RowHover> suggestionHover_;
    std::unique_ptr<RowHover> historyHover_;
    std::unique_ptr<RowHover> trendingHover_;

    class RowDragSource;
    std::unique_ptr<RowDragSource> suggestionDragSource_;

    std::vector<SuggestionEntry> setQueue_;
    class SetQueuePanel;
    std::unique_ptr<SetQueuePanel> queuePanel_;

    bool cabinMode_ = false;

    juce::String activeDetectedSource_;

    struct ChartHit { int position = 0; int delta = 0; bool isNew = false; };
    std::map<std::string, ChartHit> chartIndex_;

    bool ledBlinkState_ = true;

    juce::Rectangle<int> statusChipBounds_;

    int pollingIntervalSec_ = 2;

    int pollCounter_ = 0;

    std::shared_ptr<std::atomic<bool>> pollBusy_ { std::make_shared<std::atomic<bool>>(false) };

    juce::int64 lastDetectedActivityMs_ = 0;
    static constexpr int kStaleThresholdMs = 30000;
    static constexpr int kIdleThresholdMs = 60000;

    juce::String currentSource_ = "Rekordbox";
    juce::String lastDetectedTitle_;
    juce::String nowPlayingPath_;

    std::unique_ptr<Services::Live::NowPlayingService> nowPlayingSvc_;

    void startMonitoring();
    void stopMonitoring();
    void pollCurrentTrack();
    void pollAudioIa();
    void applyDetectedTrack(const Services::Live::NowPlayingTrack& detected);
    void updateSuggestions();
    void rebuildEntriesFromEngine();
    void applyModeWeights();
    void loadChartIndexAsync();
    void applyCabinMode();
    void toggleCabinMode();
    void addEntryToQueue(const SuggestionEntry& entry);
    void persistSetQueue();
    void restoreSetQueue();
    void updateQueueHeader();
    void showSendToDJMenuForEntry(const SuggestionEntry& entry);
    void playEntryPreview(const SuggestionEntry& entry, const juce::String& rowPrefix);
    void paintSessionSparkline(juce::Graphics& g, juce::Rectangle<int> area);
    void paintSetEnergyCurve(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPhraseHint(juce::Graphics& g, juce::Rectangle<int> area);
    void addToHistory(const juce::String& title, const juce::String& artist,
                      double bpm, const juce::String& key, float energy);
    void generateWaveformData();
    void switchAIMode();
    void switchBottomTab(BottomTab tab);
    void toggleCompactMode();
    void generateDemoData();
    void updateStats();
    void loadTrendingFromCollection();
    void loadTrendingFromDiskCache();
    void loadTrendingForCountry(const juce::String& cc, bool forceRefresh = false);
    static void fillTrendingFromChart(const Services::Streaming::LiveChart& chart,
                                      const std::map<std::string, Models::Track>* library,
                                      std::vector<TrendingTrack>& out);
    void reloadTrendingSelection(bool forceRefresh);
    juce::String selectedTrendingCountry() const;
    void applyTrendingSort();
    void locateTrendingInLibrary(int row);
    void refreshSimilarStreaming(const juce::String& title, const juce::String& artist);
    juce::String lastSimilarKey_;
    bool seedingHistory_ = false;
    void showSettingsPanel();
    void checkForUpdatesFlow();
    void showHelpPopup();
    void showSessionExportMenu();
    void showSendToDJMenu(int rowIndex, juce::Point<int> screenPos);
    void revealFileInExplorer(const juce::String& filePath);
    void mouseDown(const juce::MouseEvent& event) override;

    void paintHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void paintNowPlaying(juce::Graphics& g, juce::Rectangle<int> area);
    void paintWaveformRGB(juce::Graphics& g, juce::Rectangle<int> area);
    void paintEnergyArc(juce::Graphics& g, juce::Rectangle<int> area, float energy);
    void paintConnectionLED(juce::Graphics& g, juce::Rectangle<int> area, bool connected);
    void paintCompactView(juce::Graphics& g);
    void paintGlassPanel(juce::Graphics& g, juce::Rectangle<int> area);
    void paintTabBar(juce::Graphics& g, juce::Rectangle<int> area);
    void paintHistoryStats(juce::Graphics& g, juce::Rectangle<int> area);

    Services::Library::TrackDataProvider* m_provider = nullptr;

    Services::Suggestions::SmartSuggestEngine* smartEngine_ = nullptr;
    std::unique_ptr<Widgets::SuggestionPanel> smartPanel_;
    double lastClapAutoRefreshMs_ = 0.0;
    void refreshSmartSuggestFor(int64_t trackId);
    juce::String phraseHintCache_;
    void updatePhraseHintCache();

    std::unique_ptr<juce::TabbedComponent> rightTabs_;

    std::unique_ptr<juce::TextButton>  exportSessionBtn_;
    std::unique_ptr<juce::FileChooser> sessionExportChooser_;

    std::unique_ptr<Widgets::CrowdEnergyMeter> crowdEnergyMeter_;

    class SuggestionsTabContent;
    class TrendingTabContent;
    class HistoryTabContent;
    class MyStyleTabContent;
    class RediscoverTabContent;
    std::unique_ptr<SuggestionsTabContent>  tabContentSuggestions_;
    std::unique_ptr<TrendingTabContent>     tabContentTrending_;
    std::unique_ptr<HistoryTabContent>      tabContentHistory_;
    std::unique_ptr<MyStyleTabContent>      tabContentMyStyle_;
    std::unique_ptr<RediscoverTabContent>   tabContentRediscover_;

    std::unique_ptr<Widgets::RediscoverPanel> rediscoverPanel_;

    void handleConnectClicked();
    void handleDisconnectClicked();
    int measureButtonWidth(const juce::String& label, int minWidth = 160, int padding = 28, float fontHeight = 13.0f) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatMateLiveView)
};

} // namespace BeatMate::UI
