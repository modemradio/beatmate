#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include <vector>

namespace juce { class AudioFormatReader; }

namespace BeatMate::Services::AI {

class ClapEncoder {
public:
    static constexpr int kDim = 512;
    static constexpr const char* kModelVersion = "laion_clap/music_audioset_e15+mel10s-3w";

    ClapEncoder();
    explicit ClapEncoder(const juce::File& modelDir);
    ~ClapEncoder();

    static juce::File defaultModelDirectory();
    static juce::File defaultTagModelDirectory();
    static void setModelDirectoryOverride(const juce::File& dir);
    juce::File modelDirectory() const;
    bool isAvailable() const;

    std::vector<float> encodeFile(const juce::File& audioFile);

    struct LabelScore {
        std::string label;
        float score = -1.0f;
    };
    bool zeroShotAvailable() const;
    bool moodZeroShotAvailable() const;
    LabelScore bestGenreLabel(const std::vector<float>& audioVec);
    std::vector<LabelScore> topGenreLabels(const std::vector<float>& audioVec, int count);
    LabelScore bestMoodLabel(const std::vector<float>& audioVec);
    std::vector<LabelScore> topMoodLabels(const std::vector<float>& audioVec, int count);

    static float cosine(const std::vector<float>& a, const std::vector<float>& b);

    static std::vector<float> readVecFile(const juce::File& vecFile);

private:
    struct Impl;

    bool ensureLoaded();
    bool ensureLabelsLoaded();
    bool ensureMoodLabelsLoaded();
    std::vector<LabelScore> topLabelsFrom(
        const std::vector<std::pair<std::string, std::vector<float>>>& labels,
        const std::vector<float>& audioVec, int count) const;
    std::vector<float> monoWindow48k(juce::AudioFormatReader& reader, double centerFrac) const;
    std::vector<float> logMelFeatures(const std::vector<float>& audio) const;
    std::vector<float> runModel(const std::vector<float>& mono);

    std::unique_ptr<Impl> impl_;
    juce::File modelDir_;
    std::mutex loadMutex_;
    bool loadFailed_ = false;
    bool labelsFailed_ = false;
    bool moodLabelsFailed_ = false;
};

} // namespace BeatMate::Services::AI
