#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace BeatMate::Core {
struct MIDIMappingEntry { int channel; int ccOrNote; bool isCC; std::string action; float minValue = 0; float maxValue = 1; };
using MIDIActionCallback = std::function<void(const std::string& action, float value)>;
class MIDIMapping {
public:
    MIDIMapping() = default;
    void addMapping(const MIDIMappingEntry& mapping);
    void removeMapping(int channel, int ccOrNote, bool isCC);
    void processCC(int channel, int cc, int value, MIDIActionCallback callback);
    void processNote(int channel, int note, int velocity, MIDIActionCallback callback);
    std::vector<MIDIMappingEntry> getAllMappings() const { return mappings_; }
    void clear() { mappings_.clear(); }
    bool save(const std::string& path) const;
    bool load(const std::string& path);
private:
    std::vector<MIDIMappingEntry> mappings_;
};
}
