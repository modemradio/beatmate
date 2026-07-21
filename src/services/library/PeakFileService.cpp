#include "PeakFileService.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <functional>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

PeakFileService::PeakFileService() = default;

PeakFileService::PeakFileService(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

PeakFileService::~PeakFileService() {
    cancelRequested_ = true;
    if (worker_.joinable()) worker_.join();
}

bool PeakFileService::initialize(const PeakConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;

    if (config_.cacheDirectory.empty()) {
        config_.cacheDirectory = getDefaultCacheDir();
    }

    try {
        fs::create_directories(config_.cacheDirectory);
    } catch (const std::exception& e) {
        spdlog::error("PeakFileService: Failed to create cache dir: {}", e.what());
        return false;
    }

    int purged = 0;
    try {
        for (const auto& entry : fs::directory_iterator(config_.cacheDirectory)) {
            if (entry.path().extension() != ".bmpk") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            if (!f.is_open()) continue;
            uint32_t magic = 0;
            uint16_t version = 0;
            f.read(reinterpret_cast<char*>(&magic),   sizeof(uint32_t));
            f.read(reinterpret_cast<char*>(&version), sizeof(uint16_t));
            f.close();
            if (magic != PeakData::MAGIC || version != PeakData::VERSION) {
                fs::remove(entry.path());
                ++purged;
            }
        }
    } catch (...) {}
    if (purged > 0) {
        spdlog::info("PeakFileService: Purged {} stale cache file(s)", purged);
    }

    initialized_ = true;
    spdlog::info("PeakFileService: Initialized at '{}'", config_.cacheDirectory);
    return true;
}

std::optional<PeakData> PeakFileService::generatePeaks(const std::string& audioFilePath, int64_t trackId) {
    if (!fs::exists(audioFilePath)) {
        spdlog::warn("PeakFileService: File not found: {}", audioFilePath);
        return std::nullopt;
    }

    spdlog::debug("PeakFileService: Generating peaks for '{}'", audioFilePath);

    auto peakData = computePeaks(audioFilePath, config_.segmentsPerTrack);

    if (!peakData.isValid()) {
        spdlog::warn("PeakFileService: Failed to compute peaks for '{}'", audioFilePath);
        return std::nullopt;
    }

    peakData.trackId = trackId;
    peakData.filePath = audioFilePath;

    peakData.fileModifiedAt = getFileMtimeMillis(audioFilePath);

    if (config_.useCache && trackId > 0) {
        savePeakFile(getPeakFilePath(trackId), peakData);

        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(memoryCache_.size()) >= MAX_MEMORY_CACHE) {
            evictLruLocked();
        }
        memoryCache_[trackId] = peakData;
        touchLruLocked(trackId);
    }

    return peakData;
}

int64_t PeakFileService::getFileMtimeMillis(const std::string& audioFilePath) {
    try {
        auto ftime = fs::last_write_time(audioFilePath);
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
        auto sctp = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::clock_cast<std::chrono::system_clock>(ftime));
        const int64_t v = sctp.time_since_epoch().count();
        if (v > 0) return v;
#else
        auto sctp = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::file_clock::to_sys(ftime));
        const int64_t v = sctp.time_since_epoch().count();
        if (v > 0) return v;
#endif
    } catch (const std::exception& e) {
        spdlog::debug("PeakFileService: clock_cast failed ({}), falling back to JUCE mtime", e.what());
    } catch (...) {
    }
    const auto t = juce::File(audioFilePath).getLastModificationTime();
    if (t.toMilliseconds() > 0) return t.toMilliseconds();
    return juce::Time::currentTimeMillis();
}

std::optional<PeakData> PeakFileService::getPeaks(int64_t trackId) {
    std::string sourcePath;
    if (database_) {
        auto trackOpt = database_->getTrack(trackId);
        if (trackOpt.has_value()) sourcePath = trackOpt->filePath;
    }

    const int64_t currentMtime = sourcePath.empty()
        ? 0 : getFileMtimeMillis(sourcePath);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = memoryCache_.find(trackId);
        if (it != memoryCache_.end()) {
            const int64_t cachedMtime = it->second.fileModifiedAt;
            if (currentMtime == 0 || currentMtime <= cachedMtime) {
                touchLruLocked(trackId);
                return it->second;
            }
            memoryCache_.erase(it);
            auto lit = lruIter_.find(trackId);
            if (lit != lruIter_.end()) {
                lruOrder_.erase(lit->second);
                lruIter_.erase(lit);
            }
        }
    }

    if (config_.useCache) {
        auto diskData = loadPeakFile(getPeakFilePath(trackId));
        if (diskData.has_value()) {
            const int64_t cachedMtime = diskData->fileModifiedAt;
            if (currentMtime > cachedMtime && currentMtime > 0) {
                spdlog::debug("PeakFileService: cache stale for track {} (src {} > cache {}), regenerating",
                              trackId, currentMtime, cachedMtime);
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                if (static_cast<int>(memoryCache_.size()) >= MAX_MEMORY_CACHE) {
                    evictLruLocked();
                }
                memoryCache_[trackId] = diskData.value();
                touchLruLocked(trackId);
                return diskData;
            }
        }
    }

    if (!sourcePath.empty()) {
        return generatePeaks(sourcePath, trackId);
    }

    return std::nullopt;
}

