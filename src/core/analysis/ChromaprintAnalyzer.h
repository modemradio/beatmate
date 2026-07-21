#pragma once

#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

class ChromaprintAnalyzer {
public:
    ChromaprintAnalyzer();
    ~ChromaprintAnalyzer();

    std::string fingerprint(const AudioTrack& track);
    double compareFingerprints(const std::string& fp1, const std::string& fp2);
    bool isDuplicate(const AudioTrack& a, const AudioTrack& b, double threshold = 0.8);

private:
    std::vector<uint32_t> computeChromaHash(const AudioTrack& track);
    static uint32_t hashChroma(const std::vector<float>& chroma);
};

}
