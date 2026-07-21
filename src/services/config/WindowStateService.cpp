#include "WindowStateService.h"
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Config {

WindowStateService::WindowStateService() {}

bool WindowStateService::saveState(const std::string& filePath) {
    std::string path = filePath.empty() ? defaultPath_ : filePath;
    if (path.empty()) path = "window_state.json";

    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto json = toJson();
        std::ofstream ofs(path);
        if (!ofs) return false;
        ofs << json.dump(2);
        spdlog::info("WindowStateService: saved state to '{}'", path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("WindowStateService: save failed: {}", e.what());
        return false;
    }
}

bool WindowStateService::restoreState(const std::string& filePath) {
    std::string path = filePath.empty() ? defaultPath_ : filePath;
    if (path.empty()) path = "window_state.json";

    std::lock_guard<std::mutex> lock(mutex_);
    try {
        if (!std::filesystem::exists(path)) return false;
        std::ifstream ifs(path);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto json = nlohmann::json::parse(content);
        fromJson(json);
        spdlog::info("WindowStateService: restored state from '{}'", path);
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("WindowStateService: restore failed: {}", e.what());
        return false;
    }
}

WindowState WindowStateService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void WindowStateService::setState(const WindowState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
}

void WindowStateService::setPosition(int x, int y) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.x = x;
    state_.y = y;
}

void WindowStateService::setSize(int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.width = width;
    state_.height = height;
}

void WindowStateService::setMaximized(bool maximized) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.maximized = maximized;
}

void WindowStateService::setFullscreen(bool fullscreen) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.fullscreen = fullscreen;
}

void WindowStateService::setDisplayIndex(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.displayIndex = index;
}

WindowState WindowStateService::getValidatedState(int screenWidth, int screenHeight) const {
    WindowState s = state_;
    s.width = std::max(800, std::min(s.width, screenWidth));
    s.height = std::max(500, std::min(s.height, screenHeight));
    s.x = std::max(0, std::min(s.x, screenWidth - s.width));
    s.y = std::max(0, std::min(s.y, screenHeight - s.height));
    return s;
}

nlohmann::json WindowStateService::toJson() const {
    return {
        {"x", state_.x}, {"y", state_.y},
        {"width", state_.width}, {"height", state_.height},
        {"maximized", state_.maximized}, {"fullscreen", state_.fullscreen},
        {"displayIndex", state_.displayIndex}
    };
}

void WindowStateService::fromJson(const nlohmann::json& j) {
    state_.x = j.value("x", 100);
    state_.y = j.value("y", 100);
    state_.width = j.value("width", 1400);
    state_.height = j.value("height", 900);
    state_.maximized = j.value("maximized", false);
    state_.fullscreen = j.value("fullscreen", false);
    state_.displayIndex = j.value("displayIndex", 0);
}

} // namespace BeatMate::Services::Config
