#include "PlaylistSuggestionService.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <chrono>
#include <numeric>

namespace BeatMate::Services::Library {

static const std::map<std::string, std::vector<std::string>>& getCamelotCompat() {
    static const std::map<std::string, std::vector<std::string>> compat = {
        {"1A", {"1A","12A","2A","1B"}}, {"1B", {"1B","12B","2B","1A"}},
        {"2A", {"2A","1A","3A","2B"}},  {"2B", {"2B","1B","3B","2A"}},
        {"3A", {"3A","2A","4A","3B"}},  {"3B", {"3B","2B","4B","3A"}},
        {"4A", {"4A","3A","5A","4B"}},  {"4B", {"4B","3B","5B","4A"}},
        {"5A", {"5A","4A","6A","5B"}},  {"5B", {"5B","4B","6B","5A"}},
        {"6A", {"6A","5A","7A","6B"}},  {"6B", {"6B","5B","7B","6A"}},
        {"7A", {"7A","6A","8A","7B"}},  {"7B", {"7B","6B","8B","7A"}},
        {"8A", {"8A","7A","9A","8B"}},  {"8B", {"8B","7B","9B","8A"}},
        {"9A", {"9A","8A","10A","9B"}}, {"9B", {"9B","8B","10B","9A"}},
        {"10A",{"10A","9A","11A","10B"}},{"10B",{"10B","9B","11B","10A"}},
        {"11A",{"11A","10A","12A","11B"}},{"11B",{"11B","10B","12B","11A"}},
        {"12A",{"12A","11A","1A","12B"}},{"12B",{"12B","11B","1B","12A"}},
    };
    return compat;
}

PlaylistSuggestionService::PlaylistSuggestionService(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestForPlaylist(
    const std::vector<int64_t>& playlistTrackIds,
    const SuggestionCriteria& criteria) {

    if (!database_) return {};

    auto profile = analyzePlaylist(playlistTrackIds);

    std::set<int64_t> excludeSet(playlistTrackIds.begin(), playlistTrackIds.end());
    excludeSet.insert(criteria.excludeTrackIds.begin(), criteria.excludeTrackIds.end());

    auto allTracks = database_->getAllTracks();

    std::vector<TrackSuggestion> suggestions;

    for (const auto& track : allTracks) {
        if (excludeSet.count(track.id) > 0) continue;

        float score = 0.0f;
        std::string reason;
        std::vector<std::string> tags;

        double targetBPM = criteria.targetBPM.value_or(profile.averageBPM);
        if (targetBPM > 0 && track.bpm > 0) {
            float bpmScore = calculateBPMCompatibility(track.bpm, targetBPM);
            score += bpmScore * 0.3f;
            if (bpmScore > 0.7f) tags.push_back("bpm-match");
        }

        std::string targetKey = criteria.targetKey.value_or(profile.dominantKey);
        if (!targetKey.empty() && !track.camelotKey.empty()) {
            float keyScore = calculateKeyCompatibility(track.camelotKey, targetKey);
            score += keyScore * 0.25f;
            if (keyScore > 0.7f) tags.push_back("key-compatible");
        }

        float targetEnergy = criteria.targetEnergy.value_or(profile.averageEnergy);
        if (targetEnergy > 0 && track.energy > 0) {
            float energyScore = calculateEnergyCompatibility(track.energy, targetEnergy);
            score += energyScore * 0.2f;
            if (energyScore > 0.7f) tags.push_back("energy-match");
        }

        std::string targetGenre = criteria.preferredGenre.value_or(profile.dominantGenre);
        if (!targetGenre.empty() && !track.genre.empty()) {
            float genreScore = calculateGenreCompatibility(track.genre, targetGenre);
            score += genreScore * 0.15f;
            if (genreScore > 0.7f) tags.push_back("genre-match");
        }

        if (track.rating > 0) {
            score += (track.rating / 5.0f) * 0.1f;
        }

        if (score >= criteria.minimumScore) {
            if (!tags.empty()) {
                reason = "Matches: ";
                for (size_t i = 0; i < tags.size(); ++i) {
                    if (i > 0) reason += ", ";
                    reason += tags[i];
                }
            }

            TrackSuggestion suggestion;
            suggestion.track = track;
            suggestion.score = score;
            suggestion.reason = reason;
            suggestion.tags = tags;
            suggestions.push_back(std::move(suggestion));
        }
    }

    std::sort(suggestions.begin(), suggestions.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    int maxResults = criteria.maxSuggestions > 0 ? criteria.maxSuggestions : 20;
    if (static_cast<int>(suggestions.size()) > maxResults) {
        suggestions.resize(static_cast<size_t>(maxResults));
    }

    spdlog::debug("PlaylistSuggestionService: Generated {} suggestions for playlist ({} tracks)",
                  suggestions.size(), playlistTrackIds.size());
    return suggestions;
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestNextTrack(int64_t currentTrackId, int maxSuggestions) {
    return suggestSimilar(currentTrackId, maxSuggestions);
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestSimilar(int64_t trackId, int maxSuggestions) {
    if (!database_) return {};

    auto refOpt = database_->getTrack(trackId);
    if (!refOpt.has_value()) return {};

    const auto& ref = refOpt.value();
    auto allTracks = database_->getAllTracks();

    std::vector<TrackSuggestion> suggestions;

    for (const auto& track : allTracks) {
        if (track.id == trackId) continue;

        float score = calculateSimilarityScore(ref, track);

        if (score > 0.3f) {
            TrackSuggestion s;
            s.track = track;
            s.score = score;

            std::vector<std::string> reasons;
            if (calculateBPMCompatibility(ref.bpm, track.bpm) > 0.7f) reasons.push_back("similar BPM");
            if (isKeyCompatible(ref.camelotKey, track.camelotKey)) reasons.push_back("compatible key");
            if (ref.genre == track.genre && !ref.genre.empty()) reasons.push_back("same genre");
            if (calculateEnergyCompatibility(ref.energy, track.energy) > 0.7f) reasons.push_back("similar energy");

            s.reason = "";
            for (size_t i = 0; i < reasons.size(); ++i) {
                if (i > 0) s.reason += ", ";
                s.reason += reasons[i];
            }

            suggestions.push_back(std::move(s));
        }
    }

    std::sort(suggestions.begin(), suggestions.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    if (static_cast<int>(suggestions.size()) > maxSuggestions) {
        suggestions.resize(static_cast<size_t>(maxSuggestions));
    }

    return suggestions;
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestByBPMProgression(
    double startBPM, double endBPM, int numTracks) {

    if (!database_ || numTracks <= 0) return {};

    auto allTracks = database_->getAllTracks();
    double step = (endBPM - startBPM) / numTracks;

    std::vector<TrackSuggestion> suggestions;

    for (int i = 0; i < numTracks; ++i) {
        double targetBPM = startBPM + step * i;
        Models::Track bestTrack;
        float bestDist = 999.0f;

        std::set<int64_t> usedIds;
        for (const auto& s : suggestions) usedIds.insert(s.track.id);

        for (const auto& track : allTracks) {
            if (track.bpm <= 0 || usedIds.count(track.id) > 0) continue;

            float dist = static_cast<float>(std::abs(track.bpm - targetBPM));
            if (dist < bestDist) {
                bestDist = dist;
                bestTrack = track;
            }
        }

        if (bestTrack.id > 0) {
            TrackSuggestion s;
            s.track = bestTrack;
            s.score = 1.0f - std::min(bestDist / 20.0f, 1.0f);
            s.reason = "BPM progression target: " + std::to_string(static_cast<int>(targetBPM));
            suggestions.push_back(std::move(s));
        }
    }

    return suggestions;
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestByEnergyCurve(
    const std::vector<float>& energyCurve, int tracksPerSegment) {

    if (!database_ || energyCurve.empty()) return {};

    auto allTracks = database_->getAllTracks();
    std::vector<TrackSuggestion> suggestions;
    std::set<int64_t> usedIds;

    for (float targetEnergy : energyCurve) {
        std::vector<std::pair<float, const Models::Track*>> candidates;

        for (const auto& track : allTracks) {
            if (track.energy <= 0 || usedIds.count(track.id) > 0) continue;
            float dist = std::abs(track.energy - targetEnergy);
            candidates.emplace_back(dist, &track);
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        int added = 0;
        for (const auto& [dist, trackPtr] : candidates) {
            if (added >= tracksPerSegment) break;

            TrackSuggestion s;
            s.track = *trackPtr;
            s.score = 1.0f - std::min(dist / 5.0f, 1.0f);
            s.reason = "Energy curve target: " + std::to_string(static_cast<int>(targetEnergy));
            suggestions.push_back(std::move(s));
            usedIds.insert(trackPtr->id);
            added++;
        }
    }

    return suggestions;
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestForGenreMix(const std::string& genre, int count) {
    SuggestionCriteria criteria;
    criteria.preferredGenre = genre;
    criteria.maxSuggestions = count;
    criteria.minimumScore = 0.1f;
    return suggestForPlaylist({}, criteria);
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestForMoodMix(const std::string& mood, int count) {
    if (!database_) return {};

    auto allTracks = database_->getAllTracks();
    std::vector<TrackSuggestion> suggestions;

    std::string moodLower = mood;
    std::transform(moodLower.begin(), moodLower.end(), moodLower.begin(), ::tolower);

    for (const auto& track : allTracks) {
        std::string trackMood = track.mood;
        std::transform(trackMood.begin(), trackMood.end(), trackMood.begin(), ::tolower);

        if (!trackMood.empty() && trackMood.find(moodLower) != std::string::npos) {
            TrackSuggestion s;
            s.track = track;
            s.score = 0.8f;
            s.reason = "Mood: " + mood;
            s.tags = {"mood-match"};
            suggestions.push_back(std::move(s));
        }
    }

    std::sort(suggestions.begin(), suggestions.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    if (static_cast<int>(suggestions.size()) > count) {
        suggestions.resize(static_cast<size_t>(count));
    }

    return suggestions;
}

PlaylistProfile PlaylistSuggestionService::analyzePlaylist(const std::vector<int64_t>& trackIds) {
    PlaylistProfile profile;
    if (!database_ || trackIds.empty()) return profile;

    profile.trackCount = static_cast<int>(trackIds.size());

    double bpmSum = 0, bpmMin = 9999, bpmMax = 0;
    float energySum = 0;
    int bpmCount = 0, energyCount = 0;
    std::map<std::string, int> genres, keys, moods;

    for (auto id : trackIds) {
        auto trackOpt = database_->getTrack(id);
        if (!trackOpt.has_value()) continue;
        const auto& track = trackOpt.value();

        profile.totalDuration += track.duration;

        if (track.bpm > 0) {
            bpmSum += track.bpm;
            bpmMin = std::min(bpmMin, track.bpm);
            bpmMax = std::max(bpmMax, track.bpm);
            bpmCount++;
        }

        if (track.energy > 0) {
            energySum += track.energy;
            energyCount++;
        }

        if (!track.genre.empty()) genres[track.genre]++;
        std::string k = !track.camelotKey.empty() ? track.camelotKey : track.key;
        if (!k.empty()) keys[k]++;
        if (!track.mood.empty()) moods[track.mood]++;
    }

    profile.averageBPM = bpmCount > 0 ? bpmSum / bpmCount : 0.0;
    profile.bpmRange = bpmMax > bpmMin ? bpmMax - bpmMin : 0.0;
    profile.averageEnergy = energyCount > 0 ? energySum / energyCount : 0.0f;
    profile.genreDistribution = genres;
    profile.keyDistribution = keys;

    auto findDominant = [](const std::map<std::string, int>& m) -> std::string {
        std::string best;
        int maxCount = 0;
        for (const auto& [val, count] : m) {
            if (count > maxCount) { maxCount = count; best = val; }
        }
        return best;
    };

    profile.dominantGenre = findDominant(genres);
    profile.dominantKey = findDominant(keys);
    profile.dominantMood = findDominant(moods);

    return profile;
}

std::vector<TrackSuggestion> PlaylistSuggestionService::suggestFillToDuration(
    const std::vector<int64_t>& currentTrackIds, double targetDurationMinutes) {

    if (!database_) return {};

    double currentDuration = 0.0;
    for (auto id : currentTrackIds) {
        auto t = database_->getTrack(id);
        if (t.has_value()) currentDuration += t->duration;
    }

    double remainingSeconds = (targetDurationMinutes * 60.0) - currentDuration;
    if (remainingSeconds <= 0) return {};

    SuggestionCriteria criteria;
    criteria.excludeTrackIds = currentTrackIds;
    auto profile = analyzePlaylist(currentTrackIds);
    criteria.targetBPM = profile.averageBPM;
    criteria.targetEnergy = profile.averageEnergy;
    criteria.preferredGenre = profile.dominantGenre;
    criteria.maxSuggestions = 100;

    auto allSuggestions = suggestForPlaylist(currentTrackIds, criteria);

    std::vector<TrackSuggestion> result;
    double filled = 0.0;

    for (const auto& s : allSuggestions) {
        if (filled >= remainingSeconds) break;
        result.push_back(s);
        filled += s.track.duration;
    }

    spdlog::debug("PlaylistSuggestionService: Suggested {} tracks to fill {:.1f} minutes",
                  result.size(), remainingSeconds / 60.0);
    return result;
}


float PlaylistSuggestionService::calculateSimilarityScore(const Models::Track& ref,
                                                            const Models::Track& candidate) const {
    float score = 0.0f;
    int factors = 0;

    if (ref.bpm > 0 && candidate.bpm > 0) {
        score += calculateBPMCompatibility(ref.bpm, candidate.bpm) * 0.3f;
        factors++;
    }

    if (!ref.camelotKey.empty() && !candidate.camelotKey.empty()) {
        score += calculateKeyCompatibility(ref.camelotKey, candidate.camelotKey) * 0.25f;
        factors++;
    }

    if (ref.energy > 0 && candidate.energy > 0) {
        score += calculateEnergyCompatibility(ref.energy, candidate.energy) * 0.2f;
        factors++;
    }

    if (!ref.genre.empty() && !candidate.genre.empty()) {
        score += calculateGenreCompatibility(ref.genre, candidate.genre) * 0.15f;
        factors++;
    }

    if (candidate.rating > 0) {
        score += (candidate.rating / 5.0f) * 0.1f;
    }

    return score;
}

float PlaylistSuggestionService::calculateBPMCompatibility(double bpm1, double bpm2) const {
    if (bpm1 <= 0 || bpm2 <= 0) return 0.0f;
    double diff = std::abs(bpm1 - bpm2);
    double halfDiff = std::abs(bpm1 - bpm2 * 2.0);
    double doubleDiff = std::abs(bpm1 * 2.0 - bpm2);
    diff = std::min({diff, halfDiff, doubleDiff});

    if (diff < 1.0) return 1.0f;
    if (diff < 3.0) return 0.9f;
    if (diff < 5.0) return 0.7f;
    if (diff < 10.0) return 0.4f;
    if (diff < 20.0) return 0.2f;
    return 0.0f;
}

float PlaylistSuggestionService::calculateKeyCompatibility(const std::string& key1,
                                                             const std::string& key2) const {
    if (key1.empty() || key2.empty()) return 0.0f;
    if (key1 == key2) return 1.0f;
    return isKeyCompatible(key1, key2) ? 0.8f : 0.1f;
}

float PlaylistSuggestionService::calculateEnergyCompatibility(float e1, float e2) const {
    float diff = std::abs(e1 - e2);
    if (diff < 0.5f) return 1.0f;
    if (diff < 1.0f) return 0.8f;
    if (diff < 2.0f) return 0.6f;
    if (diff < 3.0f) return 0.3f;
    return 0.1f;
}

float PlaylistSuggestionService::calculateGenreCompatibility(const std::string& g1,
                                                               const std::string& g2) const {
    if (g1 == g2) return 1.0f;

    std::string l1 = g1, l2 = g2;
    std::transform(l1.begin(), l1.end(), l1.begin(), ::tolower);
    std::transform(l2.begin(), l2.end(), l2.begin(), ::tolower);
    if (l1 == l2) return 1.0f;

    if (l1.find(l2) != std::string::npos || l2.find(l1) != std::string::npos) return 0.7f;

    auto isElectronic = [](const std::string& g) {
        return g.find("house") != std::string::npos || g.find("techno") != std::string::npos ||
               g.find("trance") != std::string::npos || g.find("edm") != std::string::npos ||
               g.find("electro") != std::string::npos || g.find("drum") != std::string::npos;
    };
    auto isUrban = [](const std::string& g) {
        return g.find("hip") != std::string::npos || g.find("rap") != std::string::npos ||
               g.find("r&b") != std::string::npos || g.find("rnb") != std::string::npos ||
               g.find("trap") != std::string::npos;
    };
    auto isRock = [](const std::string& g) {
        return g.find("rock") != std::string::npos || g.find("metal") != std::string::npos ||
               g.find("punk") != std::string::npos || g.find("indie") != std::string::npos;
    };

    if ((isElectronic(l1) && isElectronic(l2)) ||
        (isUrban(l1) && isUrban(l2)) ||
        (isRock(l1) && isRock(l2))) {
        return 0.5f;
    }

    return 0.1f;
}

bool PlaylistSuggestionService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    auto& compat = getCamelotCompat();
    auto it = compat.find(key1);
    if (it != compat.end()) {
        return std::find(it->second.begin(), it->second.end(), key2) != it->second.end();
    }
    return false;
}

} // namespace BeatMate::Services::Library
