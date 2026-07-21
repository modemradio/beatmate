#include "WorkspaceService.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Config {

WorkspaceService::WorkspaceService() {
    initializeFactoryWorkspaces();
    if (!workspaces_.empty()) currentId_ = workspaces_.front().id;
}

void WorkspaceService::applyWorkspace(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& w : workspaces_) {
        if (w.id == id) {
            currentId_ = id;
            spdlog::info("WorkspaceService: applied workspace '{}'", w.name);
            break;
        }
    }
}

WorkspaceLayout WorkspaceService::getCurrentWorkspace() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& w : workspaces_)
        if (w.id == currentId_) return w;
    if (!workspaces_.empty()) return workspaces_.front();
    return {};
}

std::string WorkspaceService::getCurrentWorkspaceId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentId_;
}

void WorkspaceService::saveWorkspace(const WorkspaceLayout& layout) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
        [&](const WorkspaceLayout& w) { return w.id == layout.id; });
    if (it != workspaces_.end()) *it = layout;
    else workspaces_.push_back(layout);
    spdlog::info("WorkspaceService: saved workspace '{}'", layout.name);
}

bool WorkspaceService::deleteWorkspace(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
        [&](const WorkspaceLayout& w) { return w.id == id; });
    if (it == workspaces_.end()) return false;
    if (it->category == "factory") return false;
    workspaces_.erase(it);
    return true;
}

bool WorkspaceService::renameWorkspace(const std::string& id, const std::string& newName) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& w : workspaces_) {
        if (w.id == id && w.category != "factory") {
            w.name = newName;
            return true;
        }
    }
    return false;
}

std::vector<WorkspaceLayout> WorkspaceService::getAllWorkspaces() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return workspaces_;
}

std::vector<WorkspaceLayout> WorkspaceService::getFactoryWorkspaces() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WorkspaceLayout> result;
    for (auto& w : workspaces_) if (w.category == "factory") result.push_back(w);
    return result;
}

std::vector<WorkspaceLayout> WorkspaceService::getUserWorkspaces() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WorkspaceLayout> result;
    for (auto& w : workspaces_) if (w.category == "user") result.push_back(w);
    return result;
}

const WorkspaceLayout* WorkspaceService::getWorkspace(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& w : workspaces_) if (w.id == id) return &w;
    return nullptr;
}

void WorkspaceService::initializeFactoryWorkspaces() {
    workspaces_.clear();

    workspaces_.push_back({"factory_performance", "Performance", "factory",
        true, true, true, false, true, false, false, 250.0f, 200.0f, 150.0f, 2, {}});

    workspaces_.push_back({"factory_browser", "Browser", "factory",
        true, false, true, false, true, false, false, 450.0f, 180.0f, 120.0f, 2, {}});

    workspaces_.push_back({"factory_4deck", "4 Deck", "factory",
        true, true, true, false, true, false, false, 200.0f, 160.0f, 100.0f, 4, {}});

    workspaces_.push_back({"factory_stems", "Stems", "factory",
        true, true, true, true, true, false, false, 250.0f, 200.0f, 150.0f, 2, {}});

    workspaces_.push_back({"factory_minimal", "Minimal", "factory",
        false, false, true, false, true, false, false, 0.0f, 150.0f, 120.0f, 2, {}});

    workspaces_.push_back({"factory_preparation", "Preparation", "factory",
        true, false, true, false, false, false, false, 500.0f, 0.0f, 180.0f, 2, {}});

    workspaces_.push_back({"factory_sampler", "Sampler", "factory",
        true, true, true, false, true, true, false, 250.0f, 200.0f, 120.0f, 2, {}});

    spdlog::info("WorkspaceService: initialized {} factory workspaces", workspaces_.size());
}

bool WorkspaceService::loadFromDirectory(const std::string& directory) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(directory)) return false;
    for (auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() != ".bmworkspace") continue;
        try {
            std::ifstream ifs(entry.path());
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            auto json = nlohmann::json::parse(content);
            auto layout = fromJson(json);
            auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                [&](const WorkspaceLayout& w) { return w.id == layout.id; });
            if (it == workspaces_.end()) workspaces_.push_back(layout);
        } catch (...) {}
    }
    return true;
}

bool WorkspaceService::saveToDirectory(const std::string& directory) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(directory)) fs::create_directories(directory);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& w : workspaces_) {
        if (w.category == "user") {
            auto json = toJson(w);
            std::string path = (fs::path(directory) / (w.id + ".bmworkspace")).string();
            std::ofstream ofs(path);
            if (ofs) ofs << json.dump(2);
        }
    }
    return true;
}

nlohmann::json WorkspaceService::toJson(const WorkspaceLayout& layout) const {
    return {
        {"id", layout.id}, {"name", layout.name}, {"category", layout.category},
        {"browserVisible", layout.browserVisible}, {"effectsVisible", layout.effectsVisible},
        {"waveformVisible", layout.waveformVisible}, {"stemsVisible", layout.stemsVisible},
        {"mixerVisible", layout.mixerVisible}, {"samplerVisible", layout.samplerVisible},
        {"automationVisible", layout.automationVisible},
        {"browserWidth", layout.browserWidth}, {"mixerHeight", layout.mixerHeight},
        {"waveformHeight", layout.waveformHeight}, {"deckCount", layout.deckCount},
        {"customPanelStates", layout.customPanelStates}
    };
}

WorkspaceLayout WorkspaceService::fromJson(const nlohmann::json& j) const {
    WorkspaceLayout l;
    l.id = j.value("id", ""); l.name = j.value("name", "Untitled"); l.category = j.value("category", "user");
    l.browserVisible = j.value("browserVisible", true); l.effectsVisible = j.value("effectsVisible", true);
    l.waveformVisible = j.value("waveformVisible", true); l.stemsVisible = j.value("stemsVisible", false);
    l.mixerVisible = j.value("mixerVisible", true); l.samplerVisible = j.value("samplerVisible", false);
    l.automationVisible = j.value("automationVisible", false);
    l.browserWidth = j.value("browserWidth", 300.0f); l.mixerHeight = j.value("mixerHeight", 200.0f);
    l.waveformHeight = j.value("waveformHeight", 150.0f); l.deckCount = j.value("deckCount", 2);
    if (j.contains("customPanelStates")) l.customPanelStates = j["customPanelStates"];
    return l;
}

void WorkspaceService::addChangeListener(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(callback));
}

void WorkspaceService::notifyChange() {
    WorkspaceLayout current;
    std::vector<ChangeCallback> cbs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& w : workspaces_) if (w.id == currentId_) { current = w; break; }
        cbs = listeners_;
    }
    for (auto& cb : cbs) cb(current);
}

} // namespace BeatMate::Services::Config
