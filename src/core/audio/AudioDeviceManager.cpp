#include "AudioDeviceManager.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioDeviceManager::AudioDeviceManager() = default;

AudioDeviceManager::~AudioDeviceManager() {
    shutdown();
}

bool AudioDeviceManager::initialize() {
    if (initialized_) return true;

    auto result = juceDeviceManager_.initialise(
        0,        // numInputChannelsNeeded
        2,        // numOutputChannelsNeeded
        nullptr,  // savedState
        true      // selectDefaultDeviceOnFailure
    );

    if (result.isNotEmpty()) {
        spdlog::error("AudioDeviceManager: JUCE init failed: {}", result.toStdString());
        return false;
    }

    juceDeviceManager_.addChangeListener(this);

    initialized_ = true;
    scanDevices();
    spdlog::info("AudioDeviceManager: found {} devices", devices_.size());
    return true;
}

void AudioDeviceManager::shutdown() {
    if (!initialized_) return;
    juceDeviceManager_.removeChangeListener(this);
    juceDeviceManager_.closeAudioDevice();
    devices_.clear();
    initialized_ = false;
}

void AudioDeviceManager::scanDevices() {
    devices_.clear();

    int deviceId = 0;

    auto* currentDevice = juceDeviceManager_.getCurrentAudioDevice();
    std::string currentDeviceName;
    if (currentDevice) {
        currentDeviceName = currentDevice->getName().toStdString();
    }

    for (auto* deviceType : juceDeviceManager_.getAvailableDeviceTypes()) {
        std::string hostApiName = deviceType->getTypeName().toStdString();

        auto outputNames = deviceType->getDeviceNames(false);
        for (int i = 0; i < outputNames.size(); ++i) {
            std::string name = outputNames[i].toStdString();

            auto existingIt = std::find_if(devices_.begin(), devices_.end(),
                [&name](const AudioDevice& d) { return d.name == name; });

            if (existingIt != devices_.end()) {
                existingIt->maxOutputChannels = 2;
            } else {
                AudioDevice dev;
                dev.id = deviceId++;
                dev.name = name;
                dev.maxOutputChannels = 2;
                dev.maxInputChannels = 0;
                dev.hostApi = hostApiName;
                dev.defaultSampleRate = 44100.0;
                dev.isDefaultOutput = (name == currentDeviceName);

                static const double testRates[] = {
                    8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000
                };
                for (double rate : testRates) {
                    dev.supportedSampleRates.push_back(rate);
                }

                devices_.push_back(dev);
            }
        }

        auto inputNames = deviceType->getDeviceNames(true);
        for (int i = 0; i < inputNames.size(); ++i) {
            std::string name = inputNames[i].toStdString();

            auto existingIt = std::find_if(devices_.begin(), devices_.end(),
                [&name](const AudioDevice& d) { return d.name == name; });

            if (existingIt != devices_.end()) {
                existingIt->maxInputChannels = 2;
            } else {
                AudioDevice dev;
                dev.id = deviceId++;
                dev.name = name;
                dev.maxOutputChannels = 0;
                dev.maxInputChannels = 2;
                dev.hostApi = hostApiName;
                dev.defaultSampleRate = 44100.0;
                dev.isDefaultInput = (name == currentDeviceName);

                static const double testRates[] = {
                    8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000
                };
                for (double rate : testRates) {
                    dev.supportedSampleRates.push_back(rate);
                }

                devices_.push_back(dev);
            }
        }
    }
}

std::vector<AudioDevice> AudioDeviceManager::getInputDevices() const {
    std::vector<AudioDevice> result;
    for (const auto& d : devices_) {
        if (d.maxInputChannels > 0) result.push_back(d);
    }
    return result;
}

std::vector<AudioDevice> AudioDeviceManager::getOutputDevices() const {
    std::vector<AudioDevice> result;
    for (const auto& d : devices_) {
        if (d.maxOutputChannels > 0) result.push_back(d);
    }
    return result;
}

std::vector<AudioDevice> AudioDeviceManager::getAllDevices() const {
    return devices_;
}

AudioDevice AudioDeviceManager::getDefaultInputDevice() const {
    for (const auto& d : devices_) {
        if (d.isDefaultInput) return d;
    }
    for (const auto& d : devices_) {
        if (d.maxInputChannels > 0) return d;
    }
    return {};
}

AudioDevice AudioDeviceManager::getDefaultOutputDevice() const {
    for (const auto& d : devices_) {
        if (d.isDefaultOutput) return d;
    }
    for (const auto& d : devices_) {
        if (d.maxOutputChannels > 0) return d;
    }
    return {};
}

AudioDevice AudioDeviceManager::getDevice(int id) const {
    for (const auto& d : devices_) {
        if (d.id == id) return d;
    }
    return {};
}

bool AudioDeviceManager::isDeviceAvailable(int id) const {
    for (const auto& d : devices_) {
        if (d.id == id) return true;
    }
    return false;
}

void AudioDeviceManager::refresh() {
    if (!initialized_) return;
    scanDevices();
    if (changeCallback_) changeCallback_();
}

void AudioDeviceManager::changeListenerCallback(juce::ChangeBroadcaster* /*source*/) {
    spdlog::info("AudioDeviceManager: Device configuration changed");
    scanDevices();
    if (changeCallback_) changeCallback_();
}

} // namespace BeatMate::Core
