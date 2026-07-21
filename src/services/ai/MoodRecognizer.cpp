#include <algorithm>
#include "MoodRecognizer.h"
#include "ONNXInference.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::AI {

MoodRecognizer::MoodRecognizer(std::shared_ptr<ONNXInference> inference)
    : inference_(std::move(inference)) {}

MoodResult MoodRecognizer::recognize(const Models::TrackFeatures& features) {
    std::vector<float> input;
    input.insert(input.end(), features.mfcc.begin(), features.mfcc.end());
    input.push_back(features.harmonicRatio);
    input.push_back(features.spectralContrast);

    auto output = inference_->run(input);

    static const std::vector<std::string> moods = {"Happy", "Sad", "Energetic", "Calm", "Dark", "Euphoric", "Melancholic", "Aggressive"};

    MoodResult result;
    float maxConf = 0.0f;
    for (size_t i = 0; i < std::min(output.size(), moods.size()); ++i) {
        if (output[i] > maxConf) {
            maxConf = output[i];
            result.mood = moods[i];
            result.confidence = output[i];
        }
    }

    if (output.size() >= 2) {
        result.valence = output[0];
        result.arousal = output.size() > 1 ? output[1] : 0.5f;
    }

    spdlog::debug("MoodRecognizer: Detected mood '{}' ({:.1f}%)", result.mood, result.confidence * 100);
    return result;
}

bool MoodRecognizer::loadModel(const std::string& path) { return inference_->loadModel(path); }

}
