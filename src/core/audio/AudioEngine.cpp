#include "AudioEngine.h"
#include "../dsp/CompressorProcessor.h"
#include "../dsp/EQProcessor.h"
#include "../dsp/FilterProcessor.h"
#include "../dsp/DelayProcessor.h"
#include "../dsp/ReverbProcessor.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

namespace BeatMate::Core {

AudioEngine::AudioEngine()
    : masterCompressor_(std::make_unique<CompressorProcessor>()),
      masterEq_(std::make_unique<EQProcessor>()),
      masterHpf_(std::make_unique<FilterProcessor>()),
      masterLpf_(std::make_unique<FilterProcessor>()),
      masterDelay_(std::make_unique<DelayProcessor>()),
      masterReverb_(std::make_unique<ReverbProcessor>())
{
    masterHpf_->setType(FilterType::HighPass);
    masterLpf_->setType(FilterType::LowPass);
    masterHpf_->setFrequency(20.0f);          // bypass-ish
    masterLpf_->setFrequency(22000.0f);
    masterDelay_->setDelayTime(380.0f);
    masterDelay_->setFeedback(0.4f);
    masterDelay_->setMix(0.0f);               // bypass
    masterReverb_->setRoomSize(0.6f);
    masterReverb_->setDamping(0.5f);
    masterReverb_->setWet(0.0f);              // bypass
    masterReverb_->setDry(1.0f);
    spdlog::info("AudioEngine created");
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize(double sampleRate, unsigned long bufferSize, int channels) {
    if (initialized_.load()) {
        spdlog::warn("AudioEngine already initialized");
        return true;
    }

    sampleRate_.store(sampleRate);
    bufferSize_.store(bufferSize);
    channels_.store(channels);

    auto result = deviceManager_.initialise(
        0,           // numInputChannelsNeeded (0 = no input by default)
        channels,    // numOutputChannelsNeeded
        nullptr,     // savedState XML
        true,        // selectDefaultDeviceOnFailure
        {},          // preferredDefaultDeviceName
        nullptr      // preferredSetupOptions
    );

    if (result.isNotEmpty()) {
        spdlog::error("JUCE AudioDeviceManager init failed: {}", result.toStdString());
        return false;
    }


    auto* currentDevice = deviceManager_.getCurrentAudioDevice();
    if (currentDevice) {
        auto setup = deviceManager_.getAudioDeviceSetup();

        const auto rates = currentDevice->getAvailableSampleRates();
        bool rateSupported = rates.isEmpty();
        for (double r : rates)
            if (r > sampleRate - 1.0 && r < sampleRate + 1.0) { rateSupported = true; break; }
        if (rateSupported)
            setup.sampleRate = sampleRate;

        int requested = static_cast<int>(bufferSize);
        const auto available = currentDevice->getAvailableBufferSizes();
        if (!available.isEmpty()) {
            int best = available[0];
            for (int bs : available) {
                if (bs <= requested && bs > best) best = bs;
            }
            setup.bufferSize = best;
        } else {
            setup.bufferSize = requested;
        }
        deviceManager_.setAudioDeviceSetup(setup, true);
        bufferSize_.store(static_cast<unsigned long>(setup.bufferSize));
        const double actualRate = currentDevice->getCurrentSampleRate();
        if (actualRate > 0.0) sampleRate_.store(actualRate);
        setup.sampleRate = sampleRate_.load();
        const double lat = static_cast<double>(setup.bufferSize) / setup.sampleRate * 1000.0;
        spdlog::info("AudioEngine device: {} @ {}Hz, buffer={} frames, ~{:.2f} ms output latency",
                     currentDevice->getName().toStdString(),
                     setup.sampleRate, setup.bufferSize, lat);
    }

    initialized_.store(true);
    spdlog::info("AudioEngine initialized: {}Hz, {} frames, {} ch",
                 sampleRate, bufferSize_.load(), channels);
    return true;
}

void AudioEngine::shutdown() {
    if (!initialized_.load()) return;

    stop();
    deviceManager_.closeAudioDevice();
    initialized_.store(false);
    spdlog::info("AudioEngine shut down");
}

bool AudioEngine::start() {
    if (!initialized_.load() || running_.load()) return false;

    auto* currentDevice = deviceManager_.getCurrentAudioDevice();
    if (!currentDevice) {
        spdlog::error("No audio device available");
        return false;
    }

    deviceManager_.addAudioCallback(this);

    running_.store(true);
    spdlog::info("AudioEngine started");
    return true;
}

bool AudioEngine::stop() {
    if (!running_.load()) return false;

    deviceManager_.removeAudioCallback(this);
    running_.store(false);
    spdlog::info("AudioEngine stopped");
    return true;
}

bool AudioEngine::setOutputDevice(int deviceId) {
    auto* deviceType = deviceManager_.getCurrentDeviceTypeObject();
    if (!deviceType) {
        spdlog::error("No audio device type available");
        return false;
    }

    auto deviceNames = deviceType->getDeviceNames(false /* isInput */);
    if (deviceId < 0 || deviceId >= deviceNames.size()) {
        spdlog::error("Invalid output device id: {}", deviceId);
        return false;
    }

    bool wasRunning = running_.load();

    if (wasRunning) {
        drainSilence_.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stop();
    }

    auto setup = deviceManager_.getAudioDeviceSetup();
    setup.outputDeviceName = deviceNames[deviceId];
    auto result = deviceManager_.setAudioDeviceSetup(setup, true);

    outputDeviceId_ = deviceId;
    spdlog::info("Output device set to: {} ({})", deviceNames[deviceId].toStdString(), deviceId);

    if (wasRunning) {
        start();
        drainSilence_.store(false, std::memory_order_release);
    }
    return true;
}

bool AudioEngine::setInputDevice(int deviceId) {
    auto* deviceType = deviceManager_.getCurrentDeviceTypeObject();
    if (!deviceType) {
        spdlog::error("No audio device type available");
        return false;
    }

    auto deviceNames = deviceType->getDeviceNames(true /* isInput */);
    if (deviceId < 0 || deviceId >= deviceNames.size()) {
        spdlog::error("Invalid input device id: {}", deviceId);
        return false;
    }

    auto setup = deviceManager_.getAudioDeviceSetup();
    setup.inputDeviceName = deviceNames[deviceId];
    deviceManager_.setAudioDeviceSetup(setup, true);

    inputDeviceId_ = deviceId;
    spdlog::info("Input device set to: {} ({})", deviceNames[deviceId].toStdString(), deviceId);
    return true;
}

std::vector<AudioDeviceInfo> AudioEngine::getDevices() {
    std::vector<AudioDeviceInfo> devices;

    for (auto* deviceType : deviceManager_.getAvailableDeviceTypes()) {
        auto outputNames = deviceType->getDeviceNames(false);
        for (int i = 0; i < outputNames.size(); ++i) {
            AudioDeviceInfo dev;
            dev.id = static_cast<int>(devices.size());
            dev.name = outputNames[i].toStdString();
            dev.maxOutputChannels = 2; // JUCE doesn't expose this without opening the device
            dev.defaultSampleRate = 44100.0;
            devices.push_back(dev);
        }

        auto inputNames = deviceType->getDeviceNames(true);
        for (int i = 0; i < inputNames.size(); ++i) {
            bool alreadyListed = false;
            for (auto& d : devices) {
                if (d.name == inputNames[i].toStdString()) {
                    d.maxInputChannels = 2;
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed) {
                AudioDeviceInfo dev;
                dev.id = static_cast<int>(devices.size());
                dev.name = inputNames[i].toStdString();
                dev.maxInputChannels = 2;
                dev.defaultSampleRate = 44100.0;
                devices.push_back(dev);
            }
        }
    }

    auto* currentDevice = deviceManager_.getCurrentAudioDevice();
    if (currentDevice) {
        std::string currentName = currentDevice->getName().toStdString();
        for (auto& d : devices) {
            if (d.name == currentName) {
                if (d.maxOutputChannels > 0) d.isDefaultOutput = true;
                if (d.maxInputChannels > 0) d.isDefaultInput = true;
            }
        }
    }

    return devices;
}

AudioDeviceInfo AudioEngine::getDefaultOutputDevice() {
    auto devices = getDevices();
    for (auto& d : devices) {
        if (d.isDefaultOutput) return d;
    }
    if (!devices.empty()) return devices.front();
    return {};
}

AudioDeviceInfo AudioEngine::getDefaultInputDevice() {
    auto devices = getDevices();
    for (auto& d : devices) {
        if (d.isDefaultInput) return d;
    }
    return {};
}

double AudioEngine::getLatencyMs() const {
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (!device) return 0.0;
    double latencySamples = device->getOutputLatencyInSamples();
    double sr = device->getCurrentSampleRate();
    if (sr <= 0.0) return 0.0;
    return (latencySamples / sr) * 1000.0;
}

void AudioEngine::setCallback(AudioCallback callback) {
    // Lock-free publish: audio thread sees either old or new callback, never a torn state
    auto node = std::make_shared<AudioCallback>(std::move(callback));
    std::atomic_store_explicit(&userCallback_, node, std::memory_order_release);
}

void AudioEngine::setMasterVolume(float volume) {
    const float v = std::clamp(volume, 0.0f, 2.0f);
    masterVolume_.store(v);
}

void AudioEngine::setMasterCompressorAmount(float amount0to10) {
    const float a = std::clamp(amount0to10, 0.0f, 10.0f);
    compressorAmount_.store(a);
    if (!masterCompressor_) return;
    const float t01 = a / 10.0f;
    masterCompressor_->setThreshold(-30.0f * t01);
    masterCompressor_->setRatio(1.0f + 7.0f * t01);
    masterCompressor_->setAttack(10.0f);
    masterCompressor_->setRelease(100.0f);
    masterCompressor_->setKnee(6.0f);
    masterCompressor_->setMakeupGain(0.5f * a); // light auto-makeup
}

float AudioEngine::getMasterCompressorGainReduction() const {
    return masterCompressor_ ? masterCompressor_->getGainReduction() : 0.0f;
}

void AudioEngine::setMasterEqLow(float gainDb)  {
    eqLowDb_.store(std::clamp(gainDb, -24.0f, 24.0f));
    if (masterEq_) masterEq_->setLow(eqLowDb_.load());
}
void AudioEngine::setMasterEqMid(float gainDb)  {
    eqMidDb_.store(std::clamp(gainDb, -24.0f, 24.0f));
    if (masterEq_) masterEq_->setMid(eqMidDb_.load());
}
void AudioEngine::setMasterEqHigh(float gainDb) {
    eqHighDb_.store(std::clamp(gainDb, -24.0f, 24.0f));
    if (masterEq_) masterEq_->setHigh(eqHighDb_.load());
}
void AudioEngine::setMasterHpfFrequency(float hz) {
    hpfFreq_.store(std::clamp(hz, 0.0f, 22000.0f));
    if (masterHpf_ && hz > 0.0f) masterHpf_->setFrequency(hz);
}
void AudioEngine::setMasterLpfFrequency(float hz) {
    lpfFreq_.store(std::clamp(hz, 20.0f, 22000.0f));
    if (masterLpf_) masterLpf_->setFrequency(lpfFreq_.load());
}

void AudioEngine::setMasterFilterQ(float q) {
    filterQ_.store(std::clamp(q, 0.5f, 8.0f));
    if (masterHpf_) masterHpf_->setQ(filterQ_.load());
    if (masterLpf_) masterLpf_->setQ(filterQ_.load());
}

void AudioEngine::setMasterEchoAmount(float amount0to1) {
    const float a = std::clamp(amount0to1, 0.0f, 1.0f);
    echoAmount_.store(a);
    if (masterDelay_) {
        masterDelay_->setMix(a);
        masterDelay_->setFeedback(0.2f + 0.5f * a);  // more amount → longer trails
    }
}

void AudioEngine::setMasterReverbAmount(float amount0to1) {
    const float a = std::clamp(amount0to1, 0.0f, 1.0f);
    reverbAmount_.store(a);
    if (masterReverb_) {
        masterReverb_->setWet(a);
        masterReverb_->setDry(1.0f);                 // keep full dry, add wet on top
    }
}

double AudioEngine::getCpuLoad() const {
    return cpuLoad_.load();
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    int ch = std::min(numOutputChannels, channels_.load());

    size_t neededSize = static_cast<size_t>(numSamples) * ch;
    if (interleavedOutput_.size() < neededSize)
        interleavedOutput_.resize(neededSize);
    if (interleavedInput_.size() < neededSize)
        interleavedInput_.resize(neededSize);

    std::memset(interleavedOutput_.data(), 0, neededSize * sizeof(float));

    bool hasInput = false;
    if (numInputChannels > 0 && inputChannelData) {
        int inCh = std::min(numInputChannels, ch);
        std::memset(interleavedInput_.data(), 0, neededSize * sizeof(float));
        for (int s = 0; s < numSamples; ++s) {
            for (int c = 0; c < inCh; ++c) {
                if (inputChannelData[c])
                    interleavedInput_[s * inCh + c] = inputChannelData[c][s];
            }
        }
        hasInput = true;
    }

    processAudio(interleavedOutput_.data(),
                 hasInput ? interleavedInput_.data() : nullptr,
                 static_cast<unsigned long>(numSamples));

    for (int c = 0; c < numOutputChannels; ++c) {
        if (outputChannelData[c]) {
            if (c < ch) {
                for (int s = 0; s < numSamples; ++s) {
                    outputChannelData[c][s] = interleavedOutput_[s * ch + c];
                }
            } else {
                std::memset(outputChannelData[c], 0, sizeof(float) * numSamples);
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    double bufferMs = (static_cast<double>(numSamples) / sampleRate_.load()) * 1000.0;
    if (bufferMs > 0.0) {
        cpuLoad_.store(elapsedMs / bufferMs);
    }
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device) {
        const double sr = device->getCurrentSampleRate();
        sampleRate_.store(sr);
        bufferSize_.store(static_cast<unsigned long>(device->getCurrentBufferSizeSamples()));

        // Pre-alloc here: no heap allocation allowed on the audio thread
        int maxChannels = std::max(channels_.load(), 2);
        int bufSize = device->getCurrentBufferSizeSamples();
        interleavedOutput_.resize(bufSize * maxChannels, 0.0f);
        interleavedInput_.resize(bufSize * maxChannels, 0.0f);

        if (masterEq_)         masterEq_->setSampleRate(sr);
        if (masterHpf_)        masterHpf_->setSampleRate(sr);
        if (masterLpf_)        masterLpf_->setSampleRate(sr);
        if (masterDelay_)      masterDelay_->setSampleRate(sr);
        if (masterReverb_)     masterReverb_->setSampleRate(sr);
        if (masterCompressor_) masterCompressor_->setSampleRate(sr);

        masterVolumeSmoothed_.reset(sr, 0.02);              // 20 ms ramp
        masterVolumeSmoothed_.setCurrentAndTargetValue(masterVolume_.load());

        spdlog::info("AudioEngine: Device starting - {}Hz, {} samples",
                     sr,
                     device->getCurrentBufferSizeSamples());
    }
}

void AudioEngine::audioDeviceStopped() {
    spdlog::info("AudioEngine: Device stopped");
}

void AudioEngine::processAudio(float* output, const float* input, unsigned long frames) {
    int ch = channels_.load();
    unsigned long totalSamples = frames * ch;

    std::memset(output, 0, totalSamples * sizeof(float));

    if (drainSilence_.load(std::memory_order_acquire)) {
        return;
    }

    // Lock-free fetch of the user callback
    auto cbNode = std::atomic_load_explicit(&userCallback_,
                                            std::memory_order_acquire);
    if (cbNode && *cbNode) {
        (*cbNode)(output, input, frames, ch);
    }

    static unsigned long sampleCounter = 0;
    sampleCounter += frames;
    if (sampleCounter >= static_cast<unsigned long>(sampleRate_.load())) {
        sampleCounter = 0;
        float peak = 0.0f;
        for (unsigned long i = 0; i < totalSamples; ++i)
            peak = std::max(peak, std::abs(output[i]));
        spdlog::debug("AudioEngine: peak={:.4f}, frames={}, ch={}, cb={}", peak, frames, ch,
                      (cbNode && *cbNode) ? "yes" : "no");
    }

    const int frInt = static_cast<int>(frames);

    if (eqEnabled_.load(std::memory_order_relaxed) && masterEq_
        && (eqLowDb_.load(std::memory_order_relaxed)  != 0.0f
        ||  eqMidDb_.load(std::memory_order_relaxed)  != 0.0f
        ||  eqHighDb_.load(std::memory_order_relaxed) != 0.0f)) {
        masterEq_->process(output, frInt, ch);
    }

    if (filtersEnabled_.load(std::memory_order_relaxed)) {
        if (masterHpf_ && hpfFreq_.load(std::memory_order_relaxed) > 20.0f) {
            masterHpf_->process(output, frInt, ch);
        }
        if (masterLpf_ && lpfFreq_.load(std::memory_order_relaxed) < 20000.0f) {
            masterLpf_->process(output, frInt, ch);
        }
    }

    if (echoEnabled_.load(std::memory_order_relaxed)
        && echoAmount_.load(std::memory_order_relaxed) > 0.001f
        && masterDelay_) {
        masterDelay_->process(output, frInt, ch);
    }

    if (reverbEnabled_.load(std::memory_order_relaxed)
        && reverbAmount_.load(std::memory_order_relaxed) > 0.001f
        && masterReverb_) {
        masterReverb_->process(output, frInt, ch);
    }

    if (compressorEnabled_.load(std::memory_order_relaxed)
        && compressorAmount_.load(std::memory_order_relaxed) > 0.0f
        && masterCompressor_) {
        masterCompressor_->process(output, frInt, ch);
    }

    masterVolumeSmoothed_.setTargetValue(masterVolume_.load(std::memory_order_relaxed));
    if (masterVolumeSmoothed_.isSmoothing()
        || masterVolumeSmoothed_.getCurrentValue() != 1.0f) {
        for (unsigned long s = 0; s < frames; ++s) {
            const float g = masterVolumeSmoothed_.getNextValue();
            for (int c = 0; c < ch; ++c) {
                output[s * ch + c] *= g;
            }
        }
    }

    for (unsigned long i = 0; i < totalSamples; ++i) {
        output[i] = std::clamp(output[i], -1.0f, 1.0f);
    }
}

} // namespace BeatMate::Core
