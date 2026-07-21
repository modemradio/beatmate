#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BeatMate::Models { struct Track; }
namespace BeatMate::Services::Library { class TrackDatabase; }
namespace BeatMate::Core::Analysis    { class CollectionRadar; }
namespace BeatMate::Services::VirtualDJ { class VirtualDJRemote; }

namespace BeatMate::Services::Suggestions {

class MyStyleModel;

enum class EnergyDirection {
    Maintain,
    Increase,
    Decrease,
    Auto
};

enum class SessionPhase {
    Auto,
    Warmup,
    Peak,
    Cooldown
};

struct Suggestion {
    int64_t     trackId = 0;
    std::string filePath;
    std::string title;
    std::string artist;
    std::string genre;
    std::string camelotKey;
    double      bpm = 0.0;
    double      energyScore = 0.0;
    int         year = 0;

    int totalScore     = 0;   // 0-100 (weighted)
    int harmonicScore  = 0;
    int bpmScore       = 0;
    int energyScoreV   = 0;
    int styleScore     = 0;
    int trendingScore  = 0;
    int structureScore = 0;
    int timbreScore    = 0;
    int eraScore       = 0;
    int venueScore     = 0;
    int genreGraphScore= 0;
    int    lufsScore   = 0;
    double lufsDelta   = 0.0;
    bool   lufsKnown   = false;
    bool   edgeEnergyUsed = false;
    int phaseScore        = 0;  // 0-100 : fit with current session phase
    int continuityScore   = 0;  // 0-100 : smoothness vs last played energy slope
    int vocalBalanceScore = 0;  // 0-100 : penalises too many vocals in a row

    int mashupScore       = 0;

    float  timbreSim       = 0.0f;  // 0..1 raw cosine
    int    camelotDelta    = 0;     // wheel distance (signed clamped)
    double bpmDelta        = 0.0;   // signed (cand - cur)
    int    daysSincePlayed = -1;    // -1 if never / unknown

    enum class Level { Green, Yellow, Red } level = Level::Yellow;

    std::string reason;
    bool        isWildCard = false;
    bool        sharedPlaylist = false;

    int deck = 0;

    std::string transitionLabel;
    std::string transitionDetail;
    int         mixBars = 0;
    bool        manualPair = false;
};

enum class TransitionType {
    Cut,
    QuickBlend,
    Crossfade,
    BassSwap,
    FilterFade,
    EchoOut,
    LongBlend
};

struct TransitionPlan {
    TransitionType type = TransitionType::Crossfade;
    int            bars = 16;
    std::string    label;
    std::string    detail;
};

struct PhraseInfo {
    double bpm = 0.0;
    double durationSec = 0.0;
    double secondsPerBar = 0.0;
    int    barsTotal = 0;
    std::vector<double> phraseBoundariesSec;
    double mixInPointSec = -1.0;
    double mixOutPointSec = -1.0;
    int    phraseLengthBars = 16;
    bool   valid = false;
    std::string summary;
};

struct PathStep {
    int64_t     trackId = 0;
    std::string title;
    std::string artist;
    std::string camelotKey;
    double      bpm = 0.0;
    double      energyScore = 0.0;
    int         transitionScore = 0;
    std::string transitionLabel;
    int         mixBars = 0;
};

class SmartSuggestEngine {
public:
    explicit SmartSuggestEngine(std::shared_ptr<Library::TrackDatabase> db);
    ~SmartSuggestEngine();

    SmartSuggestEngine(const SmartSuggestEngine&) = delete;
    SmartSuggestEngine& operator=(const SmartSuggestEngine&) = delete;

    void setCollectionRadar(std::shared_ptr<Core::Analysis::CollectionRadar> radar);
    void setVirtualDJRemote(std::shared_ptr<VirtualDJ::VirtualDJRemote> remote);

    using ClapEmbeddingMap = std::unordered_map<int64_t, std::vector<float>>;
    using ClapLookup = std::function<std::shared_ptr<const ClapEmbeddingMap>()>;
    void setClapLookup(ClapLookup lookup);

    void setSessionVenue(const std::string& venue);

    void setBlacklist(std::unordered_set<int64_t> ids);
    void blacklistAdd(int64_t id);
    void blacklistRemove(int64_t id);

