#pragma once

#include "KeyDetector.h"
#include <memory>

namespace BeatMate::Core {

class HybridKeyDetector {
public:
    HybridKeyDetector();
    ~HybridKeyDetector();

    KeyResult detect(const AudioTrack& track);

private:
    std::unique_ptr<KeyDetector> detector1_;
    std::unique_ptr<KeyDetector> detector2_;
};

} // namespace BeatMate::Core
