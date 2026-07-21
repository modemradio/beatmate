#include "FilterProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

FilterProcessor::FilterProcessor() {
    recalculate();
}

FilterProcessor::~FilterProcessor() = default;

void FilterProcessor::setType(FilterType type) {
    type_ = type;
    recalculate();
}

void FilterProcessor::setFrequency(float freq) {
    frequency_.store(std::clamp(freq, 20.0f, 20000.0f));
    recalculate();
}

void FilterProcessor::setQ(float q) {
    q_.store(std::clamp(q, 0.1f, 30.0f));
    recalculate();
}

void FilterProcessor::setGain(float gainDb) {
    gainDb_.store(std::clamp(gainDb, -24.0f, 24.0f));
    recalculate();
}

void FilterProcessor::setOrder(int order) {
    order_ = std::clamp(order, 1, kMaxStages);
    reset();
}

void FilterProcessor::recalculate() {
    double freq = frequency_.load();
    double q = q_.load();
    double gain = gainDb_.load();

    double w0 = 2.0 * kPi * freq / sampleRate_;
    double cosW0 = std::cos(w0);
    double sinW0 = std::sin(w0);
    double alpha = sinW0 / (2.0 * q);

    double A = std::pow(10.0, gain / 40.0);

    switch (type_) {
        case FilterType::LowPass:
            coeffs_.b0 = (1.0 - cosW0) / 2.0;
            coeffs_.b1 = 1.0 - cosW0;
            coeffs_.b2 = (1.0 - cosW0) / 2.0;
            coeffs_.a0 = 1.0 + alpha;
            coeffs_.a1 = -2.0 * cosW0;
            coeffs_.a2 = 1.0 - alpha;
            break;

        case FilterType::HighPass:
            coeffs_.b0 = (1.0 + cosW0) / 2.0;
            coeffs_.b1 = -(1.0 + cosW0);
            coeffs_.b2 = (1.0 + cosW0) / 2.0;
            coeffs_.a0 = 1.0 + alpha;
            coeffs_.a1 = -2.0 * cosW0;
            coeffs_.a2 = 1.0 - alpha;
            break;

        case FilterType::BandPass:
            coeffs_.b0 = alpha;
            coeffs_.b1 = 0.0;
            coeffs_.b2 = -alpha;
            coeffs_.a0 = 1.0 + alpha;
            coeffs_.a1 = -2.0 * cosW0;
            coeffs_.a2 = 1.0 - alpha;
            break;

        case FilterType::Notch:
            coeffs_.b0 = 1.0;
            coeffs_.b1 = -2.0 * cosW0;
            coeffs_.b2 = 1.0;
            coeffs_.a0 = 1.0 + alpha;
            coeffs_.a1 = -2.0 * cosW0;
            coeffs_.a2 = 1.0 - alpha;
            break;

        case FilterType::LowShelf: {
            double sqrtA = std::sqrt(A);
            coeffs_.b0 = A * ((A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
            coeffs_.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
            coeffs_.b2 = A * ((A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
            coeffs_.a0 = (A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
            coeffs_.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
            coeffs_.a2 = (A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
            break;
        }

        case FilterType::HighShelf: {
            double sqrtA = std::sqrt(A);
            coeffs_.b0 = A * ((A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
            coeffs_.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
            coeffs_.b2 = A * ((A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
            coeffs_.a0 = (A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
            coeffs_.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
            coeffs_.a2 = (A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
            break;
        }

        case FilterType::Peak:
            coeffs_.b0 = 1.0 + alpha * A;
            coeffs_.b1 = -2.0 * cosW0;
            coeffs_.b2 = 1.0 - alpha * A;
            coeffs_.a0 = 1.0 + alpha / A;
            coeffs_.a1 = -2.0 * cosW0;
            coeffs_.a2 = 1.0 - alpha / A;
            break;
    }

    double invA0 = 1.0 / coeffs_.a0;
    coeffs_.b0 *= invA0;
    coeffs_.b1 *= invA0;
    coeffs_.b2 *= invA0;
    coeffs_.a1 *= invA0;
    coeffs_.a2 *= invA0;
    coeffs_.a0 = 1.0;
}

void FilterProcessor::processBiquad(float* buffer, int numSamples, int channels, int stage) {
    for (int ch = 0; ch < std::min(channels, kMaxChannels); ++ch) {
        auto& s = state_[ch][stage];

        for (int i = 0; i < numSamples; ++i) {
            double x = buffer[i * channels + ch];
            double y = coeffs_.b0 * x + coeffs_.b1 * s.x1 + coeffs_.b2 * s.x2
                       - coeffs_.a1 * s.y1 - coeffs_.a2 * s.y2;

            s.x2 = s.x1;
            s.x1 = x;
            s.y2 = s.y1;
            s.y1 = y;

            buffer[i * channels + ch] = static_cast<float>(y);
        }
    }
}

void FilterProcessor::process(float* buffer, int numSamples, int channels) {
    for (int stage = 0; stage < order_; ++stage) {
        processBiquad(buffer, numSamples, channels, stage);
    }
}

void FilterProcessor::reset() {
    for (auto& channelStates : state_) {
        for (auto& s : channelStates) {
            s = {};
        }
    }
}

} // namespace BeatMate::Core