void PeakFileService::touchLruLocked(int64_t trackId) {
    auto it = lruIter_.find(trackId);
    if (it != lruIter_.end()) {
        lruOrder_.erase(it->second);
    }
    lruOrder_.push_front(trackId);
    lruIter_[trackId] = lruOrder_.begin();
}

void PeakFileService::evictLruLocked() {
    if (lruOrder_.empty()) {
        if (!memoryCache_.empty()) memoryCache_.erase(memoryCache_.begin());
        return;
    }
    const int64_t victim = lruOrder_.back();
    lruOrder_.pop_back();
    lruIter_.erase(victim);
    memoryCache_.erase(victim);
}

std::optional<PeakData> PeakFileService::getPeaksByPath(const std::string& audioFilePath) {
    if (database_) {
        auto trackOpt = database_->getTrackByPath(audioFilePath);
        if (trackOpt.has_value()) {
            return getPeaks(trackOpt->id);
        }
    }

    return generatePeaks(audioFilePath, 0);
}

int PeakFileService::generatePeaksForAll(PeakProgressCallback progressCb) {
    if (!database_) return 0;

    if (worker_.joinable()) worker_.join();
    cancelRequested_ = false;

    auto self = this;
    auto cb   = std::move(progressCb);
    worker_ = std::thread([self, cb = std::move(cb)]() {
        try {
            auto allTracks = self->database_->getAllTracks();
            std::vector<int64_t> ids;
            ids.reserve(allTracks.size());
            for (const auto& t : allTracks) ids.push_back(t.id);

            auto safeCb = cb ? PeakProgressCallback(
                [cb](int processed, int total) {
                    juce::MessageManager::callAsync([cb, processed, total]() {
                        cb(processed, total);
                    });
                }) : PeakProgressCallback{};

            self->generatePeaksForTracks(ids, safeCb);
        } catch (const std::exception& e) {
            spdlog::error("PeakFileService::generatePeaksForAll worker: {}", e.what());
        } catch (...) {
            spdlog::error("PeakFileService::generatePeaksForAll worker: unknown exception");
        }
    });
    return 0;
}

int PeakFileService::generatePeaksForTracks(const std::vector<int64_t>& trackIds,
                                               PeakProgressCallback progressCb) {
    int generated = 0;
    int total = static_cast<int>(trackIds.size());

    for (size_t i = 0; i < trackIds.size(); ++i) {
        if (cancelRequested_) break;
        if (hasCachedPeaks(trackIds[i])) {
            if (progressCb) progressCb(static_cast<int>(i + 1), total);
            continue;
        }

        auto peaks = getPeaks(trackIds[i]);
        if (peaks.has_value()) generated++;

        if (progressCb) progressCb(static_cast<int>(i + 1), total);
    }

    spdlog::info("PeakFileService: Generated peaks for {} of {} tracks", generated, total);
    return generated;
}

bool PeakFileService::hasCachedPeaks(int64_t trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (memoryCache_.count(trackId) > 0) return true;
    return fs::exists(getPeakFilePath(trackId));
}

bool PeakFileService::hasCachedPeaks(const std::string& audioFilePath) const {
    return fs::exists(getPeakFilePathForAudio(audioFilePath));
}

bool PeakFileService::removeCachedPeaks(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    memoryCache_.erase(trackId);
    auto lit = lruIter_.find(trackId);
    if (lit != lruIter_.end()) {
        lruOrder_.erase(lit->second);
        lruIter_.erase(lit);
    }
    auto path = getPeakFilePath(trackId);
    try { return fs::remove(path); } catch (...) { return false; }
}

int PeakFileService::clearAllCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    int removed = 0;
    memoryCache_.clear();
    lruOrder_.clear();
    lruIter_.clear();

    try {
        for (const auto& entry : fs::directory_iterator(config_.cacheDirectory)) {
            if (entry.path().extension() == ".bmpk") {
                fs::remove(entry.path());
                removed++;
            }
        }
    } catch (...) {}

    spdlog::info("PeakFileService: Cleared {} cached peak files", removed);
    return removed;
}

