#pragma once
#include <cstdint>

#include <string>
#include <vector>
#include <memory>

#include "../../models/Track.h"
#include "../../models/SmartPlaylistRule.h"
#include "../../models/Playlist.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

class SmartPlaylist {
public:
    explicit SmartPlaylist(std::shared_ptr<TrackDatabase> database);
    ~SmartPlaylist() = default;

    int64_t createSmartPlaylist(const std::string& name, const Models::SmartPlaylistRuleGroup& rules);

    bool updateRules(int64_t playlistId, const Models::SmartPlaylistRuleGroup& rules);

    std::vector<Models::Track> evaluateRules(const Models::SmartPlaylistRuleGroup& rules);

    std::vector<Models::Track> refreshPlaylist(int64_t playlistId);

    // Get the parameterized SQL query for a rule group (placeholders "?"). The
    std::string generateSQL(const Models::SmartPlaylistRuleGroup& rules,
                            std::vector<std::string>& params) const;

private:
    std::string ruleToSQL(const Models::SmartPlaylistRule& rule,
                          std::vector<std::string>& params) const;
    std::string fieldToColumn(Models::SmartPlaylistField field) const;
    std::string operatorToSQL(Models::SmartPlaylistOperator op, const std::string& column,
                              const std::string& value, const std::string& value2,
                              std::vector<std::string>& params) const;

    std::shared_ptr<TrackDatabase> database_;
};

} // namespace BeatMate::Services::Library
