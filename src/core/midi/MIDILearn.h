#pragma once
#include <atomic>
#include <functional>
#include <string>
namespace BeatMate::Core {
struct MIDILearnResult { int channel; int cc; int note; bool isCC; };
using MIDILearnCallback = std::function<void(const MIDILearnResult& result)>;
class MIDILearn {
public:
    MIDILearn() = default;
    void startLearn(const std::string& targetParameter, MIDILearnCallback callback);
    void cancelLearn();
    bool isLearning() const { return learning_.load(); }
    std::string getTargetParameter() const { return targetParam_; }
    void onCC(int channel, int cc, int value);
    void onNote(int channel, int note, int velocity);
private:
    std::atomic<bool> learning_{false};
    std::string targetParam_;
    MIDILearnCallback callback_;
};
} // namespace BeatMate::Core
