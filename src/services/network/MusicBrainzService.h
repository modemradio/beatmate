#pragma once
#include "HttpClient.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Network {


struct MusicBrainzRecording {
    std::string mbid;
    std::string title;
    std::string artist;
    std::string album;
    std::string releaseDate;
    int durationMs = 0;
    std::string isrc;
    int score = 0;              // Search relevance score
    std::vector<std::string> tags;
    std::string genre;
};

struct MusicBrainzRelease {
    std::string mbid;
    std::string title;
    std::string artist;
    std::string date;
    std::string country;
    std::string label;
    std::string barcode;
    int trackCount = 0;
};

struct AcoustIDResult {
    std::string acoustId;
    float score = 0.0f;
    std::vector<MusicBrainzRecording> recordings;
};

class MusicBrainzService {
public:
    using RecordingCallback = std::function<void(const std::vector<MusicBrainzRecording>&, const std::string& error)>;
    using ReleaseCallback = std::function<void(const std::vector<MusicBrainzRelease>&, const std::string& error)>;

    MusicBrainzService();
    ~MusicBrainzService();

    void searchByTitle(const std::string& title, const std::string& artist, RecordingCallback callback);
    void searchByISRC(const std::string& isrc, RecordingCallback callback);
    void searchByBarcode(const std::string& barcode, ReleaseCallback callback);

    void lookupRecording(const std::string& mbid, RecordingCallback callback);
    void lookupRelease(const std::string& mbid, ReleaseCallback callback);

    void lookupByFingerprint(const std::string& fingerprint, int durationSecs, RecordingCallback callback);

    std::vector<MusicBrainzRecording> searchSync(const std::string& title, const std::string& artist = "");

    void setRateLimit(int requestsPerSecond);

    void setUserAgent(const std::string& appName, const std::string& version, const std::string& contact);

    void setAcoustIDApiKey(const std::string& key);

private:
    MusicBrainzRecording parseRecording(const nlohmann::json& j) const;
    MusicBrainzRelease parseRelease(const nlohmann::json& j) const;
    std::string urlEncode(const std::string& str) const;
    void enforceRateLimit();
    void launchWorker(std::function<void()> job);

    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::vector<Worker> workers_;
    std::mutex workersMutex_;
    std::atomic<bool> shuttingDown_{false};

    HttpClient httpClient_;
    std::string userAgent_;
    std::string acoustIdApiKey_;
    int rateLimitMs_ = 1100;    // MusicBrainz requires max 1 request per second
    std::chrono::steady_clock::time_point lastRequestTime_;
    mutable std::mutex mutex_;

    static constexpr const char* MB_BASE_URL = "https://musicbrainz.org/ws/2";
    static constexpr const char* ACOUSTID_BASE_URL = "https://api.acoustid.org/v2";
};

} // namespace BeatMate::Services::Network
