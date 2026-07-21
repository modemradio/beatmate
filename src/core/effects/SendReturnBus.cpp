#include "SendReturnBus.h"
#include <algorithm>
#include <cstring>

namespace BeatMate::Core {

SendReturnBus::SendReturnBus(int numBuses, int bufferSize, int channels) {
    buses_.resize(numBuses);
    for (auto& bus : buses_) {
        bus.buffer.resize(bufferSize * channels, 0.0f);
    }
}

SendReturnBus::~SendReturnBus() = default;

void SendReturnBus::setEffect(int busId, std::unique_ptr<DSPProcessor> effect) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (busId >= 0 && busId < static_cast<int>(buses_.size())) {
        buses_[busId].effect = std::move(effect);
    }
}

void SendReturnBus::addSend(int channelId, int busId, float amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    sends_.push_back({channelId, busId, amount});
}

void SendReturnBus::removeSend(int channelId, int busId) {
    std::lock_guard<std::mutex> lock(mutex_);
    sends_.erase(std::remove_if(sends_.begin(), sends_.end(),
        [=](const SendConfig& s) { return s.channelId == channelId && s.busId == busId; }),
        sends_.end());
}

void SendReturnBus::setSendAmount(int channelId, int busId, float amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : sends_) {
        if (s.channelId == channelId && s.busId == busId) { s.amount = amount; return; }
    }
}

void SendReturnBus::clearBuses(int numSamples, int channels) {
    for (auto& bus : buses_) {
        size_t needed = numSamples * channels;
        if (bus.buffer.size() < needed) bus.buffer.resize(needed);
        std::memset(bus.buffer.data(), 0, needed * sizeof(float));
    }
}

void SendReturnBus::sendToBus(int busId, const float* input, int numSamples, int channels, float amount) {
    if (busId < 0 || busId >= static_cast<int>(buses_.size())) return;
    int total = numSamples * channels;
    for (int i = 0; i < total; ++i) {
        buses_[busId].buffer[i] += input[i] * amount;
    }
}

void SendReturnBus::processAllBuses(int numSamples, int channels) {
    for (auto& bus : buses_) {
        if (bus.effect) {
            bus.effect->process(bus.buffer.data(), numSamples, channels);
        }
    }
}

void SendReturnBus::mixBusesToOutput(float* output, int numSamples, int channels) {
    int total = numSamples * channels;
    for (auto& bus : buses_) {
        for (int i = 0; i < total; ++i) {
            output[i] += bus.buffer[i];
        }
    }
}

} // namespace BeatMate::Core
