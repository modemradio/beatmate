#include "WaveformCacheService.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace BeatMate::Core {

WaveformCacheService::WaveformCacheService() = default;
WaveformCacheService::~WaveformCacheService() = default;

std::string WaveformCacheService::sanitizeFilename(const std::string& trackId) {
    std::string result;
    for (char c : trackId) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            result += c;
        } else {
            result += '_';
        }
    }
    if (result.size() > 200) {
        size_t hash = std::hash<std::string>{}(trackId);
        result = std::to_string(hash);
    }
    return result;
}

std::string WaveformCacheService::getCachePath(const std::string& trackId, const std::string& cacheDir) {
    return cacheDir + "/" + sanitizeFilename(trackId) + ".bmpk";
}

bool WaveformCacheService::save(const std::string& trackId, const ColouredWaveformData& data,
                                  const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        fs::create_directories(cacheDir);
        std::string path = getCachePath(trackId, cacheDir);

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            spdlog::error("WaveformCacheService: cannot create {}", path);
            return false;
        }

        file.write(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC));
        file.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));

        int32_t numPoints = static_cast<int32_t>(data.points.size());
        int32_t numDetailed = static_cast<int32_t>(data.detailedPoints.size());
        int32_t resolution = data.resolution;
        int32_t detailedRes = data.detailedResolution;
        int32_t sampleRate = data.sampleRate;
        double duration = data.duration;

        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));
        file.write(reinterpret_cast<const char*>(&numDetailed), sizeof(numDetailed));
        file.write(reinterpret_cast<const char*>(&resolution), sizeof(resolution));
        file.write(reinterpret_cast<const char*>(&detailedRes), sizeof(detailedRes));
        file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
        file.write(reinterpret_cast<const char*>(&duration), sizeof(duration));

        for (auto& pt : data.points) {
            file.write(reinterpret_cast<const char*>(&pt.amplitude), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.low), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.mid), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.high), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.color.r), 1);
            file.write(reinterpret_cast<const char*>(&pt.color.g), 1);
            file.write(reinterpret_cast<const char*>(&pt.color.b), 1);
            file.write(reinterpret_cast<const char*>(&pt.color.a), 1);
        }

        for (auto& pt : data.detailedPoints) {
            file.write(reinterpret_cast<const char*>(&pt.amplitude), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.low), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.mid), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.high), sizeof(float));
            file.write(reinterpret_cast<const char*>(&pt.color.r), 1);
            file.write(reinterpret_cast<const char*>(&pt.color.g), 1);
            file.write(reinterpret_cast<const char*>(&pt.color.b), 1);
            file.write(reinterpret_cast<const char*>(&pt.color.a), 1);
        }

        file.close();

        spdlog::debug("WaveformCacheService: saved {} ({} + {} points)", path, numPoints, numDetailed);

        enforceMaxCacheSize(cacheDir);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("WaveformCacheService: save error: {}", e.what());
        return false;
    }
}

bool WaveformCacheService::load(const std::string& trackId, ColouredWaveformData& data,
                                  const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string path = getCachePath(trackId, cacheDir);

    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t magic, version;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (magic != MAGIC || version > VERSION) {
            spdlog::warn("WaveformCacheService: invalid cache file {}", path);
            return false;
        }

        int32_t numPoints, numDetailed, resolution, detailedRes, sampleRate;
        double duration;

        file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));
        file.read(reinterpret_cast<char*>(&numDetailed), sizeof(numDetailed));
        file.read(reinterpret_cast<char*>(&resolution), sizeof(resolution));
        file.read(reinterpret_cast<char*>(&detailedRes), sizeof(detailedRes));
        file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
        file.read(reinterpret_cast<char*>(&duration), sizeof(duration));

        data.resolution = resolution;
        data.detailedResolution = detailedRes;
        data.sampleRate = sampleRate;
        data.duration = duration;

        data.points.resize(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            file.read(reinterpret_cast<char*>(&data.points[i].amplitude), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.points[i].low), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.points[i].mid), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.points[i].high), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.points[i].color.r), 1);
            file.read(reinterpret_cast<char*>(&data.points[i].color.g), 1);
            file.read(reinterpret_cast<char*>(&data.points[i].color.b), 1);
            file.read(reinterpret_cast<char*>(&data.points[i].color.a), 1);
        }

        data.detailedPoints.resize(numDetailed);
        for (int i = 0; i < numDetailed; ++i) {
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].amplitude), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].low), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].mid), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].high), sizeof(float));
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].color.r), 1);
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].color.g), 1);
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].color.b), 1);
            file.read(reinterpret_cast<char*>(&data.detailedPoints[i].color.a), 1);
        }

        spdlog::debug("WaveformCacheService: loaded {} ({} + {} points)", path, numPoints, numDetailed);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("WaveformCacheService: load error: {}", e.what());
        return false;
    }
}

bool WaveformCacheService::exists(const std::string& trackId, const std::string& cacheDir) {
    return fs::exists(getCachePath(trackId, cacheDir));
}

bool WaveformCacheService::remove(const std::string& trackId, const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return fs::remove(getCachePath(trackId, cacheDir));
    } catch (...) {
        return false;
    }
}

void WaveformCacheService::clearAll(const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        for (auto& entry : fs::directory_iterator(cacheDir)) {
            if (entry.path().extension() == ".bmpk") {
                fs::remove(entry.path());
            }
        }
        spdlog::info("WaveformCacheService: cleared cache in {}", cacheDir);
    } catch (const std::exception& e) {
        spdlog::error("WaveformCacheService: clearAll error: {}", e.what());
    }
}

size_t WaveformCacheService::getCacheSize(const std::string& cacheDir) {
    size_t totalSize = 0;
    try {
        for (auto& entry : fs::directory_iterator(cacheDir)) {
            if (entry.path().extension() == ".bmpk") {
                totalSize += entry.file_size();
            }
        }
    } catch (...) {}
    return totalSize;
}

void WaveformCacheService::enforceMaxCacheSize(const std::string& cacheDir) {
    size_t currentSize = getCacheSize(cacheDir);
    if (currentSize <= maxCacheSize_) return;

    struct CacheFile {
        fs::path path;
        std::filesystem::file_time_type lastWrite;
        size_t size;
    };

    std::vector<CacheFile> files;
    try {
        for (auto& entry : fs::directory_iterator(cacheDir)) {
            if (entry.path().extension() == ".bmpk") {
                files.push_back({entry.path(), entry.last_write_time(), entry.file_size()});
            }
        }
    } catch (...) { return; }

    std::sort(files.begin(), files.end(),
              [](const CacheFile& a, const CacheFile& b) { return a.lastWrite < b.lastWrite; });

    for (auto& f : files) {
        if (currentSize <= maxCacheSize_) break;
        try {
            fs::remove(f.path);
            currentSize -= f.size;
            spdlog::debug("WaveformCacheService: evicted {}", f.path.string());
        } catch (...) {}
    }
}

} // namespace BeatMate::Core
