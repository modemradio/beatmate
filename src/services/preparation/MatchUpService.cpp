#include "MatchUpService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Preparation {

MatchUpResult MatchUpService::matchTracks(const Models::Track& a, const Models::Track& b) {
    MatchUpResult result;
    result.trackA = a;
    result.trackB = b;

    MatchDetail bpmDetail, keyDetail, energyDetail, genreDetail, moodDetail, timbreDetail;

    float bpmScore = computeBpmMatch(a, b, bpmDetail);
    float keyScore = computeKeyMatch(a, b, keyDetail);
    float energyScore = computeEnergyMatch(a, b, energyDetail);
    float genreScore = computeGenreMatch(a, b, genreDetail);
    float moodScore = computeMoodMatch(a, b, moodDetail);
    float timbreScore = computeTimbreMatch(a, b, timbreDetail);

    result.details = {bpmDetail, keyDetail, energyDetail, genreDetail, moodDetail, timbreDetail};

    result.overallScore = bpmScore * 0.25f + keyScore * 0.25f + energyScore * 0.2f +
                          genreScore * 0.15f + moodScore * 0.1f + timbreScore * 0.05f;

    result.verdict = verdictFromScore(result.overallScore);
    result.suggestedTransition = suggestTransitionType(result);

    if (a.bpm > 0) {
        double beatsPerSec = a.bpm / 60.0;
        result.suggestedMixPoint = 32.0 / beatsPerSec;
        result.mixDurationBeats = (result.overallScore > 0.7f) ? 32.0f : 16.0f;
    }

    spdlog::debug("MatchUpService: '{}' vs '{}' = {:.0f}% ({})",
                  a.title, b.title, result.overallScore * 100.0f, result.verdict);
    return result;
}

float MatchUpService::quickScore(const Models::Track& a, const Models::Track& b) {
    MatchDetail dummy;
    float bpm = computeBpmMatch(a, b, dummy);
    float key = computeKeyMatch(a, b, dummy);
    float energy = computeEnergyMatch(a, b, dummy);
    return bpm * 0.35f + key * 0.35f + energy * 0.3f;
}

std::string MatchUpService::suggestTransitionType(const MatchUpResult& match) {
    if (match.overallScore >= 0.8f) {
        for (const auto& d : match.details) {
            if (d.criterion == "Key" && d.score >= 0.8f) return "Harmonic Mix";
        }
        return "EQ Blend";
    }
    if (match.overallScore >= 0.6f) return "Filter Sweep";
    if (match.overallScore >= 0.4f) return "Echo Out";
    return "Cut";
}

std::vector<MatchUpResult> MatchUpService::findBestMatches(
    const Models::Track& reference, const std::vector<Models::Track>& pool, int topN) {

    std::vector<MatchUpResult> results;
    for (const auto& track : pool) {
        if (track.id == reference.id) continue;
        results.push_back(matchTracks(reference, track));
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.overallScore > b.overallScore; });

    if (static_cast<int>(results.size()) > topN) results.resize(static_cast<size_t>(topN));

    spdlog::info("MatchUpService: Found {} best matches for '{}'", results.size(), reference.title);
    return results;
}

float MatchUpService::computeBpmMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const {
    detail.criterion = "BPM";
    if (a.bpm <= 0 || b.bpm <= 0) {
        detail.score = 0.5f;
        detail.explanation = "BPM data missing";
        detail.recommendation = "Analyze tracks first";
        return 0.5f;
    }

    double diff = std::abs(a.bpm - b.bpm);
    if (diff <= 1.0) {
        detail.score = 1.0f;
        detail.explanation = "Same BPM (" + std::to_string(static_cast<int>(a.bpm)) + " vs " + std::to_string(static_cast<int>(b.bpm)) + ")";
        detail.recommendation = "Direct beat-match";
    } else if (diff <= 4.0) {
        detail.score = 0.85f;
        detail.explanation = "Very close BPM (diff: " + std::to_string(static_cast<int>(diff)) + ")";
        detail.recommendation = "Easy tempo sync";
    } else if (diff <= 8.0) {
        detail.score = 0.6f;
        detail.explanation = "Moderate BPM difference (diff: " + std::to_string(static_cast<int>(diff)) + ")";
        detail.recommendation = "Use tempo fader gradually";
    } else {
        double halfDiff = std::abs(a.bpm - b.bpm * 2.0);
        double doubleDiff = std::abs(a.bpm * 2.0 - b.bpm);
        if (halfDiff <= 4.0 || doubleDiff <= 4.0) {
            detail.score = 0.7f;
            detail.explanation = "Half/double time compatible";
            detail.recommendation = "Use half-time transition";
        } else {
            detail.score = std::max(0.0f, static_cast<float>(1.0 - diff / 30.0));
            detail.explanation = "Large BPM gap (" + std::to_string(static_cast<int>(diff)) + " BPM)";
            detail.recommendation = "Use cut or breakdown transition";
        }
    }
    return detail.score;
}

