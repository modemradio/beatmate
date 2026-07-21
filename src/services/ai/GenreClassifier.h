#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../../models/TrackFeatures.h"

namespace BeatMate::Services::AI {

class ONNXInference;

struct GenreScore { std::string genre; float confidence = 0.0f; };

class GenreClassifier {
public:
    explicit GenreClassifier(std::shared_ptr<ONNXInference> inference);
    ~GenreClassifier() = default;
    std::vector<GenreScore> classify(const Models::TrackFeatures& features, int topN = 5);
    bool loadModel(const std::string& path);
    static std::vector<std::string> getSupportedGenres();
private:
    std::shared_ptr<ONNXInference> inference_;
};

}
