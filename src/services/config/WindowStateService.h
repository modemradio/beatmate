#pragma once
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Config {


struct WindowState {
    int x = 100;
    int y = 100;
    int width = 1400;
    int height = 900;
    bool maximized = false;
    bool fullscreen = false;
    int displayIndex = 0;
};

class WindowStateService {
public:
    WindowStateService();
    ~WindowStateService() = default;

    bool saveState(const std::string& filePath = "");
    bool restoreState(const std::string& filePath = "");

    WindowState getState() const;
    void setState(const WindowState& state);

    void setPosition(int x, int y);
    void setSize(int width, int height);
    void setMaximized(bool maximized);
    void setFullscreen(bool fullscreen);
    void setDisplayIndex(int index);

    WindowState getValidatedState(int screenWidth = 1920, int screenHeight = 1080) const;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    void setDefaultFilePath(const std::string& path) { defaultPath_ = path; }

private:
    WindowState state_;
    std::string defaultPath_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Config
