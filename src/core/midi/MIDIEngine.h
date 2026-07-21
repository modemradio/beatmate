#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Core {

struct MIDIDevice { int id; std::string name; bool isInput; bool isOutput; };
struct MIDIMessage { uint8_t status; uint8_t data1; uint8_t data2; double timestamp; };

using MIDINoteCallback = std::function<void(int channel, int note, int velocity)>;
using MIDICCCallback = std::function<void(int channel, int cc, int value)>;

class MIDIEngine : private juce::MidiInputCallback {
public:
    MIDIEngine();
    ~MIDIEngine() override;

    bool initialize();
    void shutdown();

    std::vector<MIDIDevice> getInputDevices() const;
    std::vector<MIDIDevice> getOutputDevices() const;

    bool openInput(int deviceId);
    bool openOutput(int deviceId);
    void closeInput();
    void closeOutput();

    void setNoteOnCallback(MIDINoteCallback cb) { noteOnCb_ = std::move(cb); }
    void setNoteOffCallback(MIDINoteCallback cb) { noteOffCb_ = std::move(cb); }
    void setCCCallback(MIDICCCallback cb) { ccCb_ = std::move(cb); }

    void sendNoteOn(int channel, int note, int velocity);
    void sendNoteOff(int channel, int note);
    void sendCC(int channel, int cc, int value);

    bool isInitialized() const { return initialized_; }

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;

    void processMessage(const MIDIMessage& msg);

    bool initialized_ = false;
    MIDINoteCallback noteOnCb_, noteOffCb_;
    MIDICCCallback ccCb_;

    std::unique_ptr<juce::MidiInput> midiInput_;
    std::unique_ptr<juce::MidiOutput> midiOutput_;
};

}
