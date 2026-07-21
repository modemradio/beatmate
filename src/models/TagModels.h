#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class TagType : int {
    Genre = 0,
    Mood = 1,
    Energy = 2,
    Style = 3,
    Instrument = 4,
    Vocal = 5,
    Situation = 6,
    Era = 7,
    Custom = 8
};

NLOHMANN_JSON_SERIALIZE_ENUM(TagType, {
    { TagType::Genre, "Genre" },
    { TagType::Mood, "Mood" },
    { TagType::Energy, "Energy" },
    { TagType::Style, "Style" },
    { TagType::Instrument, "Instrument" },
    { TagType::Vocal, "Vocal" },
    { TagType::Situation, "Situation" },
    { TagType::Era, "Era" },
    { TagType::Custom, "Custom" }
})

struct Tag {
    std::string name;
    std::string value;
    TagType type = TagType::Custom;
    std::string color;              // hex color for display

    Tag() = default;

    Tag(const std::string& name, const std::string& value, TagType type = TagType::Custom)
        : name(name), value(value), type(type) {}

    bool operator==(const Tag& other) const {
        return name == other.name && type == other.type;
    }

    bool operator<(const Tag& other) const {
        if (type != other.type) return type < other.type;
        return name < other.name;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Tag, name, value, type, color)
};

struct TrackTags {
    int64_t trackId = 0;
    std::vector<Tag> tags;

    TrackTags() = default;
    explicit TrackTags(int64_t trackId) : trackId(trackId) {}

    void addTag(const Tag& tag) {
        for (const auto& existing : tags) {
            if (existing == tag) return;
        }
        tags.push_back(tag);
    }

    void removeTag(const std::string& name, TagType type) {
        tags.erase(
            std::remove_if(tags.begin(), tags.end(),
                [&](const Tag& t) { return t.name == name && t.type == type; }),
            tags.end()
        );
    }

    [[nodiscard]] std::vector<Tag> getTagsByType(TagType type) const {
        std::vector<Tag> result;
        for (const auto& tag : tags) {
            if (tag.type == type) result.push_back(tag);
        }
        return result;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TrackTags, trackId, tags)
};

} // namespace BeatMate::Models
