#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

class CompressorProcessor;
class EQProcessor;
class FilterProcessor;
class DelayProcessor;
class ReverbProcessor;

struct AudioDeviceInfo {
    int id = -1;
    std::string name;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
    double defaultSampleRate = 44100.0;
    double defaultLowInputLatency = 0.0;
    double defaultLowOutputLatency = 0.0;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
};

using AudioCallback = std::function<void(float*, const float*, unsigned long, int)>;

class AudioEngine : public juce::AudioIODeviceCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool initialize(double sampleRate = 44100.0, unsigned long bufferSize = 256, int channels = 2);
    void shutdown();

    bool start();
    bool stop();

    bool setOutputDevice(int deviceId);
    bool setInputDevice(int deviceId);

    std::vector<AudioDeviceInfo> getDevices();
    AudioDeviceInfo getDefaultOutputDevice();
    AudioDeviceInfo getDefaultInputDevice();

    double getSampleRate() const { return sampleRate_.load(); }
    unsigned long getBufferSize() const { return bufferSize_.load(); }
    int getChannels() const { return channels_.load(); }
    double getLatencyMs() const;
    double latencyMs() const { return getLatencyMs(); }

    bool isRunning() const { return running_.load(); }
    bool isInitialized() const { return initialized_.load(); }

    void setCallback(AudioCallback callback);
    void setMasterVolume(float volume);
    float getMasterVolume() const { return masterVolume_.load(); }

    void setMasterCompressorAmount(float amount0to10);
    float getMasterCompressorAmount() const { return compressorAmount_.load(); }
    float getMasterCompressorGainReduction() const;

    void setMasterEqLow(float gainDb);
    void setMasterEqMid(float gainDb);
    void setMasterEqHigh(float gainDb);
    float getMasterEqLow()  const { return eqLowDb_.load(); }
    float getMasterEqMid()  const { return eqMidDb_.load(); }
    float getMasterEqHigh() const { return eqHighDb_.load(); }

    void setMasterHpfFrequency(float hz);
    void setMasterLpfFrequency(float hz);
    float getMasterHpfFrequency() const { return hpfFreq_.load(); }
    float getMasterLpfFrequency() const { return lpfFreq_.load(); }

    void setMasterFilterQ(float q);
    float getMasterFilterQ() const { return filterQ_.load(); }

    void setMasterEchoAmount(float amount0to1);
    float getMasterEchoAmount() const { return echoAmount_.load(); }

    void setMasterReverbAmount(float amount0to1);
    float getMasterReverbAmount() const { return reverbAmount_.load(); }

    void setMasterEqEnabled       (bool b) { eqEnabled_.store(b); }
    void setMasterFiltersEnabled  (bool b) { filtersEnabled_.store(b); }
    void setMasterEchoEnabled     (bool b) { echoEnabled_.store(b); }
    void setMasterReverbEnabled   (bool b) { reverbEnabled_.store(b); }
    void setMasterCompressorEnabled(bool b){ compressorEnabled_.store(b); }
    bool isMasterEqEnabled()        const { return eqEnabled_.load(); }
    bool isMasterFiltersEnabled()   const { return filtersEnabled_.load(); }
    bool isMasterEchoEnabled()      const { return echoEnabled_.load(); }
    bool isMasterReverbEnabled()    const { return reverbEnabled_.load(); }
    bool isMasterCompressorEnabled()const { return compressorEnabled_.load(); }

    double getCpuLoad() const;

    juce::AudioDeviceManager& getJuceDeviceManager() { return deviceManager_; }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void processAudio(float* output, const float* input, unsigned long frames);

    juce::AudioDeviceManager deviceManager_;
    std::atomic<double> sampleRate_{44100.0};
    std::atomic<unsigned long> bufferSize_{256};
    std::atomic<int> channels_{2};
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<float> masterVolume_{1.0f};
    juce::SmoothedValue<float> masterVolumeSmoothed_{1.0f};

    int outputDeviceId_ = -1;
    int inputDeviceId_ = -1;

    std::atomic<double> cpuLoad_{0.0};

    // Lock-free callback swap. The audio thread used to take a try_lock on
    std::shared_ptr<AudioCallback> userCallback_;

    std::atomic<bool> drainSilence_{false};

    // Pre-allocated buffers for audio callback (avoid heap allocation on audio thread)
    std::vector<float> interleavedOutput_;
    std::vector<float> interleavedInput_;

    std::unique_ptr<CompressorProcessor> masterCompressor_;
    std::atomic<float> compressorAmount_{0.0f};

    std::unique_ptr<EQProcessor>     masterEq_;
    std::unique_ptr<FilterProcessor> masterHpf_;
    std::unique_ptr<FilterProcessor> masterLpf_;
    std::atomic<float> eqLowDb_{0.0f};
    std::atomic<float> eqMidDb_{0.0f};
    std::atomic<float> eqHighDb_{0.0f};
    std::atomic<float> hpfFreq_{0.0f};
    std::atomic<float> lpfFreq_{22000.0f};
    std::atomic<float> filterQ_{0.707f};

    std::unique_ptr<DelayProcessor>  masterDelay_;
    std::unique_ptr<ReverbProcessor> masterReverb_;
    std::atomic<float> echoAmount_{0.0f};
    std::atomic<float> reverbAmount_{0.0f};

    std::atomic<bool> eqEnabled_{true};
    std::atomic<bool> filtersEnabled_{true};
    std::atomic<bool> echoEnabled_{true};
    std::atomic<bool> reverbEnabled_{true};
    std::atomic<bool> compressorEnabled_{true};
};

}
