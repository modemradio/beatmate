#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

namespace BeatMate::Core {

struct MixerChannel {
    int id = 0;
    std::string name;
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};     // -1 to 1
    std::atomic<bool> muted{false};
    std::atomic<bool> solo{false};
    std::atomic<float> vuLeft{0.0f};  // VU meter
    std::atomic<float> vuRight{0.0f};
    std::atomic<float> peakLeft{0.0f};
    std::atomic<float> peakRight{0.0f};

    // Audio source callback: fills buffer with this channel's audio
    std::function<void(float*, int, int)> sourceCallback;
};

class AudioMixer {
public:
    AudioMixer(int maxChannels = 32, int bufferSize = 1024, int sampleRate = 44100);
    ~AudioMixer();

    int addChannel(const std::string& name = "");
    bool removeChannel(int channelId);

    void setChannelVolume(int channelId, float volume);
    float getChannelVolume(int channelId) const;

    void setChannelPan(int channelId, float pan);
    float getChannelPan(int channelId) const;

    void muteChannel(int channelId, bool mute);
    bool isChannelMuted(int channelId) const;

    void soloChannel(int channelId, bool solo);
    bool isChannelSoloed(int channelId) const;

    void setChannelSource(int channelId,
                          std::function<void(float*, int, int)> callback);

    // Master controls
    void setMasterVolume(float volume) { masterVolume_.store(volume); }
    float getMasterVolume() const { return masterVolume_.load(); }

    // Process and get master output
    void getMasterOutput(float* output, int numFrames, int channels);

    // VU meters
    float getChannelVU(int channelId, int channel) const;
    float getChannelPeak(int channelId, int channel) const;
    float getMasterVU(int channel) const;
    void resetPeaks();

    int getChannelCount() const;

private:
    MixerChannel* findChannel(int id);
    const MixerChannel* findChannel(int id) const;
    void updateVU(MixerChannel& ch, const float* buffer, int frames, int channels);
    bool hasSoloedChannels() const;

    std::vector<std::unique_ptr<MixerChannel>> channels_;
    mutable std::mutex channelsMutex_;
    int nextChannelId_ = 1;

    std::atomic<float> masterVolume_{1.0f};
    std::atomic<float> masterVuLeft_{0.0f};
    std::atomic<float> masterVuRight_{0.0f};

    int bufferSize_;
    int sampleRate_;
    std::vector<float> tempBuffer_;
    std::vector<float> mixBuffer_;

    static constexpr float kVuDecay = 0.95f;
    static constexpr float kPeakHoldDecay = 0.999f;
};

} // namespace BeatMate::Core
