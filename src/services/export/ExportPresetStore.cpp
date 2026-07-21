#include "ExportPresetStore.h"
#include "../../app/ServiceLocator.h"
#include "../config/SettingsManager.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::Services::Export {

namespace {
constexpr const char* kPrefsKey = "export.presets";

Services::Config::SettingsManager* settings()
{
    if (!BeatMate::g_serviceLocator) return nullptr;
    return BeatMate::g_serviceLocator->tryGet<Services::Config::SettingsManager>();
}

nlohmann::json settingsToJson(const BatchExportSettings& s)
{
    nlohmann::json j;
    j["formatId"] = s.formatId;
    j["bitRateKbps"] = s.bitRateKbps;
    j["vbr"] = s.vbr;
    j["sampleRate"] = s.sampleRate;
    j["bitDepth"] = s.bitDepth;
    j["normalize"] = s.normalize;
    j["targetLufs"] = s.targetLufs;
    j["writeTags"] = s.writeTags;
    j["writeM3U"] = s.writeM3U;
    j["structureId"] = s.structureId;
    j["targetRekordbox"] = s.targetRekordbox;
    j["targetSerato"] = s.targetSerato;
    j["targetTraktor"] = s.targetTraktor;
    j["targetVirtualDJ"] = s.targetVirtualDJ;
    return j;
}

BatchExportSettings settingsFromJson(const nlohmann::json& j)
{
    BatchExportSettings s;
    s.formatId = j.value("formatId", 1);
    s.bitRateKbps = j.value("bitRateKbps", 320);
    s.vbr = j.value("vbr", false);
    s.sampleRate = j.value("sampleRate", 44100);
    s.bitDepth = j.value("bitDepth", 16);
    s.normalize = j.value("normalize", false);
    s.targetLufs = j.value("targetLufs", -14.0f);
    s.writeTags = j.value("writeTags", true);
    s.writeM3U = j.value("writeM3U", true);
    s.structureId = j.value("structureId", 1);
    s.targetRekordbox = j.value("targetRekordbox", false);
    s.targetSerato = j.value("targetSerato", false);
    s.targetTraktor = j.value("targetTraktor", false);
    s.targetVirtualDJ = j.value("targetVirtualDJ", false);
    return s;
}

void saveAll(const std::vector<ExportPreset>& presets)
{
    auto* mgr = settings();
    if (!mgr) return;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : presets)
    {
        nlohmann::json e = settingsToJson(p.settings);
        e["name"] = p.name;
        arr.push_back(e);
    }
    mgr->set<std::string>(kPrefsKey, arr.dump());
    mgr->save();
}
}

std::vector<ExportPreset> ExportPresetStore::loadAll()
{
    std::vector<ExportPreset> out;
    auto* mgr = settings();
    if (!mgr) return out;
    const std::string raw = mgr->get<std::string>(kPrefsKey, "");
    if (raw.empty()) return out;
    try
    {
        auto arr = nlohmann::json::parse(raw);
        if (!arr.is_array()) return out;
        for (const auto& e : arr)
        {
            ExportPreset p;
            p.name = e.value("name", "");
            if (p.name.empty()) continue;
            p.settings = settingsFromJson(e);
            out.push_back(std::move(p));
        }
    }
    catch (const std::exception& ex)
    {
        spdlog::warn("[ExportPresets] parse failed: {}", ex.what());
    }
    return out;
}

void ExportPresetStore::upsert(const ExportPreset& preset)
{
    auto all = loadAll();
    auto it = std::find_if(all.begin(), all.end(),
                           [&preset](const ExportPreset& p) { return p.name == preset.name; });
    if (it != all.end())
        *it = preset;
    else
        all.push_back(preset);
    saveAll(all);
}

bool ExportPresetStore::remove(const std::string& name)
{
    auto all = loadAll();
    const auto before = all.size();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&name](const ExportPreset& p) { return p.name == name; }),
              all.end());
    if (all.size() == before)
        return false;
    saveAll(all);
    return true;
}

}
