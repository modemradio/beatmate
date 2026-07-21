#include "GenreClassifier.h"
#include "ONNXInference.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::Services::AI {

GenreClassifier::GenreClassifier(std::shared_ptr<ONNXInference> inference)
    : inference_(std::move(inference)) {}

std::vector<GenreScore> GenreClassifier::classify(const Models::TrackFeatures& features, int topN) {
    std::vector<float> input;
    input.insert(input.end(), features.mfcc.begin(), features.mfcc.end());
    input.insert(input.end(), features.chroma.begin(), features.chroma.end());
    input.push_back(features.spectralContrast);
    input.push_back(features.spectralBandwidth);
    input.push_back(features.harmonicRatio);
    input.push_back(features.percussiveRatio);

    auto output = inference_->run(input);
    auto genres = getSupportedGenres();

    std::vector<GenreScore> scores;
    for (size_t i = 0; i < std::min(output.size(), genres.size()); ++i) {
        scores.push_back({genres[i], output[i]});
    }

    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
    if (static_cast<int>(scores.size()) > topN) scores.resize(static_cast<size_t>(topN));
    return scores;
}

bool GenreClassifier::loadModel(const std::string& path) {
    return inference_->loadModel(path);
}

std::vector<std::string> GenreClassifier::getSupportedGenres() {
    return {"House", "Techno", "Trance", "DnB", "Dubstep", "Hip-Hop", "Pop", "Rock",
            "R&B", "Reggae", "Jazz", "Classical", "Latin", "Ambient", "Funk"};
}

} // namespace BeatMate::Services::AI
