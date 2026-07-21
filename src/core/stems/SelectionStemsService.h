#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace BeatMate::Core::Stems {

enum class StemKind { Vocals, Drums, Bass, Other };

struct StemRangeResult {
    bool ok = false;
    juce::String errorMessage;
    juce::AudioBuffer<float> vocals;
    juce::AudioBuffer<float> drums;
    juce::AudioBuffer<float> bass;
    juce::AudioBuffer<float> other;
    double sampleRate = 44100.0;
    double startSec = 0.0;
    double endSec = 0.0;
};

// IMPORTANT: ProgressCb and DoneCb are invoked from a worker thread,
class SelectionStemsService {
public:
    using ProgressCb = std::function<void(float pct, const juce::String& phase)>;
    using DoneCb     = std::function<void(StemRangeResult)>;

    SelectionStemsService();
    ~SelectionStemsService();

    void separateRangeAsync(const juce::File& sourceAudio,
                            double startSec, double endSec,
                            std::vector<StemKind> wantedStems,
                            ProgressCb onProgress,
                            DoneCb     onDone);

    void cancel();

    bool isBusy() const noexcept { return m_busy.load(); }

private:
    std::atomic<bool>             m_busy { false };
    std::atomic<bool>             m_cancel { false };
    std::unique_ptr<juce::Thread> m_worker;
    juce::CriticalSection         m_lock;

    bool extractRangeToWav(const juce::File& sourceAudio,
                           double startSec, double endSec,
                           juce::File& outTempWav,
                           juce::String& errorMessage);
    bool runSeparation(const juce::File& tempWav,
                       const juce::File& outDir,
                       ProgressCb onProgress,
                       juce::String& errorMessage);
    bool loadStemsFromDir(const juce::File& dir,
                          double sampleRate,
                          StemRangeResult& out,
                          juce::String& errorMessage);
};

} // namespace BeatMate::Core::Stems