    void boostIds(const std::vector<int64_t>& ids);
    void boostClear();

    void setCurrentTrack(int64_t id);
    int64_t getCurrentTrack() const;

    void setEnergyDirection(EnergyDirection d);
    void setBPMRange(double minBpm, double maxBpm);
    void setHarmonicOnly(bool on);

    void setEnergyBoost(bool on);
    bool getEnergyBoost() const;

    void setBPMTolerancePercent(double pct);
    void setHarmonicMinScore(int minScore);

    void setMashupMode(bool on);

    void setSessionPhase(SessionPhase p);
    SessionPhase getSessionPhase() const;
    void noteJustPlayed(const Models::Track& t);
    void clearRecentEnergy();

    void setGenreFilter(const std::string& genre);

    void setKeyOverride(const std::string& camelot);

    void setStrictFilter(bool on);
    bool getStrictFilter() const;

    void setWeights(std::array<double, 10> w);

    void setWeights(std::array<double, 6> w);

    std::vector<Suggestion> getSuggestions(int maxResults = 10);

    std::vector<Suggestion> getSuggestionsForDeck(int deckNum, int maxResults = 10);

    std::string explainSuggestion(int64_t candidateTrackId);

    TransitionPlan suggestTransition(int64_t fromTrackId, int64_t toTrackId) const;

    PhraseInfo detectPhrases(int64_t trackId) const;
    PhraseInfo detectPhrasesForCurrent() const;

    std::vector<PathStep> findPathToTarget(int64_t targetTrackId,
                                           int maxHops = 4,
                                           int branching = 6) const;

    std::vector<PathStep> searchTracksForPicker(const std::string& query,
                                                int limit = 40) const;

    void associateTracks(int64_t fromTrackId, int64_t toTrackId, int weight = 3);
    void unassociateTracks(int64_t fromTrackId, int64_t toTrackId);
    bool isAssociated(int64_t fromTrackId, int64_t toTrackId) const;
    void setManualAssociations(const std::vector<std::pair<int64_t, int64_t>>& pairs);
    std::vector<std::pair<int64_t, int64_t>> getManualAssociations() const;
    void clearManualAssociations();

    void invalidateCache();

    void refreshStyleModel();

    void reportAccepted(int64_t suggestedId);
    void reportRejected(int64_t suggestedId);   // no DB write (backwards compat)
    void reportSkipped (int64_t suggestedId);   // decrements track_pairs weight

    static int calcHarmonicScore(const std::string& keyA, const std::string& keyB);
    static int calcHarmonicScore(const std::string& keyA, const std::string& keyB,
                                 bool energyBoostIntent);
    static std::string harmonicRelationName(const std::string& keyA, const std::string& keyB,
                                            bool energyBoostIntent = false);
    static int calcBPMScore(double bpmA, double bpmB);
    static double energyAtEdge(const std::string& energySegmentsJson, bool outro);
    static int calcEnergyScore(double energyA, double energyB, EnergyDirection dir);
    static int calcStructureScore(double introStartA, double introEndA,
                                  double outroStartA, double outroEndA,
                                  double introStartB, double introEndB,
                                  double outroStartB, double outroEndB);
    static int calcEraScore(int candYear, int preferredYearCenter, int spread);
    static int calcGenreGraphScore(const std::string& genreA, const std::string& genreB);
    static int clapSimilarityToScore(float cosine);
    static constexpr float kClapBaselineCos = 0.63f;
    static float computePersonalClapOffset(std::vector<float> cosines);
    static Suggestion::Level levelFor(int score);