int64_t PeakFileService::getCacheSizeBytes() const {
    int64_t size = 0;
    try {
        for (const auto& entry : fs::directory_iterator(config_.cacheDirectory)) {
            if (entry.path().extension() == ".bmpk") {
                size += static_cast<int64_t>(entry.file_size());
            }
        }
    } catch (...) {}
    return size;
}

bool PeakFileService::savePeakFile(const std::string& filePath, const PeakData& data) {
    try {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(&PeakData::MAGIC), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&PeakData::VERSION), sizeof(uint16_t));

        int32_t segCount = data.segmentCount;
        int32_t sps = data.samplesPerSegment;
        int32_t sr = data.sampleRate;
        int32_t ch = data.channels;
        double dur = data.duration;

        file.write(reinterpret_cast<const char*>(&segCount), sizeof(int32_t));
        file.write(reinterpret_cast<const char*>(&sps), sizeof(int32_t));
        file.write(reinterpret_cast<const char*>(&sr), sizeof(int32_t));
        file.write(reinterpret_cast<const char*>(&ch), sizeof(int32_t));
        file.write(reinterpret_cast<const char*>(&dur), sizeof(double));

        auto writeVector = [&file](const std::vector<float>& v) {
            int32_t size = static_cast<int32_t>(v.size());
            file.write(reinterpret_cast<const char*>(&size), sizeof(int32_t));
            if (size > 0) {
                file.write(reinterpret_cast<const char*>(v.data()), size * sizeof(float));
            }
        };

        writeVector(data.peaksPositive);
        writeVector(data.peaksNegative);
        writeVector(data.rms);
        writeVector(data.lowFreq);
        writeVector(data.midFreq);
        writeVector(data.highFreq);

        spdlog::debug("PeakFileService: Saved peak file '{}'", filePath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("PeakFileService: Failed to save peak file: {}", e.what());
        return false;
    }
}

std::optional<PeakData> PeakFileService::loadPeakFile(const std::string& filePath) {
    try {
        if (!fs::exists(filePath)) return std::nullopt;

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return std::nullopt;

        PeakData data;

        uint32_t magic = 0;
        uint16_t version = 0;
        file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&version), sizeof(uint16_t));

        if (magic != PeakData::MAGIC || version != PeakData::VERSION) {
            spdlog::debug("PeakFileService: Invalid peak file format: {}", filePath);
            return std::nullopt;
        }

        int32_t segCount, sps, sr, ch;
        file.read(reinterpret_cast<char*>(&segCount), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&sps), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&sr), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&ch), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&data.duration), sizeof(double));

        data.segmentCount = segCount;
        data.samplesPerSegment = sps;
        data.sampleRate = sr;
        data.channels = ch;

        auto readVector = [&file]() -> std::vector<float> {
            int32_t size = 0;
            file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
            if (size <= 0 || size > 1000000) return {};
            std::vector<float> v(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(v.data()), size * sizeof(float));
            return v;
        };

        data.peaksPositive = readVector();
        data.peaksNegative = readVector();
        data.rms = readVector();
        data.lowFreq = readVector();
        data.midFreq = readVector();
        data.highFreq = readVector();

        return data;
    } catch (const std::exception& e) {
        spdlog::debug("PeakFileService: Failed to load peak file '{}': {}", filePath, e.what());
        return std::nullopt;
    }
}

std::string PeakFileService::getPeakFilePath(int64_t trackId) const {
    return config_.cacheDirectory + "/peak_" + std::to_string(trackId) + ".bmpk";
}

std::string PeakFileService::getPeakFilePathForAudio(const std::string& audioPath) const {
    juce::String normalised(audioPath);
    normalised = normalised.replaceCharacter('\\', '/').toLowerCase();
    std::hash<std::string> hasher;
    auto hash = hasher(normalised.toStdString());
    return config_.cacheDirectory + "/peak_" + std::to_string(hash) + ".bmpk";
}

std::string PeakFileService::getDefaultCacheDir() const {
#ifdef _WIN32
    std::string appData = std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "";
    if (!appData.empty()) return appData + "/BeatMate/cache/peaks";
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) return home + "/.cache/beatmate/peaks";
#endif
    return "./beatmate_peaks";
}

