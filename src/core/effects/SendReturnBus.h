#pragma once
#include "../dsp/DSPProcessor.h"
#include <memory>
#include <mutex>
#include <vector>

namespace BeatMate::Core {

struct SendConfig { int channelId; int busId; float amount; };

class SendReturnBus {
public:
    explicit SendReturnBus(int numBuses = 4, int bufferSize = 1024, int channels = 2);
    ~SendReturnBus();

    void setEffect(int busId, std::unique_ptr<DSPProcessor> effect);
    void addSend(int channelId, int busId, float amount);
    void removeSend(int channelId, int busId);
    void setSendAmount(int channelId, int busId, float amount);

    void clearBuses(int numSamples, int channels);
    void sendToBus(int busId, const float* input, int numSamples, int channels, float amount);
    void processAllBuses(int numSamples, int channels);
    void mixBusesToOutput(float* output, int numSamples, int channels);

private:
    struct Bus { std::vector<float> buffer; std::unique_ptr<DSPProcessor> effect; };
    std::vector<Bus> buses_;
    std::vector<SendConfig> sends_;
    std::mutex mutex_;
};

} // namespace BeatMate::Core
