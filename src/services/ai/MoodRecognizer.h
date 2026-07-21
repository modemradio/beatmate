#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../../models/TrackFeatures.h"

namespace BeatMate::Services::AI {
class ONNXInference;

struct MoodResult { std::string mood; float confidence = 0.0f; float valence = 0.0f; float arousal = 0.0f; };

class MoodRecognizer {
public:
    explicit MoodRecognizer(std::shared_ptr<ONNXInference> inference);
    ~MoodRecognizer() = default;
    MoodResult recognize(const Models::TrackFeatures& features);
    bool loadModel(const std::string& path);
private:
    std::shared_ptr<ONNXInference> inference_;
};
} // namespace BeatMate::Services::AI
