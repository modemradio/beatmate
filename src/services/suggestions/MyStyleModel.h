#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

namespace BeatMate::Models { struct Track; }
namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

// Apprend le style DJ de l'utilisateur depuis play_history.
class MyStyleModel {
public:
    explicit MyStyleModel(std::shared_ptr<Services::Library::TrackDatabase> db);
    ~MyStyleModel();

    MyStyleModel(const MyStyleModel&) = delete;
    MyStyleModel& operator=(const MyStyleModel&) = delete;

    // Learn from the tracks the user has played. Idempotent and cheap
    void train();

    // Richer training pass used as the MasterSuggestionOrchestrator prior:
    void trainFromHistory();
    void trainFromHistoryAsync();

    // Incrémente le compteur de lectures depuis le dernier retrain.
    void notePlay();

    // Prior du MasterSuggestionOrchestrator (multiplicateur).
    float scoreCandidate(const Models::Track& current,
                         const Models::Track& candidate) const;

    // Returns true when enough history has been seen to make the prior
    bool  hasEnoughHistory() const;

    // Human-readable one-liner for the "Mon Style" widget.
    juce::String getProfileSummary() const;

    // Score how well `candidate` fits after `current` on a 0-100 scale.
    int calcStyleScore(int64_t currentTrackId, int64_t candidateTrackId);

    // Stats helpers surfaced by the "Mon Style" tab.
    int         totalPlays()      const;
    std::string dominantGenre()   const;
    double      avgBPM()          const;
    double      avgEnergy()       const;
    std::string dominantCamelot() const;
    std::vector<std::pair<std::string, int>> topGenres(int n = 5) const;

    // Distribution of years across the play history. Returns pairs
    std::vector<std::pair<int, int>> topYears(int n = 5) const;

    // Persist a successful (accepted) track pair into the track_pairs table,
    bool addPair(int64_t trackA, int64_t trackB, int delta = 1);

private:
    struct AvgFeatures {
        double bpm          = 0.0;
        double energy       = 0.0;
        double danceability = 0.0;
        double valence      = 0.0; // proxy: rating/5 (no valence column yet)
    };

    std::shared_ptr<Services::Library::TrackDatabase> db_;

    std::thread trainThread_;

    mutable std::mutex lock_;

    AvgFeatures                                                    avg_;
    std::unordered_map<int64_t, std::unordered_map<int64_t, int>>  pairCounts_;
    std::unordered_map<std::string, int>                           genreHistogram_;
    std::unordered_map<std::string, int>                           camelotHistogram_;
    std::unordered_map<int, int>                                   yearHistogram_;
    int                                                            totalPlays_       = 0;
    std::string                                                    dominantGenre_;
    std::string                                                    dominantCamelot_;

    // Extra profile state used by scoreCandidate() as a MasterOrchestrator
    std::unordered_map<int, int>                                   bpmBinHistogram_;    // bin = bpm/4
    std::unordered_map<int, int>                                   energyBinHistogram_; // bin = round(energy/1.0) on 0..10
    std::unordered_map<int, int>                                   hourHistogram_;      // 0..23
    std::unordered_map<std::string,
        std::unordered_map<std::string, int>>                      genreBigram_;        // genreA -> genreB -> count
    std::unordered_map<std::string,
        std::unordered_map<std::string, int>>                      keyBigram_;          // camelotA -> camelotB -> count
    std::unordered_map<int, int>                                   bpmDeltaHistogram_;  // bucketed signed delta (-10..+10)
    int                                                            preferredBpmBin_  = -1;
    double                                                         avgSessionSec_    = 0.0;
    int                                                            playsSinceRetrain_ = 0;
    static constexpr int                                           kMinPlaysForPrior = 20;
    static constexpr int                                           kRetrainEvery     = 10;

    static std::string profilePath();
    void saveProfileLocked() const;
    bool loadProfile();
    // Fusionne le feedback utilisateur persiste dans la table track_pairs
    void mergeTrackPairsFromDb(
        std::unordered_map<int64_t, std::unordered_map<int64_t, int>>& pairs) const;
};

} // namespace BeatMate::Services::Suggestions
