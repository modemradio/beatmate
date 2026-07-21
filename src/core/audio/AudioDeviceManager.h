#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <string>
#include <vector>
#include <functional>

namespace BeatMate::Core {

struct AudioDevice {
    int id = -1;
    std::string name;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
    std::vector<double> supportedSampleRates;
    double defaultSampleRate = 44100.0;
    double defaultLatency = 0.0;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
    std::string hostApi;
};

using DeviceChangeCallback = std::function<void()>;

class AudioDeviceManager : private juce::ChangeListener {
public:
    AudioDeviceManager();
    ~AudioDeviceManager() override;

    bool initialize();
    void shutdown();

    std::vector<AudioDevice> getInputDevices() const;
    std::vector<AudioDevice> getOutputDevices() const;
    std::vector<AudioDevice> getAllDevices() const;

    AudioDevice getDefaultInputDevice() const;
    AudioDevice getDefaultOutputDevice() const;
    AudioDevice getDevice(int id) const;

    bool isDeviceAvailable(int id) const;

    void refresh();

    void setDeviceChangeCallback(DeviceChangeCallback cb) { changeCallback_ = std::move(cb); }

    juce::AudioDeviceManager& getJuceDeviceManager() { return juceDeviceManager_; }

private:
    void scanDevices();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::AudioDeviceManager juceDeviceManager_;
    std::vector<AudioDevice> devices_;
    bool initialized_ = false;
    DeviceChangeCallback changeCallback_;
};

} // namespace BeatMate::Core