    static TransitionPlan buildTransitionPlan(const Models::Track& from,
                                              const Models::Track& to);
    static PhraseInfo computePhrases(const Models::Track& t);

private:
    // Internal snapshot of tuning so scoring loop is lock-free.
    struct Settings {
        int64_t currentId = 0;
        EnergyDirection dir = EnergyDirection::Auto;
        double minBpm = 0.0;
        double maxBpm = 999.0;
        bool   harmonicOnly = false;
        bool   mashupMode   = false;
        bool   strictFilter = false;
        bool   energyBoost  = false;
        double bpmTolerancePct = 0.0;
        int    harmonicMinScore = 0;
        std::array<double, 10> w{};
        std::string genreFilter;
        std::string keyOverride;
        std::string sessionVenue;
        std::unordered_set<int64_t> blacklist;
        std::unordered_set<int64_t> boost;
        SessionPhase phase = SessionPhase::Auto;
        std::vector<double> recentEnergies;      // newest-last, max 6 entries
        std::vector<std::string> recentGenres;   // lowercased, same window
        int vocalTracksInARow = 0;               // from recent window
        std::unordered_map<int64_t, std::unordered_set<int64_t>> manualAssoc;
        std::shared_ptr<const ClapEmbeddingMap> clapEmbeds;
        float clapOffset = 0.0f;
        std::shared_ptr<const std::unordered_map<int64_t, std::vector<int64_t>>> playlistsByTrack;
    };

    Settings snapshot() const;
    void refreshRecentlyPlayedIfStale();   // 5 min cache
    void ensureStyleTrained();
    void refreshPersonalClapCalibration();
    void refreshPlaylistIndexIfStale();

    void scoreCandidate(Suggestion& s,
                        const Models::Track& cur,
                        const Models::Track& cand,
                        const Settings& set,
                        int maxPlayCount,
                        int preferredYearCenter,
                        int preferredYearSpread,
                        std::int64_t now) const;

    static std::string buildReason(const Suggestion& s, const Models::Track& cur,
                                   bool energyBoost = false);

    std::vector<Suggestion> diversify(std::vector<Suggestion> ranked,
                                      int maxResults) const;

    std::vector<Suggestion> pickWildCard(const std::vector<Suggestion>& poolAll,
                                         const std::unordered_set<int64_t>& chosen) const;

    std::shared_ptr<Library::TrackDatabase>               db_;
    std::shared_ptr<Core::Analysis::CollectionRadar>      radar_;
    std::shared_ptr<VirtualDJ::VirtualDJRemote>           vdj_;
    ClapLookup                                            clapLookup_;

    mutable std::mutex lock_;
    mutable std::mutex deckSerialMutex_;
    int64_t currentTrackId_ = 0;
    EnergyDirection energyDirection_ = EnergyDirection::Auto;
    double minBpm_ = 0.0;
    double maxBpm_ = 999.0;
    bool   harmonicOnly_ = false;
    bool   mashupMode_   = false;
    bool   strictFilter_ = false;
    bool   energyBoost_  = false;
    double bpmTolerancePct_ = 0.0;
    int    harmonicMinScore_ = 0;
    std::string genreFilter_;
    std::string keyOverride_;
    std::string sessionVenue_;
    std::unordered_set<int64_t> blacklist_;
    std::unordered_set<int64_t> boostSet_;

    std::array<double, 10> weights_ =
        { 0.24, 0.22, 0.12, 0.10, 0.02, 0.03, 0.14, 0.02, 0.01, 0.10 };

    std::unique_ptr<MyStyleModel> m_style;
    int64_t lastStyleTrainUnix_ = 0;
    std::atomic<float> personalClapOffset_{0.0f};
    std::shared_ptr<const std::unordered_map<int64_t, std::vector<int64_t>>> playlistsByTrack_;
    int64_t playlistIndexRefreshedAt_ = 0;

    std::unordered_set<int64_t> recentlyPlayed_;
    int64_t                     recentlyPlayedRefreshedAt_ = 0;

    SessionPhase       sessionPhase_ = SessionPhase::Auto;
    std::vector<double>      recentEnergies_;
    std::vector<std::string> recentGenres_;
    int                      recentVocalsInARow_ = 0;

    std::unordered_map<int64_t, std::unordered_set<int64_t>> manualAssoc_;

    static SessionPhase resolvePhase(SessionPhase p);                 // Auto → wall-clock phase
    static int scorePhaseForTrack(SessionPhase phase,
                                  double bpm, double energy);
    int scoreEnergyContinuity(double candEnergy,
                               const std::vector<double>& recent) const;
    int scoreVocalBalance(const Models::Track& cand,
                           int vocalsInARow) const;
};

} // namespace BeatMate::Services::Suggestions
