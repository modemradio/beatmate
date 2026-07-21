#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace BeatMate::Core::Analysis {

struct CollectionRadarFeatures {
    int64_t            trackId  = 0;
    std::vector<float> mfccMean;      // 13 dims
    std::vector<float> mfccStd;       // 13 dims
    float              tempo    = 0.0f;
    float              energy   = 0.0f;
};

struct SimilarTrack {
    int64_t trackId    = 0;
    float   similarity = 0.0f;        // 0..1 (1 = identical)
};

struct CollectionRadarConfig {
    // Blend weights (should sum to ~1.0 for intuitive scores).
    float mfccMeanWeight = 0.70f;
    float mfccStdWeight  = 0.15f;
    float tempoWeight    = 0.10f;
    float energyWeight   = 0.05f;

    // tempo half-life for exponential decay (BPM)
    float tempoHalfLife  = 10.0f;

    int   fftSize        = 2048;
    int   hopSize        = 1024;
    int   melBands       = 40;
    int   mfccCoeffs     = 13;
    float melMinHz       = 20.0f;
    float melMaxHz       = 8000.0f;
};

class CollectionRadar {
public:
    CollectionRadar() = default;
    explicit CollectionRadar(const CollectionRadarConfig& cfg) : config_(cfg) {}

    // expensive: O(N log N) per frame
    CollectionRadarFeatures extract(int64_t trackId,
                                     const float* audio,
                                     int numSamples,
                                     double sampleRate);

    // Cache management (thread-safe).
    void store(const CollectionRadarFeatures& f);
    const CollectionRadarFeatures* get(int64_t trackId) const;  // nullptr if absent
    void remove(int64_t trackId);
    void clear();
    size_t size() const;

    // Cosine similarity on normalized mean MFCC, blended with tempo/energy/std
    float similarity(const CollectionRadarFeatures& a,
                     const CollectionRadarFeatures& b) const;

    float timbreSimilarity(int64_t a, int64_t b) const;

    // Top-K most similar tracks to reference (ordered by descending similarity).
    std::vector<SimilarTrack> findSimilar(int64_t referenceTrackId,
                                          int topK = 10) const;

    // Full symmetric similarity matrix in insertion-order of cached tracks.
    std::vector<std::vector<float>> computeSimilarityMatrix() const;

    std::vector<int64_t> cachedTrackIds() const;

    const CollectionRadarConfig& config() const { return config_; }
    void setConfig(const CollectionRadarConfig& c) { config_ = c; }

private:
    struct MelFilterBank {
        int numBands = 0;
        int fftSize  = 0;
        std::vector<std::vector<float>> filters; // [numBands][fftSize/2 + 1]
    };

    static MelFilterBank buildMelFilterBank(int numBands,
                                            int fftSize,
                                            double sampleRate,
                                            float minHz,
                                            float maxHz);

    static std::vector<std::vector<float>> buildDctMatrix(int numIn, int numOut);

    static float hzToMel(float hz);
    static float melToHz(float mel);

    static void applyHannWindow(float* frame, int n);

    static float cosineSimilarity(const std::vector<float>& a,
                                  const std::vector<float>& b);

    CollectionRadarConfig                                   config_{};
    mutable std::mutex                                      mutex_;
    std::unordered_map<int64_t, CollectionRadarFeatures>    cache_;
};

}
