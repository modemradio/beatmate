#include "HybridKeyDetector.h"
#include "../audio/AudioTrack.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace BeatMate::Core {

HybridKeyDetector::HybridKeyDetector()
    : detector1_(std::make_unique<KeyDetector>()),
      detector2_(std::make_unique<KeyDetector>()) {
}

HybridKeyDetector::~HybridKeyDetector() = default;

KeyResult HybridKeyDetector::detect(const AudioTrack& track) {
    auto full = detector1_->detect(track);
    if (full.confidence >= 0.75 || full.key.empty()) {
        spdlog::info("HybridKey: full-pass {} (conf={:.0f}%)",
                     full.key, full.confidence * 100);
        return full;
    }

    const int channels = track.getChannels();
    const int sr = track.getSampleRate();
    const size_t frames = track.getNumFrames();
    const float* raw = track.getRawData();
    if (channels <= 0 || sr <= 0 || frames == 0 || raw == nullptr)
        return full;

    struct Vote { KeyResult res; double weight; };
    std::vector<Vote> votes;
    votes.push_back({ full, 2.0 * std::max(0.05, full.confidence) });

    static constexpr std::pair<double, double> windows[3] = {
        { 0.0, 0.4 }, { 0.3, 0.7 }, { 0.6, 1.0 }
    };
    for (const auto& w : windows) {
        const size_t f0 = static_cast<size_t>(w.first * static_cast<double>(frames));
        const size_t f1 = static_cast<size_t>(w.second * static_cast<double>(frames));
        if (f1 <= f0 + static_cast<size_t>(sr))
            continue;
        AudioTrack sub;
        sub.loadData(raw + f0 * static_cast<size_t>(channels),
                     (f1 - f0) * static_cast<size_t>(channels), sr, channels);
        auto r = detector2_->detect(sub);
        if (r.key.empty())
            continue;
        votes.push_back({ std::move(r), 0.0 });
        votes.back().weight = std::max(0.05, votes.back().res.confidence);
    }

    std::map<std::pair<int, bool>, double> scores;
    std::map<std::pair<int, bool>, KeyResult> reps;
    double totalScore = 0.0;
    for (const auto& v : votes) {
        const auto k = std::make_pair(v.res.pitchClass, v.res.isMinor);
        scores[k] += v.weight;
        totalScore += v.weight;
        auto it = reps.find(k);
        if (it == reps.end() || v.res.confidence > it->second.confidence)
            reps[k] = v.res;
    }

    KeyResult best = full;
    double bestScore = 0.0;
    for (const auto& [k, s] : scores) {
        if (s > bestScore) {
            bestScore = s;
            best = reps[k];
        }
    }
    if (totalScore > 0.0)
        best.confidence = std::min(1.0, bestScore / totalScore);

    spdlog::info("HybridKey: low-confidence vote across {} windows -> {} (conf={:.0f}%, full was {} {:.0f}%)",
                 votes.size(), best.key, best.confidence * 100,
                 full.key, full.confidence * 100);
    return best;
}

} // namespace BeatMate::Core
