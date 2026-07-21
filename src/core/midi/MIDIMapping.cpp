#include "MIDIMapping.h"
#include <algorithm>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

void MIDIMapping::addMapping(const MIDIMappingEntry& mapping) {
    mappings_.push_back(mapping);
}

void MIDIMapping::removeMapping(int channel, int ccOrNote, bool isCC) {
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
        [=](const MIDIMappingEntry& m) {
            return m.channel == channel && m.ccOrNote == ccOrNote && m.isCC == isCC;
        }), mappings_.end());
}

void MIDIMapping::processCC(int channel, int cc, int value, MIDIActionCallback callback) {
    if (!callback) return;
    for (auto& m : mappings_) {
        if (m.isCC && m.channel == channel && m.ccOrNote == cc) {
            float normalized = static_cast<float>(value) / 127.0f;
            float mapped = m.minValue + normalized * (m.maxValue - m.minValue);
            callback(m.action, mapped);
        }
    }
}

void MIDIMapping::processNote(int channel, int note, int velocity, MIDIActionCallback callback) {
    if (!callback) return;
    for (auto& m : mappings_) {
        if (!m.isCC && m.channel == channel && m.ccOrNote == note) {
            float value = static_cast<float>(velocity) / 127.0f;
            callback(m.action, value);
        }
    }
}

bool MIDIMapping::save(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return false;
    for (auto& m : mappings_) {
        file << m.channel << " " << m.ccOrNote << " " << m.isCC << " "
             << m.action << " " << m.minValue << " " << m.maxValue << "\n";
    }
    return true;
}

bool MIDIMapping::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    mappings_.clear();
    MIDIMappingEntry m;
    int isCC;
    while (file >> m.channel >> m.ccOrNote >> isCC >> m.action >> m.minValue >> m.maxValue) {
        m.isCC = static_cast<bool>(isCC);
        mappings_.push_back(m);
    }
    spdlog::info("MIDIMapping: loaded {} mappings from {}", mappings_.size(), path);
    return true;
}

} // namespace BeatMate::Core
