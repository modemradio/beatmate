#include "DJProfileService.h"

#include "SmartSuggestEngine.h"

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>

namespace BeatMate::Services::Suggestions {

using Models::DJProfile;
using nlohmann::json;

namespace {

std::string profilesFilePath() {
    auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory).getChildFile("BeatMate");
    dir.createDirectory();
    return dir.getChildFile("profiles.json").getFullPathName().toStdString();
}

EnergyDirection toEnergyDir(const std::string& s) {
    if (s == "Increase") return EnergyDirection::Increase;
    if (s == "Decrease") return EnergyDirection::Decrease;
    if (s == "Maintain") return EnergyDirection::Maintain;
    return EnergyDirection::Auto;
}

} // namespace

DJProfileService::DJProfileService()
    : filePath_(profilesFilePath())
{
    ensureSeeded();
}

std::vector<DJProfile> DJProfileService::seededProfiles() {
    std::vector<DJProfile> out;

    {
        DJProfile p;
        p.name  = "Club Peak Hours";
        p.venue = "Club";
        // Heavy BPM + harmonic + energy; favors trending peak-time tracks.
        p.weights = { 0.30, 0.22, 0.18, 0.08, 0.08, 0.04, 0.05, 0.02, 0.02, 0.01 };
        p.minBPM = 122; p.maxBPM = 132;
        p.genreFilter = "House";
        p.energyDirection = "Increase";
        out.push_back(p);
    }
    {
        DJProfile p;
        p.name  = "Wedding Classics";
        p.venue = "Wedding";
        // Style + era over harmonic — crowd-pleaser eras matter most.
        p.weights = { 0.18, 0.16, 0.14, 0.16, 0.04, 0.05, 0.05, 0.15, 0.05, 0.02 };
        p.minBPM = 95; p.maxBPM = 130;
        p.genreFilter = "";
        p.energyDirection = "Auto";
        out.push_back(p);
    }
    {
        DJProfile p;
        p.name  = "Lounge Bar";
        p.venue = "Bar";
        // Calmer set: timbre + style, low-tempo cap, decreasing energy.
        p.weights = { 0.20, 0.14, 0.10, 0.18, 0.04, 0.04, 0.18, 0.06, 0.04, 0.02 };
        p.minBPM = 90; p.maxBPM = 115;
        p.genreFilter = "Deep House";
        p.energyDirection = "Maintain";
        out.push_back(p);
    }
    {
        DJProfile p;
        p.name  = "Festival Main Stage";
        p.venue = "Festival";
        // Drops > harmony: structure + trending heavy, wide BPM band.
        p.weights = { 0.20, 0.18, 0.20, 0.06, 0.12, 0.12, 0.04, 0.02, 0.04, 0.02 };
        p.minBPM = 124; p.maxBPM = 150;
        p.genreFilter = "";
        p.energyDirection = "Increase";
        out.push_back(p);
    }
    {
        DJProfile p;
        p.name  = "Warm-up Session";
        p.venue = "Club";
        // Slow build, harmonic-priority, low BPMs.
        p.weights = { 0.34, 0.18, 0.10, 0.12, 0.02, 0.06, 0.10, 0.04, 0.02, 0.02 };
        p.minBPM = 100; p.maxBPM = 122;
        p.genreFilter = "";
        p.energyDirection = "Maintain";
        out.push_back(p);
    }

    return out;
}

std::vector<DJProfile> DJProfileService::readAll() {
    std::vector<DJProfile> out;
    juce::File f(filePath_);
    if (!f.existsAsFile()) return out;
    try {
        std::ifstream in(filePath_);
        if (!in) return out;
        json j; in >> j;
        if (j.is_array()) {
            for (const auto& item : j) {
                try { out.push_back(item.get<DJProfile>()); }
                catch (...) { /* skip malformed entry */ }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[DJProfileService] read failed: {}", e.what());
    }
    return out;
}

bool DJProfileService::writeAll(const std::vector<DJProfile>& all) {
    try {
        json arr = json::array();
        for (const auto& p : all) arr.push_back(p);
        std::ofstream out(filePath_, std::ios::trunc);
        if (!out) return false;
        out << arr.dump(2);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[DJProfileService] write failed: {}", e.what());
        return false;
    }
}

void DJProfileService::ensureSeeded() {
    juce::File f(filePath_);
    if (f.existsAsFile() && f.getSize() > 2) return;
    auto seeds = seededProfiles();
    if (writeAll(seeds))
        spdlog::info("[DJProfileService] seeded {} default profiles -> {}",
                     seeds.size(), filePath_);
}

std::vector<DJProfile> DJProfileService::listProfiles() {
    std::lock_guard<std::mutex> g(lock_);
    return readAll();
}

std::optional<DJProfile> DJProfileService::loadProfile(const std::string& name) {
    std::lock_guard<std::mutex> g(lock_);
    auto all = readAll();
    auto it = std::find_if(all.begin(), all.end(),
        [&](const DJProfile& p) { return p.name == name; });
    if (it == all.end()) return std::nullopt;
    return *it;
}

bool DJProfileService::saveProfile(const DJProfile& profile) {
    std::lock_guard<std::mutex> g(lock_);
    auto all = readAll();
    auto it = std::find_if(all.begin(), all.end(),
        [&](const DJProfile& p) { return p.name == profile.name; });
    if (it == all.end()) all.push_back(profile);
    else                 *it = profile;
    return writeAll(all);
}

bool DJProfileService::deleteProfile(const std::string& name) {
    std::lock_guard<std::mutex> g(lock_);
    auto all = readAll();
    auto sz = all.size();
    all.erase(std::remove_if(all.begin(), all.end(),
        [&](const DJProfile& p) { return p.name == name; }), all.end());
    if (all.size() == sz) return false;
    return writeAll(all);
}

void DJProfileService::applyProfile(const DJProfile& profile,
                                    SmartSuggestEngine& engine)
{
    engine.setWeights(profile.weights);
    engine.setBPMRange(profile.minBPM, profile.maxBPM);
    engine.setGenreFilter(profile.genreFilter);
    engine.setEnergyDirection(toEnergyDir(profile.energyDirection));
    engine.setSessionVenue(profile.venue);

    // Favorites -> always-boost; skipped -> blacklist.
    engine.boostIds(profile.favorites);
    std::unordered_set<int64_t> bl(profile.skipped.begin(), profile.skipped.end());
    engine.setBlacklist(std::move(bl));

    std::vector<std::pair<int64_t, int64_t>> assoc;
    assoc.reserve(profile.associations.size());
    for (const auto& a : profile.associations)
        if (a[0] > 0 && a[1] > 0) assoc.emplace_back(a[0], a[1]);
    engine.setManualAssociations(assoc);

    spdlog::info("[DJProfileService] applied profile '{}' (venue={}, BPM {}-{})",
                 profile.name, profile.venue,
                 (int) profile.minBPM, (int) profile.maxBPM);
}

} // namespace BeatMate::Services::Suggestions
