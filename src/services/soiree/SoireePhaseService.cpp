#include "SoireePhaseService.h"

#include <algorithm>
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Soiree {

using Models::PhaseTemplate;
using Models::ClientRequests;
using Models::SoireeVenue;

namespace {

PhaseTemplate makeTemplate(
    std::string name,
    SoireeVenue venue,
    float f0, double bMin0, double bMax0, float eMin0, float eMax0,
    float f1, double bMin1, double bMax1, float eMin1, float eMax1,
    float f2, double bMin2, double bMax2, float eMin2, float eMax2,
    float f3, double bMin3, double bMax3, float eMin3, float eMax3,
    float f4, double bMin4, double bMax4, float eMin4, float eMax4)
{
    PhaseTemplate t;
    t.name  = std::move(name);
    t.venue = venue;
    t.phaseFractions = { f0, f1, f2, f3, f4 };
    t.bpmRanges = {
        { bMin0, bMax0 }, { bMin1, bMax1 }, { bMin2, bMax2 },
        { bMin3, bMax3 }, { bMin4, bMax4 }
    };
    t.energyRanges = {
        { eMin0, eMax0 }, { eMin1, eMax1 }, { eMin2, eMax2 },
        { eMin3, eMax3 }, { eMin4, eMax4 }
    };
    return t;
}

} // namespace

SoireePhaseService::SoireePhaseService() {
    seedDefaults();
}

void SoireePhaseService::seedDefaults() {
    m_templates.clear();

    m_templates.push_back(makeTemplate(
        "Club classic", SoireeVenue::Club,
        0.15f, 115.0, 122.0, 3.0f, 5.0f,   // WarmUp
        0.20f, 122.0, 128.0, 5.0f, 7.0f,   // Buildup
        0.35f, 126.0, 132.0, 8.0f, 10.0f,  // Peak
        0.20f, 120.0, 128.0, 6.0f, 8.0f,   // CoolDown
        0.10f, 115.0, 122.0, 4.0f, 6.0f)); // Closing

    m_templates.push_back(makeTemplate(
        "Wedding", SoireeVenue::Wedding,
        0.20f,  90.0, 110.0, 2.0f, 4.0f,   // WarmUp
        0.20f, 105.0, 118.0, 4.0f, 6.0f,   // Buildup
        0.25f, 115.0, 128.0, 7.0f,  9.0f,  // Peak
        0.25f, 100.0, 120.0, 5.0f,  7.0f,  // CoolDown
        0.10f,  80.0, 105.0, 3.0f,  5.0f));// Closing (slow dances)

    m_templates.push_back(makeTemplate(
        "Festival", SoireeVenue::Festival,
        0.08f, 120.0, 126.0, 5.0f,  7.0f,   // WarmUp
        0.17f, 124.0, 130.0, 6.0f,  8.0f,   // Buildup
        0.50f, 128.0, 140.0, 8.5f, 10.0f,   // Peak (long!)
        0.15f, 124.0, 132.0, 7.0f,  9.0f,   // CoolDown
        0.10f, 118.0, 126.0, 5.0f,  7.0f)); // Closing

    m_templates.push_back(makeTemplate(
        "Bar", SoireeVenue::Bar,
        0.25f,  90.0, 108.0, 2.0f, 4.0f,   // WarmUp
        0.20f, 100.0, 115.0, 3.0f, 5.0f,   // Buildup
        0.20f, 108.0, 120.0, 4.0f, 6.0f,   // "Peak" (soft)
        0.20f, 100.0, 115.0, 3.0f, 5.0f,   // CoolDown
        0.15f,  85.0, 105.0, 2.0f, 4.0f)); // Closing
}

std::vector<PhaseTemplate> SoireePhaseService::listTemplates() const {
    return m_templates;
}

std::optional<PhaseTemplate>
SoireePhaseService::getTemplate(const std::string& name) const {
    auto it = std::find_if(m_templates.begin(), m_templates.end(),
        [&](const PhaseTemplate& t) { return t.name == name; });
    if (it == m_templates.end()) return std::nullopt;
    return *it;
}

bool SoireePhaseService::addTemplate(const PhaseTemplate& t) {
    if (!isValidTemplate(t)) return false;
    if (getTemplate(t.name).has_value()) return false;
    m_templates.push_back(t);
    return true;
}

bool SoireePhaseService::isValidTemplate(const PhaseTemplate& t) {
    if (t.name.empty()) return false;
    if (t.phaseFractions.size() != 5) return false;
    if (t.bpmRanges.size()    != 5)   return false;
    if (t.energyRanges.size() != 5)   return false;

    float sum = 0.0f;
    for (float f : t.phaseFractions) {
        if (f < 0.0f || f > 1.0f) return false;
        sum += f;
    }
    if (std::fabs(sum - 1.0f) > 0.05f) return false;

    for (const auto& r : t.bpmRanges) {
        if (r.first <= 0.0 || r.second <= 0.0) return false;
        if (r.first > r.second) return false;
    }
    for (const auto& r : t.energyRanges) {
        if (r.first < 0.0f || r.second > 10.0f) return false;
        if (r.first > r.second) return false;
    }
    return true;
}

bool SoireePhaseService::saveToFile(const std::string& path) const {
    try {
        nlohmann::json j;
        j["templates"]      = m_templates;
        j["clientRequests"] = m_clientReq;

        std::ofstream os(path);
        if (!os) return false;
        os << j.dump(2);
        return static_cast<bool>(os);
    } catch (const std::exception&) {
        return false;
    }
}

bool SoireePhaseService::loadFromFile(const std::string& path) {
    try {
        std::ifstream is(path);
        if (!is) return false;

        nlohmann::json j;
        is >> j;

        if (j.contains("templates")) {
            auto parsed = j.at("templates").get<std::vector<PhaseTemplate>>();
            std::vector<PhaseTemplate> accepted;
            accepted.reserve(parsed.size());
            for (auto& t : parsed) {
                if (isValidTemplate(t)) accepted.push_back(std::move(t));
            }
            m_templates = std::move(accepted);
        }
        if (j.contains("clientRequests")) {
            m_clientReq = j.at("clientRequests").get<ClientRequests>();
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SoireePhaseService::isMustPlay(int64_t trackId) const {
    const auto& v = m_clientReq.mustPlayIds;
    return std::find(v.begin(), v.end(), trackId) != v.end();
}

bool SoireePhaseService::isDoNotPlay(int64_t trackId) const {
    const auto& v = m_clientReq.doNotPlayIds;
    return std::find(v.begin(), v.end(), trackId) != v.end();
}

} // namespace BeatMate::Services::Soiree
