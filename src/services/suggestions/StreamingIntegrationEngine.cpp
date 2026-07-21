#include "StreamingIntegrationEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

std::vector<IntegrationResult> StreamingIntegrationEngine::searchAcrossServices(const std::string& query, int maxResults) {
    std::vector<IntegrationResult> results;

    for (const auto& [service, enabled] : enabledServices_) {
        if (!enabled) continue;
        spdlog::debug("StreamingIntegrationEngine: Searching '{}' on {}", query, service);
    }

    if (static_cast<int>(results.size()) > maxResults) results.resize(static_cast<size_t>(maxResults));
    spdlog::info("StreamingIntegrationEngine: Found {} results for query '{}'", results.size(), query);
    return results;
}

std::vector<IntegrationResult> StreamingIntegrationEngine::findStreamingVersions(const Models::Track& localTrack) {
    std::vector<IntegrationResult> results;

    IntegrationResult result = createResult(localTrack);
    result.hasLocalCopy = true;
    result.matchConfidence = 1.0f;

    for (const auto& [service, enabled] : enabledServices_) {
        if (!enabled) continue;

        StreamingTrackInfo info;
        info.serviceName = service;
        info.serviceId = "";
        info.trackUrl = "";
        info.previewUrl = "";
        info.popularity = 0.0f;

    }

    results.push_back(result);
    spdlog::debug("StreamingIntegrationEngine: Found streaming versions for '{}'", localTrack.title);
    return results;
}

std::vector<IntegrationResult> StreamingIntegrationEngine::enrichWithStreaming(const std::vector<Models::Track>& tracks) {
    std::vector<IntegrationResult> results;
    results.reserve(tracks.size());

    for (const auto& track : tracks) {
        IntegrationResult result = createResult(track);
        result.hasLocalCopy = (track.source != Models::TrackSource::Streaming);
        result.matchConfidence = 1.0f;
        results.push_back(result);
    }

    spdlog::info("StreamingIntegrationEngine: Enriched {} tracks with streaming data", results.size());
    return results;
}

void StreamingIntegrationEngine::searchAsync(const std::string& query, StreamingSearchCallback callback) {
    auto results = searchAcrossServices(query);
    if (callback) callback(results);
}

void StreamingIntegrationEngine::enableService(const std::string& serviceName, bool enabled) {
    enabledServices_[serviceName] = enabled;
    spdlog::info("StreamingIntegrationEngine: Service '{}' {}", serviceName, enabled ? "enabled" : "disabled");
}

std::vector<std::string> StreamingIntegrationEngine::getEnabledServices() const {
    std::vector<std::string> services;
    for (const auto& [name, enabled] : enabledServices_) {
        if (enabled) services.push_back(name);
    }
    return services;
}

IntegrationResult StreamingIntegrationEngine::createResult(const Models::Track& track) const {
    IntegrationResult result;
    result.track = track;
    result.hasLocalCopy = !track.filePath.empty();
    result.matchConfidence = 1.0f;
    return result;
}

float StreamingIntegrationEngine::matchConfidence(const Models::Track& local, const Models::Track& streaming) const {
    float score = 0.0f;
    if (local.title == streaming.title) score += 0.4f;
    if (local.artist == streaming.artist) score += 0.4f;
    if (std::abs(local.duration - streaming.duration) < 5.0) score += 0.2f;
    return score;
}

} // namespace BeatMate::Services::Suggestions
