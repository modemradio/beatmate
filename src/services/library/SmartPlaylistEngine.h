#pragma once

#include <vector>
#include <string>
#include "../../models/Track.h"
#include "../../models/SmartPlaylistRule.h"

namespace BeatMate::Services::Library {

class SmartPlaylistEngine {
public:
    SmartPlaylistEngine() = default;

    std::vector<Models::Track> evaluate(const std::vector<Models::Track>& tracks,
                                         const Models::SmartPlaylistRuleGroup& group);

    bool evaluateRule(const Models::Track& track, const Models::SmartPlaylistRule& rule);

private:
    std::string getFieldValue(const Models::Track& track, Models::SmartPlaylistField field);
    double getFieldNumericValue(const Models::Track& track, Models::SmartPlaylistField field);
    bool isNumericField(Models::SmartPlaylistField field);
    bool evaluateString(const std::string& trackValue, Models::SmartPlaylistOperator op,
                        const std::string& ruleValue, const std::string& ruleValue2 = "");
    bool evaluateNumeric(double trackValue, Models::SmartPlaylistOperator op,
                         const std::string& ruleValue, const std::string& ruleValue2 = "");
    bool evaluateGroup(const Models::Track& track, const Models::SmartPlaylistRuleGroup& group);
};

} // namespace BeatMate::Services::Library
