#include "StemCache.h"
#include "../audio/AudioTrack.h"
#include "../audio/AudioFileReader.h"
#include "../audio/AudioFileWriter.h"
#include <filesystem>
#include <chrono>
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

namespace BeatMate::Core {
namespace fs = std::filesystem;

static std::string resolveCacheDir(const std::string& cacheDir) {
    if (! cacheDir.empty() && fs::path(cacheDir).is_absolute())
        return cacheDir;
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate");
    const auto leaf = cacheDir.empty() ? juce::String("StemsCache") : juce::String(cacheDir);
    return base.getChildFile(leaf).getFullPathName().toStdString();
}

StemCache::StemCache(const std::string& cacheDir) : cacheDir_(resolveCacheDir(cacheDir)) {
    std::error_code ec;
    fs::create_directories(cacheDir_, ec);
    if (ec) spdlog::warn("StemCache: could not create cache dir '{}': {}", cacheDir_, ec.message());
    ready_ = ! ec && fs::is_directory(cacheDir_, ec);
}

StemCache::~StemCache() = default;

bool StemCache::save(const std::string& trackId, const StemResult& stems) {
    if (!ready_) return false;
    auto dir = fs::path(cacheDir_) / trackId;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        spdlog::warn("StemCache: could not create dir '{}': {}", dir.string(), ec.message());
        return false;
    }

    AudioFileWriter writer;
    auto saveStem = [&](const std::shared_ptr<AudioTrack>& stem, const std::string& name) {
        if (stem) writer.writeWAV(*stem, (dir / (name + ".wav")).string());
    };

    saveStem(stems.vocals, "vocals");
    saveStem(stems.drums, "drums");
    saveStem(stems.bass, "bass");
    saveStem(stems.other, "other");

    spdlog::info("StemCache: saved stems for {}", trackId);
    evictOldEntries();
    return true;
}

std::optional<StemResult> StemCache::load(const std::string& trackId) {
    if (!ready_) return std::nullopt;
    auto dir = fs::path(cacheDir_) / trackId;
    if (!fs::exists(dir)) return std::nullopt;

    AudioFileReader reader;
    StemResult result;

    auto loadStem = [&](const std::string& name) -> std::shared_ptr<AudioTrack> {
        auto path = (dir / (name + ".wav")).string();
        if (fs::exists(path)) return reader.readFile(path);
        return nullptr;
    };

    result.vocals = loadStem("vocals");
    result.drums = loadStem("drums");
    result.bass = loadStem("bass");
    result.other = loadStem("other");
    result.success = (result.vocals || result.drums || result.bass || result.other);

    spdlog::info("StemCache: loaded stems for {}", trackId);
    return result;
}

bool StemCache::exists(const std::string& trackId) const {
    return fs::exists(fs::path(cacheDir_) / trackId);
}

void StemCache::remove(const std::string& trackId) {
    auto dir = fs::path(cacheDir_) / trackId;
    if (fs::exists(dir)) fs::remove_all(dir);
}

void StemCache::clear() {
    if (fs::exists(cacheDir_)) fs::remove_all(cacheDir_);
    fs::create_directories(cacheDir_);
}

int64_t StemCache::getCacheSizeBytes() const {
    int64_t total = 0;
    for (auto& entry : fs::recursive_directory_iterator(cacheDir_)) {
        if (entry.is_regular_file()) total += entry.file_size();
    }
    return total;
}

void StemCache::evictOldEntries() {
    auto now = fs::file_time_type::clock::now();
    auto maxAge = std::chrono::hours(24 * expirationDays_);

    for (auto& entry : fs::directory_iterator(cacheDir_)) {
        if (!entry.is_directory()) continue;
        auto lastWrite = fs::last_write_time(entry);
        if (now - lastWrite > maxAge) {
            fs::remove_all(entry);
            spdlog::info("StemCache: evicted old entry {}", entry.path().filename().string());
        }
    }

    while (getCacheSizeBytes() > maxCacheBytes_) {
        fs::file_time_type oldest = fs::file_time_type::max();
        fs::path oldestPath;
        for (auto& entry : fs::directory_iterator(cacheDir_)) {
            if (!entry.is_directory()) continue;
            auto t = fs::last_write_time(entry);
            if (t < oldest) { oldest = t; oldestPath = entry.path(); }
        }
        if (!oldestPath.empty()) {
            fs::remove_all(oldestPath);
            spdlog::info("StemCache: evicted for size: {}", oldestPath.filename().string());
        } else break;
    }
}

} // namespace BeatMate::Core
