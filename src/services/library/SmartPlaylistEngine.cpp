#include "SmartPlaylistEngine.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

std::string SmartPlaylistEngine::getFieldValue(const Models::Track& track, Models::SmartPlaylistField field)
{
    switch (field) {
        case Models::SmartPlaylistField::Title:      return track.title;
        case Models::SmartPlaylistField::Artist:     return track.artist;
        case Models::SmartPlaylistField::Album:      return track.album;
        case Models::SmartPlaylistField::Genre:      return track.genre;
        case Models::SmartPlaylistField::Key:        return track.key;
        case Models::SmartPlaylistField::CamelotKey: return track.camelotKey;
        case Models::SmartPlaylistField::Comment:    return track.comment;
        case Models::SmartPlaylistField::Label:      return track.label;
        default: return "";
    }
}

double SmartPlaylistEngine::getFieldNumericValue(const Models::Track& track, Models::SmartPlaylistField field)
{
    switch (field) {
        case Models::SmartPlaylistField::BPM:        return track.bpm;
        case Models::SmartPlaylistField::Energy:     return track.energy;
        case Models::SmartPlaylistField::Rating:     return track.rating;
        case Models::SmartPlaylistField::Year:       return track.year;
        case Models::SmartPlaylistField::Duration:   return track.duration;
        case Models::SmartPlaylistField::BitRate:    return track.bitRate;
        case Models::SmartPlaylistField::SampleRate: return track.sampleRate;
        default: return 0.0;
    }
}

bool SmartPlaylistEngine::isNumericField(Models::SmartPlaylistField field)
{
    switch (field) {
        case Models::SmartPlaylistField::BPM:
        case Models::SmartPlaylistField::Energy:
        case Models::SmartPlaylistField::Rating:
        case Models::SmartPlaylistField::Year:
        case Models::SmartPlaylistField::Duration:
        case Models::SmartPlaylistField::BitRate:
        case Models::SmartPlaylistField::SampleRate:
        case Models::SmartPlaylistField::Danceability:
        case Models::SmartPlaylistField::PlayCount:
            return true;
        default:
            return false;
    }
}

bool SmartPlaylistEngine::evaluateString(const std::string& trackValue,
                                           Models::SmartPlaylistOperator op,
                                           const std::string& ruleValue,
                                           const std::string& /*ruleValue2*/)
{
    std::string tvLower = trackValue, rvLower = ruleValue;
    std::transform(tvLower.begin(), tvLower.end(), tvLower.begin(), ::tolower);
    std::transform(rvLower.begin(), rvLower.end(), rvLower.begin(), ::tolower);

    switch (op) {
        case Models::SmartPlaylistOperator::Equals:      return tvLower == rvLower;
        case Models::SmartPlaylistOperator::NotEquals:    return tvLower != rvLower;
        case Models::SmartPlaylistOperator::Contains:     return tvLower.find(rvLower) != std::string::npos;
        case Models::SmartPlaylistOperator::NotContains:  return tvLower.find(rvLower) == std::string::npos;
        case Models::SmartPlaylistOperator::StartsWith:   return tvLower.substr(0, rvLower.size()) == rvLower;
        case Models::SmartPlaylistOperator::EndsWith: {
            if (tvLower.size() < rvLower.size()) return false;
            return tvLower.substr(tvLower.size() - rvLower.size()) == rvLower;
        }
        case Models::SmartPlaylistOperator::IsEmpty:     return trackValue.empty();
        case Models::SmartPlaylistOperator::IsNotEmpty:  return !trackValue.empty();
        default: return false;
    }
}

bool SmartPlaylistEngine::evaluateNumeric(double trackValue,
                                            Models::SmartPlaylistOperator op,
                                            const std::string& ruleValue,
                                            const std::string& ruleValue2)
{
    double rv = 0.0;
    try { rv = std::stod(ruleValue); } catch (...) { return false; }

    switch (op) {
        case Models::SmartPlaylistOperator::Equals:        return std::abs(trackValue - rv) < 0.01;
        case Models::SmartPlaylistOperator::NotEquals:      return std::abs(trackValue - rv) >= 0.01;
        case Models::SmartPlaylistOperator::GreaterThan:    return trackValue > rv;
        case Models::SmartPlaylistOperator::LessThan:       return trackValue < rv;
        case Models::SmartPlaylistOperator::GreaterOrEqual: return trackValue >= rv;
        case Models::SmartPlaylistOperator::LessOrEqual:    return trackValue <= rv;
        case Models::SmartPlaylistOperator::Between: {
            double rv2 = 0.0;
            try { rv2 = std::stod(ruleValue2); } catch (...) { return false; }
            return trackValue >= rv && trackValue <= rv2;
        }
        default: return false;
    }
}

bool SmartPlaylistEngine::evaluateRule(const Models::Track& track, const Models::SmartPlaylistRule& rule)
{
    if (isNumericField(rule.field)) {
        double val = getFieldNumericValue(track, rule.field);
        return evaluateNumeric(val, rule.operator_, rule.value, rule.value2);
    } else {
        std::string val = getFieldValue(track, rule.field);
        return evaluateString(val, rule.operator_, rule.value, rule.value2);
    }
}

bool SmartPlaylistEngine::evaluateGroup(const Models::Track& track,
                                          const Models::SmartPlaylistRuleGroup& group)
{
    if (group.rules.empty() && group.subGroups.empty())
        return true;

    bool result = (group.conjunction == Models::RuleConjunction::AND);

    for (auto& rule : group.rules) {
        bool ruleResult = evaluateRule(track, rule);

        if (group.conjunction == Models::RuleConjunction::AND) {
            if (!ruleResult) return false;
        } else {
            if (ruleResult) return true;
        }
    }

    for (auto& subGroup : group.subGroups) {
        bool subResult = evaluateGroup(track, subGroup);

        if (group.conjunction == Models::RuleConjunction::AND) {
            if (!subResult) return false;
        } else {
            if (subResult) return true;
        }
    }

    return result;
}

std::vector<Models::Track> SmartPlaylistEngine::evaluate(
    const std::vector<Models::Track>& tracks,
    const Models::SmartPlaylistRuleGroup& group)
{
    std::vector<Models::Track> results;

    for (auto& track : tracks) {
        if (evaluateGroup(track, group)) {
            results.push_back(track);

            if (group.maxResults > 0 && static_cast<int>(results.size()) >= group.maxResults)
                break;
        }
    }

    spdlog::info("[SmartPlaylist] Evaluated {} tracks, {} matched", tracks.size(), results.size());
    return results;
}

} // namespace BeatMate::Services::Library
