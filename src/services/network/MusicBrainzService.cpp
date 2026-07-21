#include "MusicBrainzService.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Network {

MusicBrainzService::MusicBrainzService() {
    userAgent_ = "BeatMate/12.0.0 (contact@beatmate.fr)";
    httpClient_.setTimeout(15000);
    httpClient_.setMaxRetries(2);
    lastRequestTime_ = std::chrono::steady_clock::now() - std::chrono::seconds(2);
}

MusicBrainzService::~MusicBrainzService() {
    shuttingDown_ = true;
    std::vector<Worker> workers;
    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        workers.swap(workers_);
    }
    for (auto& w : workers) {
        if (w.thread.joinable()) w.thread.join();
    }
}

void MusicBrainzService::launchWorker(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(workersMutex_);
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (it->done->load()) {
            if (it->thread.joinable()) it->thread.join();
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    Worker w;
    w.done = done;
    w.thread = std::thread([job = std::move(job), done]() {
        job();
        done->store(true);
    });
    workers_.push_back(std::move(w));
}

void MusicBrainzService::searchByTitle(const std::string& title, const std::string& artist,
                                        RecordingCallback callback) {
    launchWorker([this, title, artist, callback]() {
        if (shuttingDown_) return;
        auto results = searchSync(title, artist);
        std::string error = results.empty() ? "No results found" : "";
        if (!shuttingDown_) callback(results, error);
    });
}

void MusicBrainzService::searchByISRC(const std::string& isrc, RecordingCallback callback) {
    launchWorker([this, isrc, callback]() {
        if (shuttingDown_) return;
        enforceRateLimit();
        std::string url = std::string(MB_BASE_URL) + "/isrc/" + urlEncode(isrc) + "?fmt=json&inc=recordings+artists";
        auto response = httpClient_.get(url, {{"User-Agent", userAgent_}});
        std::vector<MusicBrainzRecording> results;
        std::string error;
        if (response.success) {
            try {
                auto j = nlohmann::json::parse(response.body);
                if (j.contains("recordings") && j["recordings"].is_array()) {
                    for (auto& rj : j["recordings"]) {
                        results.push_back(parseRecording(rj));
                    }
                }
            } catch (const std::exception& e) { error = e.what(); }
        } else { error = response.error; }
        if (!shuttingDown_) callback(results, error);
    });
}

void MusicBrainzService::searchByBarcode(const std::string& barcode, ReleaseCallback callback) {
    launchWorker([this, barcode, callback]() {
        if (shuttingDown_) return;
        enforceRateLimit();
        std::string url = std::string(MB_BASE_URL) + "/release?query=barcode:" + urlEncode(barcode) + "&fmt=json";
        auto response = httpClient_.get(url, {{"User-Agent", userAgent_}});
        std::vector<MusicBrainzRelease> results;
        std::string error;
        if (response.success) {
            try {
                auto j = nlohmann::json::parse(response.body);
                if (j.contains("releases") && j["releases"].is_array()) {
                    for (auto& rj : j["releases"]) {
                        results.push_back(parseRelease(rj));
                    }
                }
            } catch (const std::exception& e) { error = e.what(); }
        } else { error = response.error; }
        if (!shuttingDown_) callback(results, error);
    });
}

void MusicBrainzService::lookupRecording(const std::string& mbid, RecordingCallback callback) {
    launchWorker([this, mbid, callback]() {
        if (shuttingDown_) return;
        enforceRateLimit();
        std::string url = std::string(MB_BASE_URL) + "/recording/" + urlEncode(mbid) + "?fmt=json&inc=artists+releases+tags+genres+isrcs";
        auto response = httpClient_.get(url, {{"User-Agent", userAgent_}});
        std::vector<MusicBrainzRecording> results;
        std::string error;
        if (response.success) {
            try {
                auto j = nlohmann::json::parse(response.body);
                results.push_back(parseRecording(j));
            } catch (const std::exception& e) { error = e.what(); }
        } else { error = response.error; }
        if (!shuttingDown_) callback(results, error);
    });
}

void MusicBrainzService::lookupRelease(const std::string& mbid, ReleaseCallback callback) {
    launchWorker([this, mbid, callback]() {
        if (shuttingDown_) return;
        enforceRateLimit();
        std::string url = std::string(MB_BASE_URL) + "/release/" + urlEncode(mbid) + "?fmt=json&inc=artists+recordings+labels";
        auto response = httpClient_.get(url, {{"User-Agent", userAgent_}});
        std::vector<MusicBrainzRelease> results;
        std::string error;
        if (response.success) {
            try {
                auto j = nlohmann::json::parse(response.body);
                results.push_back(parseRelease(j));
            } catch (const std::exception& e) { error = e.what(); }
        } else { error = response.error; }
        if (!shuttingDown_) callback(results, error);
    });
}

void MusicBrainzService::lookupByFingerprint(const std::string& fingerprint, int durationSecs,
                                               RecordingCallback callback) {
    if (acoustIdApiKey_.empty()) {
        callback({}, "AcoustID API key not set");
        return;
    }
    launchWorker([this, fingerprint, durationSecs, callback]() {
        if (shuttingDown_) return;
        std::string url = std::string(ACOUSTID_BASE_URL) + "/lookup?client=" + urlEncode(acoustIdApiKey_)
            + "&duration=" + std::to_string(durationSecs)
            + "&fingerprint=" + urlEncode(fingerprint)
            + "&meta=recordings+releasegroups+compress";
        auto response = httpClient_.get(url, {{"User-Agent", userAgent_}});
        std::vector<MusicBrainzRecording> results;
        std::string error;
        if (response.success) {
            try {
                auto j = nlohmann::json::parse(response.body);
                if (j.contains("results") && j["results"].is_array()) {
                    for (auto& r : j["results"]) {
                        if (r.contains("recordings") && r["recordings"].is_array()) {
                            for (auto& rec : r["recordings"]) {
                                MusicBrainzRecording mbr;
                                mbr.mbid = rec.value("id", "");
                                mbr.title = rec.value("title", "");
                                if (rec.contains("artists") && rec["artists"].is_array() && !rec["artists"].empty()) {
                                    mbr.artist = rec["artists"][0].value("name", "");
                                }
                                mbr.durationMs = rec.value("duration", 0) * 1000;
                                mbr.score = static_cast<int>(r.value("score", 0.0f) * 100);
                                results.push_back(mbr);
                            }
                        }
                    }
                }
            } catch (const std::exception& e) { error = e.what(); }
        } else { error = response.error; }
        if (!shuttingDown_) callback(results, error);
    });
}

std::vector<MusicBrainzRecording> MusicBrainzService::searchSync(const std::string& title,
                                                                   const std::string& artist) {
    enforceRateLimit();
    std::string query = "recording:" + urlEncode(title);
    if (!artist.empty()) query += "+AND+artist:" + urlEncode(artist);
    std::string url = std::string(MB_BASE_URL) + "/recording?query=" + query + "&fmt=json&limit=10";

    auto response = httpClient_.get(url, {{"User-Agent", userAgent_}});
    std::vector<MusicBrainzRecording> results;
    if (response.success) {
        try {
            auto j = nlohmann::json::parse(response.body);
            if (j.contains("recordings") && j["recordings"].is_array()) {
                for (auto& rj : j["recordings"]) {
                    results.push_back(parseRecording(rj));
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("MusicBrainzService: parse error: {}", e.what());
        }
    }
    return results;
}

void MusicBrainzService::setRateLimit(int requestsPerSecond) {
    rateLimitMs_ = requestsPerSecond > 0 ? (1000 / requestsPerSecond) : 1100;
}

void MusicBrainzService::setUserAgent(const std::string& appName, const std::string& version,
                                       const std::string& contact) {
    userAgent_ = appName + "/" + version + " (" + contact + ")";
}

void MusicBrainzService::setAcoustIDApiKey(const std::string& key) {
    acoustIdApiKey_ = key;
}

MusicBrainzRecording MusicBrainzService::parseRecording(const nlohmann::json& j) const {
    MusicBrainzRecording r;
    r.mbid = j.value("id", "");
    r.title = j.value("title", "");
    r.score = j.value("score", 0);
    r.durationMs = j.value("length", 0);

    if (j.contains("artist-credit") && j["artist-credit"].is_array() && !j["artist-credit"].empty()) {
        r.artist = j["artist-credit"][0].value("name", "");
        if (j["artist-credit"][0].contains("artist")) {
            r.artist = j["artist-credit"][0]["artist"].value("name", r.artist);
        }
    }

    if (j.contains("releases") && j["releases"].is_array() && !j["releases"].empty()) {
        r.album = j["releases"][0].value("title", "");
        r.releaseDate = j["releases"][0].value("date", "");
    }

    if (j.contains("isrcs") && j["isrcs"].is_array() && !j["isrcs"].empty()) {
        r.isrc = j["isrcs"][0].get<std::string>();
    }

    if (j.contains("tags") && j["tags"].is_array()) {
        for (auto& t : j["tags"]) {
            r.tags.push_back(t.value("name", ""));
        }
        if (!r.tags.empty()) r.genre = r.tags.front();
    }

    return r;
}

MusicBrainzRelease MusicBrainzService::parseRelease(const nlohmann::json& j) const {
    MusicBrainzRelease r;
    r.mbid = j.value("id", "");
    r.title = j.value("title", "");
    r.date = j.value("date", "");
    r.country = j.value("country", "");
    r.barcode = j.value("barcode", "");
    r.trackCount = j.value("track-count", 0);

    if (j.contains("artist-credit") && j["artist-credit"].is_array() && !j["artist-credit"].empty()) {
        r.artist = j["artist-credit"][0].value("name", "");
    }

    if (j.contains("label-info") && j["label-info"].is_array() && !j["label-info"].empty()) {
        auto& li = j["label-info"][0];
        if (li.contains("label")) r.label = li["label"].value("name", "");
    }

    return r;
}

std::string MusicBrainzService::urlEncode(const std::string& str) const {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (auto c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') escaped << c;
        else escaped << '%' << std::setw(2) << int((unsigned char)c);
    }
    return escaped.str();
}

void MusicBrainzService::enforceRateLimit() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequestTime_).count();
    if (elapsed < rateLimitMs_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(rateLimitMs_ - elapsed));
    }
    lastRequestTime_ = std::chrono::steady_clock::now();
}

} // namespace BeatMate::Services::Network
