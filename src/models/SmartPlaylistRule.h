#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class SmartPlaylistField : int {
    BPM = 0,
    Key = 1,
    Genre = 2,
    Energy = 3,
    Rating = 4,
    Year = 5,
    Artist = 6,
    Title = 7,
    Album = 8,
    Duration = 9,
    PlayCount = 10,
    DateAdded = 11,
    LastPlayed = 12,
    Comment = 13,
    Label = 14,
    Mood = 15,
    Danceability = 16,
    Color = 17,
    Source = 18,
    FileFormat = 19,
    BitRate = 20,
    SampleRate = 21,
    Grouping = 22,
    CamelotKey = 23,
    OpenKey = 24
};

NLOHMANN_JSON_SERIALIZE_ENUM(SmartPlaylistField, {
    { SmartPlaylistField::BPM, "BPM" },
    { SmartPlaylistField::Key, "Key" },
    { SmartPlaylistField::Genre, "Genre" },
    { SmartPlaylistField::Energy, "Energy" },
    { SmartPlaylistField::Rating, "Rating" },
    { SmartPlaylistField::Year, "Year" },
    { SmartPlaylistField::Artist, "Artist" },
    { SmartPlaylistField::Title, "Title" },
    { SmartPlaylistField::Album, "Album" },
    { SmartPlaylistField::Duration, "Duration" },
    { SmartPlaylistField::PlayCount, "PlayCount" },
    { SmartPlaylistField::DateAdded, "DateAdded" },
    { SmartPlaylistField::LastPlayed, "LastPlayed" },
    { SmartPlaylistField::Comment, "Comment" },
    { SmartPlaylistField::Label, "Label" },
    { SmartPlaylistField::Mood, "Mood" },
    { SmartPlaylistField::Danceability, "Danceability" },
    { SmartPlaylistField::Color, "Color" },
    { SmartPlaylistField::Source, "Source" },
    { SmartPlaylistField::FileFormat, "FileFormat" },
    { SmartPlaylistField::BitRate, "BitRate" },
    { SmartPlaylistField::SampleRate, "SampleRate" },
    { SmartPlaylistField::Grouping, "Grouping" },
    { SmartPlaylistField::CamelotKey, "CamelotKey" },
    { SmartPlaylistField::OpenKey, "OpenKey" }
})

enum class SmartPlaylistOperator : int {
    Equals = 0,
    NotEquals = 1,
    Contains = 2,
    NotContains = 3,
    StartsWith = 4,
    EndsWith = 5,
    GreaterThan = 6,
    LessThan = 7,
    GreaterOrEqual = 8,
    LessOrEqual = 9,
    Between = 10,
    IsEmpty = 11,
    IsNotEmpty = 12,
    InLast = 13,       // e.g. "in last 30 days"
    NotInLast = 14
};

NLOHMANN_JSON_SERIALIZE_ENUM(SmartPlaylistOperator, {
    { SmartPlaylistOperator::Equals, "Equals" },
    { SmartPlaylistOperator::NotEquals, "NotEquals" },
    { SmartPlaylistOperator::Contains, "Contains" },
    { SmartPlaylistOperator::NotContains, "NotContains" },
    { SmartPlaylistOperator::StartsWith, "StartsWith" },
    { SmartPlaylistOperator::EndsWith, "EndsWith" },
    { SmartPlaylistOperator::GreaterThan, "GreaterThan" },
    { SmartPlaylistOperator::LessThan, "LessThan" },
    { SmartPlaylistOperator::GreaterOrEqual, "GreaterOrEqual" },
    { SmartPlaylistOperator::LessOrEqual, "LessOrEqual" },
    { SmartPlaylistOperator::Between, "Between" },
    { SmartPlaylistOperator::IsEmpty, "IsEmpty" },
    { SmartPlaylistOperator::IsNotEmpty, "IsNotEmpty" },
    { SmartPlaylistOperator::InLast, "InLast" },
    { SmartPlaylistOperator::NotInLast, "NotInLast" }
})

enum class RuleConjunction : int {
    AND = 0,
    OR = 1
};

NLOHMANN_JSON_SERIALIZE_ENUM(RuleConjunction, {
    { RuleConjunction::AND, "AND" },
    { RuleConjunction::OR, "OR" }
})

struct SmartPlaylistRule {
    SmartPlaylistField field = SmartPlaylistField::BPM;
    SmartPlaylistOperator operator_ = SmartPlaylistOperator::Equals;
    std::string value;
    std::string value2;         // for Between operator
    RuleConjunction conjunction = RuleConjunction::AND;

    SmartPlaylistRule() = default;

    SmartPlaylistRule(SmartPlaylistField field, SmartPlaylistOperator op, const std::string& value)
        : field(field), operator_(op), value(value) {}

    SmartPlaylistRule(SmartPlaylistField field, SmartPlaylistOperator op,
                      const std::string& value, const std::string& value2)
        : field(field), operator_(op), value(value), value2(value2) {}

    bool operator==(const SmartPlaylistRule& other) const {
        return field == other.field && operator_ == other.operator_ &&
               value == other.value && value2 == other.value2;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SmartPlaylistRule,
        field, operator_, value, value2, conjunction
    )
};

struct SmartPlaylistRuleGroup {
    RuleConjunction conjunction = RuleConjunction::AND;
    std::vector<SmartPlaylistRule> rules;
    std::vector<SmartPlaylistRuleGroup> subGroups;
    int maxResults = -1;        // -1 = unlimited

    SmartPlaylistRuleGroup() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SmartPlaylistRuleGroup,
        conjunction, rules, subGroups, maxResults
    )
};

} // namespace BeatMate::Models
