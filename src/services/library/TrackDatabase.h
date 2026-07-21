#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <optional>
#include <mutex>
#include <cstdint>

#include <sqlite3.h>

#include "../../models/Track.h"
#include "../../models/CuePoint.h"

namespace BeatMate::Services::History     { class SessionHistoryRecorder; }
namespace BeatMate::Services::Suggestions  { class MyStyleModel; }

namespace BeatMate::Services::Library {

class TrackDatabase {
public:
    TrackDatabase();
    explicit TrackDatabase(const std::string& dbPath);
    ~TrackDatabase();

    TrackDatabase(const TrackDatabase&) = delete;
    TrackDatabase& operator=(const TrackDatabase&) = delete;

    bool initialize(const std::string& dbPath = "");
    bool migrate();
    void close();
    bool isOpen() const;

    int64_t addTrack(const Models::Track& track);
    bool updateTrack(const Models::Track& track);
    bool deleteTrack(int64_t id);
    std::optional<Models::Track> getTrack(int64_t id);
    std::optional<Models::Track> getTrackByPath(const std::string& filePath);

    std::vector<Models::Track> getAllTracks();
    std::vector<Models::Track> getTracksByQuery(const std::string& sql, const std::vector<std::string>& params = {});
    int64_t getTrackCount();

    int64_t addCuePoint(const Models::CuePoint& cue);
    bool updateCuePoint(const Models::CuePoint& cue);
    bool deleteCuePoint(int64_t id);
    std::vector<Models::CuePoint> getCuePoints(int64_t trackId);
    std::map<int64_t, int> getCueCounts();

    std::string getSyncWatermark(const std::string& software);
    void setSyncWatermark(const std::string& software, const std::string& watermark);
    int getSyncCount(const std::string& software);

    struct TrackEmbedding {
        int64_t trackId = 0;
        std::vector<float> vec;
        int64_t fileMtime = 0;
    };
    bool upsertTrackEmbedding(int64_t trackId, const std::vector<float>& vec,
                              const std::string& modelVersion, int64_t fileMtime);
    std::vector<TrackEmbedding> loadAllTrackEmbeddings(const std::string& modelVersion);
    bool updateTrackGenreIfEmpty(int64_t trackId, const std::string& genre);
    bool updateTrackMoodIfEmpty(int64_t trackId, const std::string& mood);

    bool updateTrackAnalysis(int64_t trackId, double bpm, const std::string& key, float energy);
    bool updateTrackAnalysisByPath(const std::string& filePath, double bpm, const std::string& key, float energy);
    // Unconditional variant used by CollectionSyncService: the caller decides
    bool updateTrackAnalysisFromSync(int64_t trackId, double bpm, const std::string& key,
                                     float energy, const std::string& source);
    std::string getAnalysisSource(int64_t trackId);
    bool setAnalysisSource(int64_t trackId, const std::string& source);
    bool importCuePointsForTrack(int64_t trackId, const std::vector<Models::CuePoint>& cuePoints);

    bool setupFTS5();
    bool rebuildFTSIndex();
    std::vector<Models::Track> searchFTS(const std::string& query, int limit = 100);

    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    bool addTracks(const std::vector<Models::Track>& tracks);
    bool deleteTracks(const std::vector<int64_t>& ids);

    // Generic helpers (public for PlaylistManager + DJ-software sync paths)
    bool executeWrite(const std::string& sql, const std::vector<std::string>& params = {});
    int64_t getLastInsertRowId();

    // Tags personnels (tables tags + track_tags)
    std::vector<std::string> getTrackTags(int64_t trackId);
    bool setTrackTags(int64_t trackId, const std::vector<std::string>& tags);
    std::vector<std::string> getAllTags();

    // executeRead: SELECT with callback per row; row columns accessible through
    bool executeRead(const std::string& sql,
                     const std::vector<std::string>& params,
                     const std::function<void(sqlite3_stmt*)>& rowCallback);

    friend class TagManager;
    friend class BeatMate::Services::History::SessionHistoryRecorder;
    friend class BeatMate::Services::Suggestions::MyStyleModel;

private:
    bool createTables();
    bool createIndexes();
    Models::Track trackFromStatement(sqlite3_stmt* stmt);
    Models::CuePoint cuePointFromStatement(sqlite3_stmt* stmt);
    bool executeSQL(const std::string& sql);
    int getCurrentSchemaVersion();
    bool setSchemaVersion(int version);

    int getColumnIndex(sqlite3_stmt* stmt, const char* name);

    void bindString(sqlite3_stmt* stmt, int index, const std::string& value);

    sqlite3* db_ = nullptr;
    std::string dbPath_;
    int txDepth_ = 0;
    // Recursive so nested calls (e.g. addTracks -> beginTransaction -> addTrack)
    mutable std::recursive_mutex mutex_;
    bool isOpen_ = false;

    static constexpr int CURRENT_SCHEMA_VERSION = 11;
};

} // namespace BeatMate::Services::Library
