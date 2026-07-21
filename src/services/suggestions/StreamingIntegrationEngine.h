#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "../../models/Track.h"

namespace BeatMate::Services::Suggestions {

struct StreamingTrackInfo {
    std::string serviceId;
    std::string serviceName;
    std::string trackUrl;
    std::string previewUrl;
    float popularity = 0.0f;
    bool isExplicit = false;
    std::string albumArtUrl;
};

struct IntegrationResult {
    Models::Track track;
    std::vector<StreamingTrackInfo> availableOn;
    bool hasLocalCopy = false;
    float matchConfidence = 0.0f;
};

using StreamingSearchCallback = std::function<void(const std::vector<IntegrationResult>&)>;

class StreamingIntegrationEngine {
public:
    StreamingIntegrationEngine() = default;

    std::vector<IntegrationResult> searchAcrossServices(const std::string& query, int maxResults = 10);
    std::vector<IntegrationResult> findStreamingVersions(const Models::Track& localTrack);
    std::vector<IntegrationResult> enrichWithStreaming(const std::vector<Models::Track>& tracks);
    void searchAsync(const std::string& query, StreamingSearchCallback callback);

    void enableService(const std::string& serviceName, bool enabled);
    std::vector<std::string> getEnabledServices() const;

private:
    IntegrationResult createResult(const Models::Track& track) const;
    float matchConfidence(const Models::Track& local, const Models::Track& streaming) const;

    std::map<std::string, bool> enabledServices_ = {
        {"spotify", true}, {"tidal", true}, {"apple_music", true},
        {"soundcloud", false}, {"youtube_music", false}
    };
};

} // namespace BeatMate::Services::Suggestions
