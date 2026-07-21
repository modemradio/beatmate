#pragma once

#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

enum class DuplicateMethod {
    ByFilename,
    ByFingerprint,
    ByMetadata
};

struct DuplicatePair {
    Models::Track track1;
    Models::Track track2;
    DuplicateMethod method;
    float confidence = 0.0f; // 0-1
};

using DuplicateProgressCallback = std::function<void(int processed, int total)>;

class DuplicateDetector {
public:
    explicit DuplicateDetector(std::shared_ptr<TrackDatabase> database);
    ~DuplicateDetector() = default;

    std::vector<DuplicatePair> findDuplicates(DuplicateMethod method = DuplicateMethod::ByMetadata,
                                               DuplicateProgressCallback callback = nullptr);

    std::vector<DuplicatePair> findByFilename();
    std::vector<DuplicatePair> findByFingerprint();
    std::vector<DuplicatePair> findByMetadata();

    struct LibraryMatch {
        Models::Track existing;
        float confidence = 0.0f;
        DuplicateMethod method = DuplicateMethod::ByMetadata;
    };
    std::optional<LibraryMatch> findMatchForCandidate(const Models::Track& candidate,
                                                      const std::vector<Models::Track>& librarySnapshot) const;

    void setTitleSimilarityThreshold(float threshold) { titleThreshold_ = threshold; }
    void setDurationToleranceSec(double tolerance) { durationTolerance_ = tolerance; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    float calculateSimilarity(const std::string& a, const std::string& b) const;
    std::string normalizeString(const std::string& str) const;

    std::shared_ptr<TrackDatabase> database_;
    float titleThreshold_ = 0.85f;
    double durationTolerance_ = 2.0; // seconds
    bool enabled_ = true;
};

} // namespace BeatMate::Services::Library
