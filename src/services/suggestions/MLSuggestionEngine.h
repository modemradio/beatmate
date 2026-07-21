#pragma once
#include <vector>
#include <string>
#include <memory>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Library { class TrackDatabase; }
namespace BeatMate::Services::AI { class ONNXInference; }

namespace BeatMate::Services::Suggestions {

class MLSuggestionEngine {
public:
    explicit MLSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~MLSuggestionEngine() = default;

    std::vector<RecommendationResult> suggest(const Models::Track& current, int count = 10);
    bool loadModel(const std::string& modelPath);
    bool isModelLoaded() const { return modelLoaded_; }

private:
    std::vector<float> extractFeatures(const Models::Track& track) const;
    std::vector<float> embed(const std::vector<float>& features) const;
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;
    std::shared_ptr<Library::TrackDatabase> database_;
    std::shared_ptr<AI::ONNXInference> inference_;
    bool modelLoaded_ = false;
    std::string modelPath_;
};

}
