#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace BeatMate::Core {

class AudioPlayer;
class SoundTouchWrapper;
class SignalsmithWrapper;

class RealtimePitchProcessor {
public:
    enum class Mode {
        Vinyl = 0,          // setSpeed on AudioPlayer; pitch rides with speed
        RePitch,            // SoundTouch: pitch only, tempo preserved
        BeatSlice,          // SoundTouch + periodic flush on beat grid
        FormantCorrection   // Signalsmith: pitch + optional stretch + formants
    };

    RealtimePitchProcessor();
    ~RealtimePitchProcessor();

    // Non-owning. Player's lifetime must outlive the processor.
    void setPlayer(AudioPlayer* player);
    AudioPlayer* getPlayer() const { return player_; }

    // Engine reconfigures on sample rate / channel change (cheap when same).
    void prepare(int sampleRate, int channels);

    void setMode(Mode m);
    Mode getMode() const { return mode_.load(); }

    // Pitch in semitones (clamped ±24).
    void setPitchSemitones(double st);
    double getPitchSemitones() const { return pitchSt_.load(); }

    // Tempo ratio: 1.0 = no stretch (hors mode Vinyl)
    void setTempoRatio(double ratio);
    double getTempoRatio() const { return tempoRatio_.load(); }

    // Beat grid (absolute seconds). Copied; safe to set from any thread.
    void setBeatPositions(std::vector<double> beats);

    // Writes `numFrames * channels` floats to `out`. Thread: audio callback.
    void processBlock(float* out, int numFrames, int channels);

    // Resets internal buffers & DSP state (call on seek / track change).
    void reset();

private:
    // pulls raw PCM ignoring the player's own speed
    void pullFromPlayer(float* scratch, int numFrames, int channels);

    void ensureSoundTouch();
    void ensureSignalsmith();

    AudioPlayer* player_ = nullptr;

    // defaut FormantCorrection : Signalsmith preserve les formants
    std::atomic<Mode> mode_{Mode::FormantCorrection};
    std::atomic<double> pitchSt_{0.0};
    std::atomic<double> tempoRatio_{1.0};

    int sampleRate_ = 44100;
    int channels_ = 2;

    std::unique_ptr<SoundTouchWrapper> soundTouch_;
    std::unique_ptr<SignalsmithWrapper> signalsmith_;

    // Ring buffer of produced samples (interleaved) — feeds processBlock().
    std::vector<float> outputRing_;
    size_t ringRead_ = 0;
    size_t ringWrite_ = 0;
    size_t ringMask_ = 0; // always power-of-two - 1

    std::vector<float> pullScratch_;

    std::vector<double> beatPositions_;
    std::mutex beatsMutex_;
    size_t nextBeatIdx_ = 0;
    double lastPlayerPosForBeatSync_ = -1.0;

    Mode lastAppliedMode_ = Mode::FormantCorrection;
    double lastAppliedPitch_ = 0.0;
    double lastAppliedTempo_ = 1.0;

    void configureFromParams();
};

}
