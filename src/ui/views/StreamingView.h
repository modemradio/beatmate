#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../models/Track.h"
#include "../../services/streaming/StreamingAccountService.h"
#include "../../services/streaming/StreamingIntegrationService.h"
#include "../../services/streaming/StreamingManager.h"
#include "../../services/streaming/BeatportService.h"
#include "../../services/streaming/BillboardService.h"
#include "../../services/streaming/TrendingTracksService.h"
#include "../../services/streaming/BeatportOfflineLockerService.h"
#include "../../services/streaming/ChartScheduler.h"
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::Services::Streaming { class SpotifyService; }

namespace BeatMate::UI {

class TrackTablePanel : public juce::Component, public juce::TableListBoxModel {
public:
    enum ColumnId { ColRank=1, ColTitle, ColArtist, ColBPM, ColKey, ColGenre, ColPlays, ColEnergy, ColDate, ColCues, ColDelta, ColLib, ColYt };

    struct ChartRowMeta {
        int delta = 0;
        int prevPosition = 0;
        bool isNew = false;
        bool hasDelta = false;
        bool inLibrary = false;
        juce::String previewUrl;
    };

    TrackTablePanel(const juce::String& sectionTitle, bool showRank = true);
    ~TrackTablePanel() override = default;

    void setTracks(const std::vector<Models::Track>& tracks);
    const std::vector<Models::Track>& getTracks() const { return m_tracks; }

    void setChartMeta(std::vector<ChartRowMeta> meta);
    const ChartRowMeta* chartMetaFor(int row) const;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics& g, int row, int colId, int w, int h, bool selected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(const Models::Track&)> onTrackDoubleClicked;
    std::function<void(const Models::Track&, juce::Point<int> screenPos)> onTrackRightClicked;
    std::function<void(int, const Models::Track&)> onRowDoubleClicked;
    std::function<void(int, const Models::Track&, juce::Point<int>)> onRowRightClicked;
    std::function<void(int, const Models::Track&)> onYoutubeClicked;

    void addColumns(std::initializer_list<ColumnId> cols, bool sortable = true);
    void setEmptyMessage(const juce::String& msg) { m_emptyMessage = msg; }
    void setEmptyMessageKey(const juce::String& key);
    void retranslateUi();

private:
    void drawBadge(juce::Graphics& g, const juce::String& text, juce::Colour bgCol,
                   int x, int y, int w, int h) const;
    juce::String formatRelativeDate(int64_t timestamp) const;

    juce::Label m_sectionLabel;
    std::unique_ptr<juce::TableListBox> m_table;
    std::unique_ptr<juce::TableListBox>& m_trackTable = m_table; // legacy alias
    std::vector<Models::Track> m_tracks;
    std::vector<ChartRowMeta> m_chartMeta;
    bool m_showRank = true;
    juce::String m_emptyMessage { "Aucune piste disponible" };
    juce::String m_emptyMessageKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackTablePanel)
};

class ComptesTab : public juce::Component, public BeatMate::UI::IRetranslatable {
public:
    ComptesTab(); // legacy ctor (used by current .cpp)
    explicit ComptesTab(Services::Streaming::StreamingAccountService* accountService);
    ~ComptesTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();
    void retranslateUi() override;

    void onConnect(int serviceIndex);
    void onDisconnect(int serviceIndex);
    void onSetPrimary(int serviceIndex);

    struct ServiceRow {
        int serviceType = 0;
        juce::String name;
        juce::Colour statusLEDColor;
        juce::String tierText;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::Label> statusLabel;
        std::unique_ptr<juce::TextButton> connectBtn;
        std::unique_ptr<juce::TextButton> disconnectBtn;
        std::unique_ptr<juce::ToggleButton> primaryToggle;
    };

private:
    void initServiceRows();

    Services::Streaming::StreamingAccountService* m_accountService = nullptr;
    std::vector<ServiceRow> m_serviceRows;
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_infoLabel;
    std::unique_ptr<juce::Viewport> m_viewport;
    std::unique_ptr<juce::Component> m_content;

    std::vector<ServiceRow>& m_services = m_serviceRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComptesTab)
};

