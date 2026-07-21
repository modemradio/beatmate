#include "MIDIEngine.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

MIDIEngine::MIDIEngine() = default;

MIDIEngine::~MIDIEngine() { shutdown(); }

bool MIDIEngine::initialize() {
    initialized_ = true;
    spdlog::info("MIDIEngine: initialized (JUCE MIDI)");
    return true;
}

void MIDIEngine::shutdown() {
    closeInput();
    closeOutput();
    initialized_ = false;
}

std::vector<MIDIDevice> MIDIEngine::getInputDevices() const {
    std::vector<MIDIDevice> devices;
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < midiInputs.size(); ++i) {
        devices.push_back({i, midiInputs[i].name.toStdString(), true, false});
    }
    return devices;
}

std::vector<MIDIDevice> MIDIEngine::getOutputDevices() const {
    std::vector<MIDIDevice> devices;
    auto midiOutputs = juce::MidiOutput::getAvailableDevices();
    for (int i = 0; i < midiOutputs.size(); ++i) {
        devices.push_back({i, midiOutputs[i].name.toStdString(), false, true});
    }
    return devices;
}

bool MIDIEngine::openInput(int deviceId) {
    try {
        auto midiInputs = juce::MidiInput::getAvailableDevices();
        if (deviceId >= 0 && deviceId < midiInputs.size()) {
            midiInput_ = juce::MidiInput::openDevice(midiInputs[deviceId].identifier, this);
            if (midiInput_) {
                midiInput_->start();
                spdlog::info("MIDIEngine: opened input {}", midiInputs[deviceId].name.toStdString());
                return true;
            }
        }
        spdlog::error("MIDIEngine: failed to open input device {}", deviceId);
    } catch (const std::exception& e) {
        spdlog::error("MIDIEngine: failed to open input: {}", e.what());
    }
    return false;
}

bool MIDIEngine::openOutput(int deviceId) {
    try {
        auto midiOutputs = juce::MidiOutput::getAvailableDevices();
        if (deviceId >= 0 && deviceId < midiOutputs.size()) {
            midiOutput_ = juce::MidiOutput::openDevice(midiOutputs[deviceId].identifier);
            if (midiOutput_) {
                spdlog::info("MIDIEngine: opened output {}", midiOutputs[deviceId].name.toStdString());
                return true;
            }
        }
        spdlog::error("MIDIEngine: failed to open output device {}", deviceId);
    } catch (const std::exception& e) {
        spdlog::error("MIDIEngine: failed to open output: {}", e.what());
    }
    return false;
}

void MIDIEngine::closeInput() {
    if (midiInput_) {
        midiInput_->stop();
        midiInput_.reset();
    }
}

void MIDIEngine::closeOutput() {
    midiOutput_.reset();
}

void MIDIEngine::handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                            const juce::MidiMessage& message) {
    if (message.getRawDataSize() >= 3) {
        const auto* rawData = message.getRawData();
        MIDIMessage msg{rawData[0], rawData[1], rawData[2], message.getTimeStamp()};
        processMessage(msg);
    }
}

void MIDIEngine::processMessage(const MIDIMessage& msg) {
    uint8_t type = msg.status & 0xF0;
    int channel = msg.status & 0x0F;

    switch (type) {
        case 0x90: // Note On
            if (msg.data2 > 0) {
                if (noteOnCb_) noteOnCb_(channel, msg.data1, msg.data2);
            } else {
                if (noteOffCb_) noteOffCb_(channel, msg.data1, 0);
            }
            break;
        case 0x80: // Note Off
            if (noteOffCb_) noteOffCb_(channel, msg.data1, msg.data2);
            break;
        case 0xB0: // Control Change
            if (ccCb_) ccCb_(channel, msg.data1, msg.data2);
            break;
    }
}

void MIDIEngine::sendNoteOn(int channel, int note, int velocity) {
    if (midiOutput_) {
        midiOutput_->sendMessageNow(
            juce::MidiMessage::noteOn(channel + 1, note, static_cast<juce::uint8>(velocity)));
    }
}

void MIDIEngine::sendNoteOff(int channel, int note) {
    if (midiOutput_) {
        midiOutput_->sendMessageNow(
            juce::MidiMessage::noteOff(channel + 1, note, static_cast<juce::uint8>(0)));
    }
}

void MIDIEngine::sendCC(int channel, int cc, int value) {
    if (midiOutput_) {
        midiOutput_->sendMessageNow(
            juce::MidiMessage::controllerEvent(channel + 1, cc, value));
    }
}

} // namespace BeatMate::Core
