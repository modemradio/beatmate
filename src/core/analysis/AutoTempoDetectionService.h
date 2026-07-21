#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct TempoSegment {
    double startTime = 0.0;
    double endTime = 0.0;
    double bpm = 0.0;
    float confidence = 0.0f;
};

struct TempoMap {
    std::vector<TempoSegment> segments;
    double averageBPM = 0.0;
    double minBPM = 0.0;
    double maxBPM = 0.0;
    bool isVariableTempo = false;
    double tempoVariation = 0.0;  // percentage
};

class AutoTempoDetectionService {
public:
    AutoTempoDetectionService();
    ~AutoTempoDetectionService();

    TempoMap detect(const AudioTrack& track);

    void setSegmentDuration(double seconds) { segmentDuration_ = seconds; }
    void setMinBPM(double min) { minBPM_ = min; }
    void setMaxBPM(double max) { maxBPM_ = max; }
    void setVariationThreshold(double pct) { variationThreshold_ = pct; }

private:
    double estimateSegmentBPM(const float* data, size_t numSamples, int sampleRate);
    std::vector<TempoSegment> mergeSegments(const std::vector<TempoSegment>& segments, double tolerance);

    double segmentDuration_ = 4.0;   // Analyze per 4-second segments
    double minBPM_ = 60.0;
    double maxBPM_ = 200.0;
    double variationThreshold_ = 2.0; // 2% variation = variable tempo
};

} // namespace BeatMate::Core
