#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace BeatMate::Services::AI {

class ONNXInference {
public:
    ONNXInference();
    ~ONNXInference();

    bool loadModel(const std::string& modelPath);
    std::vector<float> run(const std::vector<float>& input);
    bool isLoaded() const { return loaded_; }
    bool isGPUAvailable() const;
    void setUseGPU(bool useGPU) { useGPU_ = useGPU; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
    bool useGPU_ = true;
    std::string modelPath_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::AI
