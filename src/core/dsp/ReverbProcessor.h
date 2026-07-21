#pragma once

#include "DSPProcessor.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

namespace BeatMate::Core {

class ReverbProcessor : public DSPProcessor {
public:
    ReverbProcessor();
    ~ReverbProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Reverb"; }

    void setRoomSize(float size);
    void setDamping(float damp);
    void setWet(float wet);
    void setDry(float dry);
    void setWidth(float width);

    float getRoomSize() const { return roomSize_.load(); }
    float getDamping() const { return damping_.load(); }
    float getWet() const { return wet_.load(); }
    float getDry() const { return dry_.load(); }
    float getWidth() const { return width_.load(); }

private:
    struct CombFilter {
        std::vector<float> buffer;
        int bufferSize = 0;
        int index = 0;
        float filterStore = 0.0f;

        void init(int size) {
            bufferSize = size;
            buffer.resize(size, 0.0f);
            index = 0;
            filterStore = 0.0f;
        }

        float process(float input, float feedback, float damp) {
            float output = buffer[index];
            filterStore = output * (1.0f - damp) + filterStore * damp;
            buffer[index] = input + filterStore * feedback;
            index = (index + 1) % bufferSize;
            return output;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            filterStore = 0.0f;
        }
    };

    struct AllPassFilter {
        std::vector<float> buffer;
        int bufferSize = 0;
        int index = 0;

        void init(int size) {
            bufferSize = size;
            buffer.resize(size, 0.0f);
            index = 0;
        }

        float process(float input) {
            float buffered = buffer[index];
            buffer[index] = input + buffered * 0.5f;
            index = (index + 1) % bufferSize;
            return buffered - input;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
    };

    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllPasses = 4;
    static constexpr int kStereoSpread = 23;

    std::array<CombFilter, kNumCombs> combL_, combR_;
    std::array<AllPassFilter, kNumAllPasses> allPassL_, allPassR_;

    std::atomic<float> roomSize_{0.5f};
    std::atomic<float> damping_{0.5f};
    std::atomic<float> wet_{0.3f};
    std::atomic<float> dry_{0.7f};
    std::atomic<float> width_{1.0f};

    void initFilters();
};

}
