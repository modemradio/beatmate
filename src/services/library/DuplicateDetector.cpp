#include "DuplicateDetector.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

DuplicateDetector::DuplicateDetector(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

std::vector<DuplicatePair> DuplicateDetector::findDuplicates(DuplicateMethod method,
                                                              DuplicateProgressCallback callback) {
    spdlog::info("DuplicateDetector: Starting duplicate detection with method {}",
                 static_cast<int>(method));

    switch (method) {
        case DuplicateMethod::ByFilename:
            return findByFilename();
        case DuplicateMethod::ByFingerprint:
            return findByFingerprint();
        case DuplicateMethod::ByMetadata:
        default:
            return findByMetadata();
    }
}

std::vector<DuplicatePair> DuplicateDetector::findByFilename() {
    std::vector<DuplicatePair> duplicates;
    if (!database_) {
        spdlog::debug("DuplicateDetector: No database, skipping findByFilename");
        return duplicates;
    }
    auto tracks = database_->getAllTracks();

    std::map<std::string, std::vector<Models::Track>> groups;
    for (const auto& track : tracks) {
        std::string filename = fs::path(track.filePath).stem().string();
        std::string normalized = normalizeString(filename);
        groups[normalized].push_back(track);
    }

    for (const auto& [key, group] : groups) {
        if (group.size() < 2) continue;
        for (size_t i = 0; i < group.size(); ++i) {
            for (size_t j = i + 1; j < group.size(); ++j) {
                DuplicatePair pair;
                pair.track1 = group[i];
                pair.track2 = group[j];
                pair.method = DuplicateMethod::ByFilename;
                pair.confidence = 0.9f;
                duplicates.push_back(pair);
            }
        }
    }

    spdlog::info("DuplicateDetector: Found {} filename duplicates", duplicates.size());
    return duplicates;
}

std::vector<DuplicatePair> DuplicateDetector::findByFingerprint() {
    std::vector<DuplicatePair> duplicates;
    if (!database_) {
        spdlog::debug("DuplicateDetector: No database, skipping findByFingerprint");
        return duplicates;
    }
    auto tracks = database_->getAllTracks();

    std::map<int64_t, std::vector<Models::Track>> sizeGroups;
    for (const auto& track : tracks) {
        sizeGroups[track.fileSize].push_back(track);
    }

    for (const auto& [size, group] : sizeGroups) {
        if (group.size() < 2) continue;
        for (size_t i = 0; i < group.size(); ++i) {
            for (size_t j = i + 1; j < group.size(); ++j) {
                if (std::abs(group[i].duration - group[j].duration) < durationTolerance_) {
                    DuplicatePair pair;
                    pair.track1 = group[i];
                    pair.track2 = group[j];
                    pair.method = DuplicateMethod::ByFingerprint;
                    pair.confidence = 0.8f;
                    duplicates.push_back(pair);
                }
            }
        }
    }

    spdlog::info("DuplicateDetector: Found {} fingerprint-based duplicates", duplicates.size());
    return duplicates;
}

std::vector<DuplicatePair> DuplicateDetector::findByMetadata() {
    std::vector<DuplicatePair> duplicates;
    if (!database_) {
        spdlog::debug("DuplicateDetector: No database, skipping findByMetadata");
        return duplicates;
    }
    auto tracks = database_->getAllTracks();

    for (size_t i = 0; i < tracks.size(); ++i) {
        for (size_t j = i + 1; j < tracks.size(); ++j) {
            const auto& a = tracks[i];
            const auto& b = tracks[j];

            float titleSim = calculateSimilarity(normalizeString(a.title), normalizeString(b.title));
            float artistSim = calculateSimilarity(normalizeString(a.artist), normalizeString(b.artist));

            bool durationMatch = std::abs(a.duration - b.duration) < durationTolerance_;

            float confidence = (titleSim * 0.5f + artistSim * 0.3f + (durationMatch ? 0.2f : 0.0f));

            if (confidence >= titleThreshold_) {
                DuplicatePair pair;
                pair.track1 = a;
                pair.track2 = b;
                pair.method = DuplicateMethod::ByMetadata;
                pair.confidence = confidence;
                duplicates.push_back(pair);
            }
        }
    }

    spdlog::info("DuplicateDetector: Found {} metadata duplicates", duplicates.size());
    return duplicates;
}

std::optional<DuplicateDetector::LibraryMatch> DuplicateDetector::findMatchForCandidate(
    const Models::Track& candidate,
    const std::vector<Models::Track>& librarySnapshot) const {

    const std::string candTitle = normalizeString(candidate.title);
    const std::string candArtist = normalizeString(candidate.artist);
    std::string candFileName;
    {
        const auto slash = candidate.filePath.find_last_of("/\\");
        candFileName = normalizeString(slash == std::string::npos
                                           ? candidate.filePath
                                           : candidate.filePath.substr(slash + 1));
    }

    std::optional<LibraryMatch> best;

    for (const auto& existing : librarySnapshot) {
        if (!candidate.filePath.empty() && existing.filePath == candidate.filePath)
            continue;

        if (!candFileName.empty()) {
            const auto slash = existing.filePath.find_last_of("/\\");
            const std::string existingFileName = normalizeString(
                slash == std::string::npos ? existing.filePath
                                           : existing.filePath.substr(slash + 1));
            if (existingFileName == candFileName) {
                if (!best || best->confidence < 0.9f)
                    best = LibraryMatch{ existing, 0.9f, DuplicateMethod::ByFilename };
                continue;
            }
        }

        if (candTitle.empty())
            continue;

        const float titleSim = calculateSimilarity(candTitle, normalizeString(existing.title));
        const float artistSim = calculateSimilarity(candArtist, normalizeString(existing.artist));
        const bool durationMatch = candidate.duration > 0.0 && existing.duration > 0.0
            && std::abs(candidate.duration - existing.duration) < durationTolerance_;
        const float confidence = titleSim * 0.5f + artistSim * 0.3f + (durationMatch ? 0.2f : 0.0f);

        if (confidence >= titleThreshold_ && (!best || confidence > best->confidence))
            best = LibraryMatch{ existing, confidence, DuplicateMethod::ByMetadata };
    }

    return best;
}

float DuplicateDetector::calculateSimilarity(const std::string& a, const std::string& b) const {
    if (a.empty() && b.empty()) return 1.0f;
    if (a.empty() || b.empty()) return 0.0f;
    if (a == b) return 1.0f;

    size_t m = a.size();
    size_t n = b.size();
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1));

    for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= n; ++j) dp[0][j] = j;

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost});
        }
    }

    size_t maxLen = std::max(m, n);
    return 1.0f - (static_cast<float>(dp[m][n]) / static_cast<float>(maxLen));
}

std::string DuplicateDetector::normalizeString(const std::string& str) const {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

} // namespace BeatMate::Services::Library
