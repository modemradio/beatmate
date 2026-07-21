#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <cstdint>

#include "../library/TrackDatabase.h"
#include "../djsoftware/DJHistoryReader.h"

namespace BeatMate::Services::DJSoftware { class UnifiedDJHistory; }

namespace BeatMate::Services::History {

struct SessionInfo {
    std::string sessionId;
    std::string sessionName;
    std::string venue;
    int64_t startedAt = 0;
    int64_t endedAt   = 0;
};

struct PlayEvent {
    int64_t trackId = 0;
    int64_t playedAt = 0;
    std::string sessionId;
    std::string context;
};

class SessionHistoryRecorder {
public:
    explicit SessionHistoryRecorder(std::shared_ptr<Services::Library::TrackDatabase> db);
    ~SessionHistoryRecorder();

    SessionHistoryRecorder(const SessionHistoryRecorder&) = delete;
    SessionHistoryRecorder& operator=(const SessionHistoryRecorder&) = delete;

    std::string startSession(const std::string& name, const std::string& venue = "");
    void endSession();
    std::optional<SessionInfo> currentSession() const;

    bool recordPlay(int64_t trackId, const std::string& context = "");

    int importExternalPlayHistory(
        const std::vector<Services::DJSoftware::PlayedTrack>& played);

    void importExternalHistoryAsync(Services::DJSoftware::UnifiedDJHistory& unified,
                                    std::function<void(int)> onDone = {});

    void setOnPlayRecorded(std::function<void(int64_t trackId)> cb);

    std::vector<PlayEvent> getSessionEvents(const std::string& sessionId) const;
    std::vector<PlayEvent> currentSessionEvents() const;
    std::vector<SessionInfo> listSessions() const;
    std::vector<int64_t>     mostPlayed(int topN = 20) const;

    // Stats agregees d'une session, calculees ici pour que l'UI/exports n'aient rien a recalculer
    struct SessionSummary {
        std::string  sessionId;
        std::string  sessionName;
        int64_t      durationSec    = 0;   // last event timestamp - first event timestamp
        int          trackCount     = 0;
        double       avgBpm         = 0.0;
        double       avgEnergy      = 0.0;
        double       biggestBpmJump = 0.0; // |BPM(n+1) - BPM(n)| max
        int          biggestKeyJump = 0;   // Camelot wheel distance, max
        std::string  dominantGenre;
        std::vector<std::pair<std::string, int>> topArtists;     // artist, plays (desc)
        std::vector<std::string>                 camelotJourney; // ordered keys per play
    };

    SessionSummary summary(const std::string& sessionId) const;

private:
    std::string buildContextJson(const std::string& userContext) const;

    static bool parseSessionFromContext(const std::string& json,
                                        std::string& outSessionId,
                                        std::string& outName,
                                        std::string& outVenue);

    static std::string jsonEscape(const std::string& s);

    std::shared_ptr<Services::Library::TrackDatabase> m_db;
    std::optional<SessionInfo> m_currentSession;
    mutable std::mutex m_mutex;
    std::function<void(int64_t)> m_onPlayRecorded;
    std::thread m_importThread;
};

} // namespace BeatMate::Services::History
