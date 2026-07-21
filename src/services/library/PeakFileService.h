#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <map>
#include <unordered_map>
#include <list>
#include <optional>
#include <functional>
#include <cstdint>
#include <thread>
#include <atomic>

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

class TrackDatabase;

struct PeakData {
    int64_t trackId = 0;
    std::string filePath;

    std::vector<float> peaksPositive;
    std::vector<float> peaksNegative;
    std::vector<float> rms;

    std::vector<float> lowFreq;
    std::vector<float> midFreq;
    std::vector<float> highFreq;

    int segmentCount = 0;
    int samplesPerSegment = 0;
    int sampleRate = 44100;
    int channels = 2;
    double duration = 0.0;
    int64_t fileModifiedAt = 0;

    static constexpr uint32_t MAGIC = 0x424D504B; // "BMPK"
    static constexpr uint16_t VERSION = 4;

    bool isValid() const { return segmentCount > 0 && !peaksPositive.empty(); }
};

struct PeakConfig {
    std::string cacheDirectory;          // dossier des fichiers .bmpk
    int segmentsPerTrack = 2000;
    bool generateColorData = true;
    bool useCache = true;
};

using PeakProgressCallback = std::function<void(int processed, int total)>;

class PeakFileService {
public:
    PeakFileService();
    explicit PeakFileService(std::shared_ptr<TrackDatabase> database);
    ~PeakFileService();

    bool initialize(const PeakConfig& config = {});

    std::optional<PeakData> generatePeaks(const std::string& audioFilePath, int64_t trackId = 0);

    std::optional<PeakData> getPeaks(int64_t trackId);
    std::optional<PeakData> getPeaksByPath(const std::string& audioFilePath);

    int generatePeaksForAll(PeakProgressCallback progressCb = nullptr);
    int generatePeaksForTracks(const std::vector<int64_t>& trackIds,
                                 PeakProgressCallback progressCb = nullptr);

    bool hasCachedPeaks(int64_t trackId) const;
    bool hasCachedPeaks(const std::string& audioFilePath) const;
    bool removeCachedPeaks(int64_t trackId);
    int clearAllCache();
    int64_t getCacheSizeBytes() const;

    bool savePeakFile(const std::string& filePath, const PeakData& data);
    std::optional<PeakData> loadPeakFile(const std::string& filePath);

private:
    std::string getPeakFilePath(int64_t trackId) const;
    std::string getPeakFilePathForAudio(const std::string& audioPath) const;
    std::string getDefaultCacheDir() const;
    PeakData computePeaks(const std::string& audioFilePath, int segments) const;

    // exigent mutex_ tenu
    void touchLruLocked(int64_t trackId);
    void evictLruLocked();

    static int64_t getFileMtimeMillis(const std::string& audioFilePath);

    std::shared_ptr<TrackDatabase> database_;
    PeakConfig config_;
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> cancelRequested_{false};

    std::unordered_map<int64_t, PeakData> memoryCache_;
    std::list<int64_t> lruOrder_; // front = plus récemment utilisé
    std::unordered_map<int64_t, std::list<int64_t>::iterator> lruIter_;
    static constexpr int MAX_MEMORY_CACHE = 50;

    bool initialized_ = false;
};

} // namespace BeatMate::Services::Library