class RechercheTab : public juce::Component {
public:
    explicit RechercheTab(Services::Streaming::StreamingIntegrationService* integration);
    ~RechercheTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onSearch();
    void onImportTrack(int row);
    void onAddToPlaylist(int row);

private:
    Services::Streaming::StreamingIntegrationService* m_integration = nullptr;

    juce::TextEditor m_searchEdit;
    juce::ComboBox m_platformFilter; // Tous, Spotify, Tidal, AppleMusic, SoundCloud, YouTubeMusic, AmazonMusic, Beatport
    std::unique_ptr<TrackTablePanel> m_resultsTable;
    juce::TextButton m_importBtn      { "Importer" };
    juce::TextButton m_addToPlaylistBtn { "Ajouter au playlist" };
    std::unique_ptr<juce::Label> m_statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RechercheTab)
};

class PlaylistsTab : public juce::Component {
public:
    explicit PlaylistsTab(Services::Streaming::StreamingIntegrationService* integration);
    ~PlaylistsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();

private:
    void onSyncFrom();
    void onSyncTo();
    void onCrossSync();

    Services::Streaming::StreamingIntegrationService* m_integration = nullptr;

    juce::ListBox m_localPlaylists;
    juce::ListBox m_remotePlaylists;
    juce::ComboBox m_serviceSelector;
    juce::TextButton m_syncFromBtn    { "Sync depuis" };
    juce::TextButton m_syncToBtn      { "Exporter vers" };
    juce::TextButton m_crossSyncBtn   { "Cross-sync" };
    double m_syncProgressValue = 0.0;
    juce::ProgressBar m_syncProgress  { m_syncProgressValue };
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_localLabel;
    std::unique_ptr<juce::Label> m_remoteLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistsTab)
};

class ChartsTab : public juce::Component, public juce::Thread, public BeatMate::UI::IRetranslatable {
public:
    explicit ChartsTab(Services::Library::TrackDataProvider* provider = nullptr);
    ~ChartsTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    void run() override;
    void startChartLoad(bool force);

    Services::Library::TrackDataProvider* m_provider = nullptr;

    std::unique_ptr<TrackTablePanel> m_chartTable;
    std::unique_ptr<juce::TextButton> m_refreshBtn;
    std::unique_ptr<juce::ComboBox> m_countryCombo;
    std::unique_ptr<juce::Label> m_statusLabel;

    std::string m_selectedCountry = "fr";
    std::atomic<bool> m_forceRefresh { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChartsTab)
};

class DecouvrirTab : public juce::Component, public BeatMate::UI::IRetranslatable {
public:
    DecouvrirTab(Services::Library::TrackDataProvider* provider,
                 Services::Streaming::SpotifyService* spotify = nullptr);
    ~DecouvrirTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();
    void retranslateUi() override;

private:
    static bool camelotCompatible(const std::string& keyA, const std::string& keyB);
    static std::pair<int,char> parseCamelotKey(const std::string& key);
    std::vector<Models::Track> buildHarmonicChain(const std::vector<Models::Track>& pool, int maxLen);
    std::vector<Models::Track> findForgottenTracks(const std::vector<Models::Track>& all);
    std::vector<Models::Track> findSimilarToLast(const std::vector<Models::Track>& all);

    void fetchRecommendations();
    void fetchNewReleases();
    void fetchSimilarArtists();

    Services::Library::TrackDataProvider* m_provider = nullptr;
    Services::Streaming::SpotifyService* m_spotify = nullptr;

    std::unique_ptr<juce::Viewport> m_viewport;
    std::unique_ptr<juce::Component> m_content;

    std::unique_ptr<TrackTablePanel> m_forgottenPanel;
    std::unique_ptr<TrackTablePanel> m_similarPanel;
    std::unique_ptr<TrackTablePanel> m_harmonicPanel;

    std::unique_ptr<TrackTablePanel> m_recommendationsPanel;
    std::unique_ptr<TrackTablePanel> m_newReleasesPanel;
    std::unique_ptr<TrackTablePanel> m_similarArtistsPanel;

    std::unique_ptr<juce::Label> m_audioFeaturesTitle;
    std::unique_ptr<juce::Label> m_danceabilityLabel;
    std::unique_ptr<juce::Label> m_energyLabel;
    std::unique_ptr<juce::Label> m_valenceLabel;
    std::unique_ptr<juce::Slider> m_danceabilityBar;
    std::unique_ptr<juce::Slider> m_energyBar;
    std::unique_ptr<juce::Slider> m_valenceBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecouvrirTab)
};