float MatchUpService::computeKeyMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const {
    detail.criterion = "Key";
    std::string key1 = a.camelotKey.empty() ? a.key : a.camelotKey;
    std::string key2 = b.camelotKey.empty() ? b.key : b.camelotKey;

    if (key1.empty() || key2.empty()) {
        detail.score = 0.5f;
        detail.explanation = "Key data missing";
        detail.recommendation = "Analyze tracks for key detection";
        return 0.5f;
    }

    std::string relation = keyRelation(key1, key2);
    if (relation == "same") {
        detail.score = 1.0f;
        detail.explanation = "Same key (" + key1 + ")";
        detail.recommendation = "Perfect harmonic mix";
    } else if (relation == "adjacent") {
        detail.score = 0.9f;
        detail.explanation = "Adjacent keys (" + key1 + " -> " + key2 + ")";
        detail.recommendation = "Smooth harmonic transition";
    } else if (relation == "parallel") {
        detail.score = 0.85f;
        detail.explanation = "Parallel keys (" + key1 + " -> " + key2 + ")";
        detail.recommendation = "Major/minor shift works well";
    } else {
        detail.score = 0.1f;
        detail.explanation = "Incompatible keys (" + key1 + " -> " + key2 + ")";
        detail.recommendation = "Avoid long harmonic overlap";
    }
    return detail.score;
}

float MatchUpService::computeEnergyMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const {
    detail.criterion = "Energy";
    float diff = std::abs(a.energy - b.energy);
    detail.score = std::max(0.0f, 1.0f - diff / 5.0f);
    detail.explanation = "Energy diff: " + std::to_string(static_cast<int>(diff * 10) / 10.0f).substr(0, 3);

    if (diff <= 1.0f) {
        detail.recommendation = "Smooth energy flow";
    } else if (diff <= 2.5f) {
        detail.recommendation = "Moderate energy shift - use buildup/breakdown";
    } else {
        detail.recommendation = "Large energy jump - consider adding bridge track";
    }
    return detail.score;
}

float MatchUpService::computeGenreMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const {
    detail.criterion = "Genre";
    if (a.genre.empty() || b.genre.empty()) {
        detail.score = 0.5f;
        detail.explanation = "Genre data missing";
        detail.recommendation = "Tag your tracks";
        return 0.5f;
    }
    detail.score = (a.genre == b.genre) ? 1.0f : 0.3f;
    detail.explanation = a.genre + " vs " + b.genre;
    detail.recommendation = (a.genre == b.genre) ? "Same genre - great flow" : "Genre change - use transition wisely";
    return detail.score;
}

float MatchUpService::computeMoodMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const {
    detail.criterion = "Mood";
    if (a.mood.empty() || b.mood.empty()) {
        detail.score = 0.5f;
        detail.explanation = "Mood data missing";
        return 0.5f;
    }
    detail.score = (a.mood == b.mood) ? 1.0f : 0.3f;
    detail.explanation = a.mood + " -> " + b.mood;
    detail.recommendation = (a.mood == b.mood) ? "Consistent mood" : "Mood shift";
    return detail.score;
}

float MatchUpService::computeTimbreMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const {
    detail.criterion = "Timbre";
    float danceabilityDiff = std::abs(a.danceability - b.danceability);
    detail.score = std::max(0.0f, 1.0f - danceabilityDiff * 2.0f);
    detail.explanation = "Danceability similarity";
    detail.recommendation = (detail.score > 0.7f) ? "Similar feel" : "Different textures";
    return detail.score;
}

std::string MatchUpService::verdictFromScore(float score) const {
    if (score >= 0.85f) return "Perfect";
    if (score >= 0.7f) return "Good";
    if (score >= 0.5f) return "Acceptable";
    if (score >= 0.3f) return "Poor";
    return "Incompatible";
}

bool MatchUpService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    return keyRelation(key1, key2) != "incompatible";
}

std::string MatchUpService::keyRelation(const std::string& key1, const std::string& key2) const {
    if (key1 == key2) return "same";
    if (key1.size() < 2 || key2.size() < 2) return "incompatible";
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back(); char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return "parallel";
        int diff = ((num2 - num1) + 12) % 12;
        if ((diff == 1 || diff == 11) && let1 == let2) return "adjacent";
    } catch (...) {}
    return "incompatible";
}

} // namespace BeatMate::Services::Preparation
