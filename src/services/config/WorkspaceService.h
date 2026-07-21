#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Config {


struct WorkspaceLayout {
    std::string id;
    std::string name;
    std::string category;
    bool browserVisible = true;
    bool effectsVisible = true;
    bool waveformVisible = true;
    bool stemsVisible = true;
    bool mixerVisible = true;
    bool samplerVisible = false;
    bool automationVisible = false;
    float browserWidth = 300.0f;
    float mixerHeight = 200.0f;
    float waveformHeight = 150.0f;
    int deckCount = 2;
    nlohmann::json customPanelStates;
};

class WorkspaceService {
public:
    using ChangeCallback = std::function<void(const WorkspaceLayout&)>;

    WorkspaceService();
    ~WorkspaceService() = default;

    void applyWorkspace(const std::string& id);
    WorkspaceLayout getCurrentWorkspace() const;
    std::string getCurrentWorkspaceId() const;

    void saveWorkspace(const WorkspaceLayout& layout);
    bool deleteWorkspace(const std::string& id);
    bool renameWorkspace(const std::string& id, const std::string& newName);

    std::vector<WorkspaceLayout> getAllWorkspaces() const;
    std::vector<WorkspaceLayout> getFactoryWorkspaces() const;
    std::vector<WorkspaceLayout> getUserWorkspaces() const;
    const WorkspaceLayout* getWorkspace(const std::string& id) const;

    void initializeFactoryWorkspaces();

    bool loadFromDirectory(const std::string& directory);
    bool saveToDirectory(const std::string& directory);
    nlohmann::json toJson(const WorkspaceLayout& layout) const;
    WorkspaceLayout fromJson(const nlohmann::json& j) const;

    void addChangeListener(ChangeCallback callback);

private:
    void notifyChange();

    std::vector<WorkspaceLayout> workspaces_;
    std::string currentId_;
    std::vector<ChangeCallback> listeners_;
    mutable std::mutex mutex_;
};

}