class OfflineTab : public juce::Component {
public:
    explicit OfflineTab(Services::Streaming::BeatportOfflineLockerService* locker);
    ~OfflineTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();

    void onSync();
    void onDownloadAll();
    void onVerify();

private:
    Services::Streaming::BeatportOfflineLockerService* m_locker = nullptr;

    std::unique_ptr<TrackTablePanel> m_lockerTracks;
    double m_downloadProgressValue = 0.0;
    juce::ProgressBar m_downloadProgress { m_downloadProgressValue };
    juce::Label m_diskSpaceLabel;
    juce::ComboBox m_formatCombo;       // WAV, AIFF, MP3
    juce::TextButton m_syncBtn          { "Synchroniser" };
    juce::TextButton m_downloadAllBtn   { "Tout telecharger" };
    juce::TextButton m_verifyBtn        { "Verifier" };
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineTab)
};

class StreamingView : public juce::Component, public BeatMate::UI::IRetranslatable {
public:
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void searchRequested(const juce::String& query, const juce::String& platform) {}
        virtual void importTrackRequested(const Models::Track& track) {}
        virtual void playlistSyncRequested(const juce::String& playlistId, const juce::String& service) {}
        virtual void chartRefreshRequested(const juce::String& chartType, const juce::String& genre) {}
    };

    void addListener(Listener* l)    { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    StreamingView();
    explicit StreamingView(Services::Library::TrackDataProvider* provider);
    ~StreamingView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

    void onSearch();
    void onImportTrack();

private:
    void setupUI();
    void createTabs();

    Services::Library::TrackDataProvider* m_provider = nullptr;

    std::unique_ptr<Services::Streaming::StreamingAccountService>        m_accountService;
    std::unique_ptr<Services::Streaming::StreamingIntegrationService>    m_integrationService;
    std::unique_ptr<Services::Streaming::StreamingManager>              m_streamingManager;
    std::unique_ptr<Services::Streaming::BeatportService>               m_beatportService;
    std::unique_ptr<Services::Streaming::BillboardService>              m_billboardService;
    std::unique_ptr<Services::Streaming::TrendingTracksService>         m_trendingService;
    std::unique_ptr<Services::Streaming::BeatportOfflineLockerService>  m_offlineLockerService;
    std::unique_ptr<Services::Streaming::ChartScheduler>                m_chartScheduler;

    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TabbedComponent> m_mainTabs;
    std::unique_ptr<juce::TabbedComponent>& m_tabWidget = m_mainTabs; // legacy alias

    std::unique_ptr<ComptesTab>    m_comptesTab;
    std::unique_ptr<RechercheTab>  m_rechercheTab;
    std::unique_ptr<PlaylistsTab>  m_playlistsTab;
    std::unique_ptr<ChartsTab>     m_chartsTab;
    std::unique_ptr<DecouvrirTab>  m_decouvrirTab;
    std::unique_ptr<OfflineTab>    m_offlineTab;

    juce::ListenerList<Listener> m_listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamingView)
};


class HistoriqueTab : public juce::Component, public BeatMate::UI::IRetranslatable {
public:
    explicit HistoriqueTab(Services::Library::TrackDataProvider* provider = nullptr);
    ~HistoriqueTab() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void refresh();
    void retranslateUi() override;
private:
    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::unique_ptr<TrackTablePanel> m_history;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoriqueTab)
};

class TendancesTab : public juce::Component, public BeatMate::UI::IRetranslatable {
public:
    explicit TendancesTab(Services::Library::TrackDataProvider* provider = nullptr);
    ~TendancesTab() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void refresh();
    void retranslateUi() override;
private:
    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::unique_ptr<juce::Viewport> m_viewport;
    std::unique_ptr<juce::Component> m_content;
    std::unique_ptr<TrackTablePanel> m_topPlayed, m_recentlyAdded, m_recentlyAnalyzed;
    std::unique_ptr<juce::Label> m_unanalyzedBanner;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TendancesTab)
};

} // namespace BeatMate::UI