namespace {
struct Biquad {
    double b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
    double z1{0}, z2{0};
    inline float process(float x) noexcept {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
    void setLowpass(double fc, double sr, double Q = 0.707) {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
        const double cosw = std::cos(w0);
        const double sinw = std::sin(w0);
        const double alpha = sinw / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        b0 = ((1.0 - cosw) / 2.0) / a0;
        b1 = (1.0 - cosw) / a0;
        b2 = ((1.0 - cosw) / 2.0) / a0;
        a1 = (-2.0 * cosw) / a0;
        a2 = (1.0 - alpha) / a0;
    }
    void setHighpass(double fc, double sr, double Q = 0.707) {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
        const double cosw = std::cos(w0);
        const double sinw = std::sin(w0);
        const double alpha = sinw / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        b0 = ((1.0 + cosw) / 2.0) / a0;
        b1 = -(1.0 + cosw) / a0;
        b2 = ((1.0 + cosw) / 2.0) / a0;
        a1 = (-2.0 * cosw) / a0;
        a2 = (1.0 - alpha) / a0;
    }
};
}

PeakData PeakFileService::computePeaks(const std::string& audioFilePath, int segments) const {
    PeakData result;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    juce::File audioFile(audioFilePath);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));

    if (!reader) {
        spdlog::warn("PeakFileService: Cannot read audio file: {}", audioFilePath);
        return result;
    }

    auto totalSamples = reader->lengthInSamples;
    auto sampleRate = static_cast<int>(reader->sampleRate);
    auto numChannels = static_cast<int>(reader->numChannels);

    if (totalSamples <= 0 || sampleRate <= 0) return result;

    result.sampleRate = sampleRate;
    result.channels = numChannels;
    result.duration = static_cast<double>(totalSamples) / sampleRate;
    result.samplesPerSegment = static_cast<int>(totalSamples / segments);
    result.segmentCount = segments;

    if (result.samplesPerSegment <= 0) result.samplesPerSegment = 1;

    result.peaksPositive.resize(static_cast<size_t>(segments), 0.0f);
    result.peaksNegative.resize(static_cast<size_t>(segments), 0.0f);
    result.rms.resize(static_cast<size_t>(segments), 0.0f);

    if (config_.generateColorData) {
        result.lowFreq.resize(static_cast<size_t>(segments), 0.0f);
        result.midFreq.resize(static_cast<size_t>(segments), 0.0f);
        result.highFreq.resize(static_cast<size_t>(segments), 0.0f);
    }

    Biquad lowLP_a, lowLP_b, highHP_a, highHP_b;
    lowLP_a.setLowpass (600.0,  (double) sampleRate);
    lowLP_b.setLowpass (600.0,  (double) sampleRate);
    highHP_a.setHighpass(4000.0, (double) sampleRate);
    highHP_b.setHighpass(4000.0, (double) sampleRate);

    const int chunkSize = result.samplesPerSegment;
    juce::AudioBuffer<float> buffer(numChannels, chunkSize);

    for (int seg = 0; seg < segments; ++seg) {
        int64_t startSample = static_cast<int64_t>(seg) * chunkSize;
        int samplesToRead = std::min(chunkSize, static_cast<int>(totalSamples - startSample));
        if (samplesToRead <= 0) break;

        buffer.clear();
        reader->read(&buffer, 0, samplesToRead, startSample, true, true);

        float peakMax = 0.0f, peakMin = 0.0f;
        float rmsSum = 0.0f;
        float lowPeak = 0.0f, midPeak = 0.0f, highPeak = 0.0f;

        const float* chL = buffer.getReadPointer(0);
        const float* chR = (numChannels > 1) ? buffer.getReadPointer(1) : chL;

        for (int s = 0; s < samplesToRead; ++s) {
            const float mono = 0.5f * (chL[s] + chR[s]);

            peakMax = std::max(peakMax, mono);
            peakMin = std::min(peakMin, mono);
            rmsSum += mono * mono;

            if (config_.generateColorData) {
                const float lo = lowLP_b.process (lowLP_a.process (mono));
                const float hi = highHP_b.process(highHP_a.process(mono));
                const float mi = mono - lo - hi;
                lowPeak  = std::max(lowPeak,  std::abs(lo));
                midPeak  = std::max(midPeak,  std::abs(mi));
                highPeak = std::max(highPeak, std::abs(hi));
            }
        }

        result.peaksPositive[static_cast<size_t>(seg)] = peakMax;
        result.peaksNegative[static_cast<size_t>(seg)] = peakMin;
        result.rms[static_cast<size_t>(seg)] = samplesToRead > 0
            ? std::sqrt(rmsSum / static_cast<float>(samplesToRead)) : 0.0f;

        if (config_.generateColorData) {
            result.lowFreq [static_cast<size_t>(seg)] = lowPeak;
            result.midFreq [static_cast<size_t>(seg)] = midPeak;
            result.highFreq[static_cast<size_t>(seg)] = highPeak;
        }
    }

    spdlog::info("PeakFileService: RGB waveform — {} segments, {} Hz, "
                 "band-split via biquad LP/HP (RGB band split) for '{}'",
                 segments, sampleRate, audioFilePath);
    return result;
}

}
