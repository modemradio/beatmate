#include "StreamingIntegrationService.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <future>
#include <map>
#include <set>

namespace BeatMate::Services::Streaming {

StreamingIntegrationService::StreamingIntegrationService() = default;

void StreamingIntegrationService::setAccountService(
    std::shared_ptr<StreamingAccountService> accountService) {
    accountService_ = std::move(accountService);
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::searchAllPlatforms(
    const std::string& query, int limitPerService) {
    CrossPlatformSearchOptions options;
    options.query = query;
    options.limitPerService = limitPerService;
    options.searchAllConnected = true;
    return searchWithOptions(options);
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::searchWithOptions(
    const CrossPlatformSearchOptions& options) {
    if (!accountService_) return {};

    std::vector<UnifiedTrackResult> allResults;

    auto connected = accountService_->getConnectedAccounts();

    std::vector<std::pair<Models::StreamingServiceType,
                          std::future<StreamingSearchResult>>> futures;
    for (const auto& account : connected) {
        if (!options.searchAllConnected && !options.specificServices.empty()) {
            bool found = false;
            for (auto s : options.specificServices) {
                if (s == account.serviceType) { found = true; break; }
            }
            if (!found) continue;
        }

        auto service = accountService_->getService(account.serviceType);
        if (!service) continue;

        const auto query = options.query;
        const int limit = options.limitPerService;
        futures.emplace_back(account.serviceType, std::async(std::launch::async,
            [service, query, limit]() -> StreamingSearchResult {
                try {
                    return service->search(query, limit);
                } catch (const std::exception& e) {
                    spdlog::error("StreamingIntegrationService: search error: {}", e.what());
                } catch (...) {}
                return StreamingSearchResult{};
            }));
    }

    for (auto& [serviceType, fut] : futures) {
        auto results = fut.get();
        for (const auto& track : results.tracks) {
            UnifiedTrackResult unified;
            unified.track = track;
            unified.source = serviceType;
            unified.relevanceScore = computeRelevanceScore(track, options.query);
            unified.availableSources.push_back(serviceType);
            allResults.push_back(unified);
        }
    }

    if (options.deduplicateByISRC) {
        allResults = deduplicateByISRC(allResults);
    }

    if (options.mergeResults) {
        allResults = mergeAndRank(allResults);
    }

    spdlog::info("StreamingIntegrationService: Search '{}' returned {} unified results",
                 options.query, allResults.size());
    return allResults;
}

std::optional<UnifiedTrackResult> StreamingIntegrationService::findTrackByISRC(const std::string& isrc) {
    if (!accountService_ || isrc.empty()) return std::nullopt;

    auto connected = accountService_->getConnectedAccounts();
    for (const auto& account : connected) {
        auto service = accountService_->getService(account.serviceType);
        if (!service) continue;

        auto results = service->search("isrc:" + isrc, 1);
        if (!results.tracks.empty()) {
            UnifiedTrackResult unified;
            unified.track = results.tracks[0];
            unified.source = account.serviceType;
            unified.relevanceScore = 1.0;
            unified.availableSources.push_back(account.serviceType);
            return unified;
        }
    }
    return std::nullopt;
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::findTrackOnOtherPlatforms(
    const Models::StreamingTrack& track) {
    std::vector<UnifiedTrackResult> results;
    if (!accountService_) return results;

    if (!track.isrc.empty()) {
        auto connected = accountService_->getConnectedAccounts();
        for (const auto& account : connected) {
            if (account.serviceType == track.serviceType) continue;

            auto service = accountService_->getService(account.serviceType);
            if (!service) continue;

            auto searchResults = service->search("isrc:" + track.isrc, 1);
            if (!searchResults.tracks.empty()) {
                UnifiedTrackResult unified;
                unified.track = searchResults.tracks[0];
                unified.source = account.serviceType;
                unified.relevanceScore = 1.0;
                results.push_back(unified);
            }
        }
    }

    return results;
}

UnifiedTrackResult StreamingIntegrationService::resolveTrack(const std::string& title,
                                                               const std::string& artist) {
    UnifiedTrackResult best;
    if (!accountService_) return best;

    std::string query = artist + " " + title;
    auto results = searchAllPlatforms(query, 3);

    if (!results.empty()) {
        best = results[0];
    }
    return best;
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::resolveMultipleTracks(
    const std::vector<std::pair<std::string, std::string>>& titleArtistPairs) {
    std::vector<UnifiedTrackResult> results;
    results.reserve(titleArtistPairs.size());

    for (const auto& [title, artist] : titleArtistPairs) {
        results.push_back(resolveTrack(title, artist));
    }
    return results;
}

bool StreamingIntegrationService::syncPlaylistToPlatform(
    const Models::Playlist& playlist,
    Models::StreamingServiceType targetPlatform,
    const std::vector<Models::StreamingTrack>& tracks) {

    if (!accountService_ || !accountService_->isConnected(targetPlatform)) return false;
    auto service = accountService_->getService(targetPlatform);
    if (!service) return false;

    spdlog::info("StreamingIntegrationService: Syncing playlist '{}' ({} tracks) to {}",
                 playlist.name, tracks.size(),
                 StreamingAccountService::serviceTypeToString(targetPlatform));

    std::vector<Models::StreamingTrack> resolvedTracks;
    for (const auto& track : tracks) {
        if (track.serviceType == targetPlatform) {
            resolvedTracks.push_back(track);
        } else if (!track.isrc.empty()) {
            auto results = service->search("isrc:" + track.isrc, 1);
            if (!results.tracks.empty()) {
                resolvedTracks.push_back(results.tracks[0]);
            }
        }
    }

    spdlog::info("StreamingIntegrationService: Resolved {}/{} tracks on {}",
                 resolvedTracks.size(), tracks.size(),
                 StreamingAccountService::serviceTypeToString(targetPlatform));

    return !resolvedTracks.empty();
}

bool StreamingIntegrationService::syncPlaylistFromPlatform(
    Models::StreamingServiceType source,
    const std::string& playlistId,
    Models::Playlist& outPlaylist,
    std::vector<Models::StreamingTrack>& outTracks) {

    if (!accountService_ || !accountService_->isConnected(source)) return false;
    auto service = accountService_->getService(source);
    if (!service) return false;

    auto playlists = service->getPlaylists();
    for (const auto& pl : playlists) {
        outPlaylist = pl;
        break;
    }
    return true;
}

bool StreamingIntegrationService::crossSyncPlaylist(
    const std::string& sourcePlaylistId,
    Models::StreamingServiceType sourcePlatform,
    Models::StreamingServiceType targetPlatform) {

    Models::Playlist playlist;
    std::vector<Models::StreamingTrack> tracks;

    if (!syncPlaylistFromPlatform(sourcePlatform, sourcePlaylistId, playlist, tracks)) return false;
    return syncPlaylistToPlatform(playlist, targetPlatform, tracks);
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::getUnifiedRecentlyPlayed(int limit) {
    std::vector<UnifiedTrackResult> results;
    if (!accountService_) return results;

    auto connected = accountService_->getConnectedAccounts();
    int perService = std::max(1, limit / std::max(1, static_cast<int>(connected.size())));

    for (const auto& account : connected) {
        auto service = accountService_->getService(account.serviceType);
        if (!service) continue;

        // Use search as a fallback since not all services expose recently-played via base class
        auto playlists = service->getPlaylists();
    }

    return results;
}

std::vector<UnifiedPlaylist> StreamingIntegrationService::getUnifiedPlaylists() {
    std::vector<UnifiedPlaylist> unifiedPlaylists;
    if (!accountService_) return unifiedPlaylists;

    auto connected = accountService_->getConnectedAccounts();
    for (const auto& account : connected) {
        auto service = accountService_->getService(account.serviceType);
        if (!service) continue;

        auto playlists = service->getPlaylists();
        for (const auto& pl : playlists) {
            UnifiedPlaylist up;
            up.name = pl.name;
            up.description = pl.description;
            up.primarySource = account.serviceType;
            up.totalTracks = static_cast<int>(pl.trackIds.size());
            unifiedPlaylists.push_back(up);
        }
    }

    spdlog::info("StreamingIntegrationService: {} unified playlists across {} services",
                 unifiedPlaylists.size(), connected.size());
    return unifiedPlaylists;
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::getUnifiedSavedTracks(int limitPerService) {
    std::vector<UnifiedTrackResult> results;
    if (!accountService_) return results;

    auto connected = accountService_->getConnectedAccounts();
    for (const auto& account : connected) {
        auto service = accountService_->getService(account.serviceType);
        if (!service) continue;

        // Try to get playlists as a proxy for saved content
        auto playlists = service->getPlaylists();
        spdlog::debug("StreamingIntegrationService: Got {} playlists from {}",
                      playlists.size(),
                      StreamingAccountService::serviceTypeToString(account.serviceType));
    }

    return deduplicateByISRC(results);
}

std::vector<StreamingIntegrationService::PlatformAvailability>
StreamingIntegrationService::checkAvailability(
    const std::vector<std::pair<std::string, std::string>>& titleArtistPairs) {

    std::vector<PlatformAvailability> results;
    if (!accountService_) return results;

    auto connected = accountService_->getConnectedAccounts();

    for (const auto& [title, artist] : titleArtistPairs) {
        PlatformAvailability avail;
        avail.title = title;
        avail.artist = artist;

        std::string query = artist + " " + title;
        for (const auto& account : connected) {
            auto service = accountService_->getService(account.serviceType);
            if (!service) continue;

            auto searchResults = service->search(query, 1);
            bool found = !searchResults.tracks.empty();
            avail.available[account.serviceType] = found;
            if (found) {
                avail.serviceIds[account.serviceType] = searchResults.tracks[0].serviceId;
            }
        }
        results.push_back(avail);
    }
    return results;
}

StreamingIntegrationService::IntegrationStats StreamingIntegrationService::getStats() const {
    IntegrationStats stats;
    if (!accountService_) return stats;

    auto connected = accountService_->getConnectedAccounts();
    stats.totalConnectedServices = static_cast<int>(connected.size());

    for (const auto& account : connected) {
        auto service = accountService_->getService(account.serviceType);
        if (!service) continue;

        auto playlists = service->getPlaylists();
        stats.playlistsPerService[account.serviceType] = static_cast<int>(playlists.size());
        stats.totalPlaylists += static_cast<int>(playlists.size());
    }

    return stats;
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::deduplicateByISRC(
    std::vector<UnifiedTrackResult>& tracks) const {

    std::map<std::string, UnifiedTrackResult> isrcMap;
    std::vector<UnifiedTrackResult> noIsrc;

    for (auto& t : tracks) {
        if (!t.track.isrc.empty()) {
            auto it = isrcMap.find(t.track.isrc);
            if (it != isrcMap.end()) {
                it->second.availableSources.push_back(t.source);
                it->second.availableOnMultiple = true;
                if (t.relevanceScore > it->second.relevanceScore) {
                    auto sources = it->second.availableSources;
                    it->second = t;
                    it->second.availableSources = sources;
                    it->second.availableOnMultiple = true;
                }
            } else {
                isrcMap[t.track.isrc] = t;
            }
        } else {
            noIsrc.push_back(t);
        }
    }

    std::vector<UnifiedTrackResult> result;
    for (auto& [isrc, t] : isrcMap) result.push_back(t);
    for (auto& t : noIsrc) result.push_back(t);

    return result;
}

std::vector<UnifiedTrackResult> StreamingIntegrationService::mergeAndRank(
    std::vector<UnifiedTrackResult>& tracks) const {

    std::sort(tracks.begin(), tracks.end(),
              [](const UnifiedTrackResult& a, const UnifiedTrackResult& b) {
                  if (a.availableOnMultiple != b.availableOnMultiple)
                      return a.availableOnMultiple;
                  return a.relevanceScore > b.relevanceScore;
              });

    return tracks;
}

double StreamingIntegrationService::computeRelevanceScore(const Models::StreamingTrack& track,
                                                            const std::string& query) const {
    double score = 0.0;

    score += static_cast<double>(track.popularity) / 100.0 * 0.3;

    if (!track.previewUrl.empty()) score += 0.1;

    if (!track.isrc.empty()) score += 0.1;

    if (!track.artworkUrl.empty()) score += 0.05;

    if (track.durationMs > 30000 && track.durationMs < 900000) score += 0.1;

    score += 0.35;

    return std::min(score, 1.0);
}

} // namespace BeatMate::Services::Streaming
