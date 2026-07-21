#include "RealtimePitchProcessor.h"

#include "../audio/AudioPlayer.h"
#include "../audio/AudioTrack.h"
#include "SoundTouchWrapper.h"
#include "SignalsmithWrapper.h"

#include <SoundTouch.h>
#if __has_include(<signalsmith-stretch.h>)
  #define HAS_SIGNALSMITH 1
  #include <signalsmith-stretch.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

//  Realtime DSP state hidden via a static side-map keyed by 'this'.
struct RealtimePitchProcessor_State {
    soundtouch::SoundTouch st;
    bool stConfigured = false;
#ifdef HAS_SIGNALSMITH
    signalsmith::stretch::SignalsmithStretch<float> sig;
    std::vector<std::vector<float>> sigInCh;
    std::vector<std::vector<float>> sigOutCh;
    bool sigConfigured = false;
#endif
};

static std::mutex g_stateMutex;
static std::unordered_map<const RealtimePitchProcessor*,
                          std::unique_ptr<RealtimePitchProcessor_State>> g_stateMap;

static RealtimePitchProcessor_State& getState(const RealtimePitchProcessor* self) {
    std::lock_guard<std::mutex> lk(g_stateMutex);
    auto it = g_stateMap.find(self);
    if (it == g_stateMap.end()) {
        auto p = std::make_unique<RealtimePitchProcessor_State>();
        auto* raw = p.get();
        g_stateMap.emplace(self, std::move(p));
        return *raw;
    }
    return *it->second;
}
static void dropState(const RealtimePitchProcessor* self) {
    std::lock_guard<std::mutex> lk(g_stateMutex);
    g_stateMap.erase(self);
}

namespace {
size_t nextPow2(size_t v) { size_t x = 1; while (x < v) x <<= 1; return x; }
}

RealtimePitchProcessor::RealtimePitchProcessor() {
    ringMask_ = nextPow2(48000 * 2) - 1;
    outputRing_.assign(ringMask_ + 1, 0.0f);
    pullScratch_.reserve(16384);
    (void)getState(this); // eager-allocate side state
}

RealtimePitchProcessor::~RealtimePitchProcessor() {
    dropState(this);
}

void RealtimePitchProcessor::setPlayer(AudioPlayer* player) {
    player_ = player;
    reset();
}

void RealtimePitchProcessor::prepare(int sampleRate, int channels) {
    if (channels <= 0) channels = 2;
    if (sampleRate <= 0) sampleRate = 48000;
    const size_t neededRing = nextPow2(static_cast<size_t>(sampleRate) * channels * 2);
    if (sampleRate_ != sampleRate || channels_ != channels
        || neededRing != ringMask_ + 1) {
        sampleRate_ = sampleRate;
        channels_ = channels;
        ringMask_ = neededRing - 1;
        outputRing_.assign(ringMask_ + 1, 0.0f);
        ringRead_ = ringWrite_ = 0;

        auto& s = getState(this);
        s.stConfigured = false;
#ifdef HAS_SIGNALSMITH
        s.sigConfigured = false;
#endif
    }
    configureFromParams();
}

void RealtimePitchProcessor::setMode(Mode m) {
    if (m != mode_.load()) {
        mode_.store(m);
        reset();
    }
}

void RealtimePitchProcessor::setPitchSemitones(double st) {
    st = std::clamp(st, -24.0, 24.0);
    pitchSt_.store(st);
}

void RealtimePitchProcessor::setTempoRatio(double ratio) {
    ratio = std::clamp(ratio, 0.25, 4.0);
    tempoRatio_.store(ratio);
}

void RealtimePitchProcessor::setBeatPositions(std::vector<double> beats) {
    std::lock_guard<std::mutex> lk(beatsMutex_);
    beatPositions_ = std::move(beats);
    nextBeatIdx_ = 0;
    lastPlayerPosForBeatSync_ = -1.0;
}

void RealtimePitchProcessor::reset() {
    ringRead_ = ringWrite_ = 0;
    if (!outputRing_.empty())
        std::fill(outputRing_.begin(), outputRing_.end(), 0.0f);
    auto& s = getState(this);
    s.st.clear();
#ifdef HAS_SIGNALSMITH
    if (s.sigConfigured) s.sig.reset();
#endif
    {
        std::lock_guard<std::mutex> lk(beatsMutex_);
        nextBeatIdx_ = 0;
        lastPlayerPosForBeatSync_ = -1.0;
    }
    lastAppliedMode_ = mode_.load();
    lastAppliedPitch_ = pitchSt_.load();
    lastAppliedTempo_ = tempoRatio_.load();
}

void RealtimePitchProcessor::configureFromParams() {
    auto& s = getState(this);
    const Mode m = mode_.load();
    const double st = pitchSt_.load();
    const double tempo = tempoRatio_.load();

    if (m == Mode::RePitch || m == Mode::BeatSlice) {
        if (!s.stConfigured) {
            s.st.setSampleRate(sampleRate_);
            s.st.setChannels(channels_);
            s.st.setSetting(SETTING_USE_AA_FILTER, 1);
            s.st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
            s.st.setSetting(SETTING_SEQUENCE_MS, 40);
            s.st.setSetting(SETTING_SEEKWINDOW_MS, 15);
            s.st.setSetting(SETTING_OVERLAP_MS, 8);
            s.stConfigured = true;
        }
        s.st.setPitchSemiTones(static_cast<float>(st));
        s.st.setTempo(tempo);
        s.st.setRate(1.0);
    }

#ifdef HAS_SIGNALSMITH
    if (m == Mode::FormantCorrection) {
        if (!s.sigConfigured) {
            s.sig.presetDefault(channels_, static_cast<float>(sampleRate_));
            s.sigInCh.assign(channels_, {});
            s.sigOutCh.assign(channels_, {});
            s.sigConfigured = true;
        }
        s.sig.setTransposeSemitones(static_cast<float>(st));
    }
#endif
    (void)tempo; // used above via setTempo(); silence unused warning if SL stripped.
}

void RealtimePitchProcessor::pullFromPlayer(float* scratch, int numFrames, int channels) {
    if (!player_) {
        std::fill(scratch, scratch + numFrames * channels, 0.0f);
        return;
    }
    const Mode m = mode_.load();
    if (m != Mode::Vinyl) {
        player_->setSpeed(1.0);
    }
    player_->processBlock(scratch, numFrames, channels);
}

void RealtimePitchProcessor::processBlock(float* out, int numFrames, int channels) {
    if (numFrames <= 0 || channels <= 0) return;
    if (!player_) {
        std::fill(out, out + numFrames * channels, 0.0f);
        return;
    }
    if (channels != channels_) {
        prepare(sampleRate_, channels);
    }

    const Mode m = mode_.load();

    if (m != lastAppliedMode_
        || std::fabs(pitchSt_.load() - lastAppliedPitch_) > 1e-6
        || std::fabs(tempoRatio_.load() - lastAppliedTempo_) > 1e-6) {
        configureFromParams();
        lastAppliedMode_ = m;
        lastAppliedPitch_ = pitchSt_.load();
        lastAppliedTempo_ = tempoRatio_.load();
    }

    if (m == Mode::Vinyl) {
        const double ratio = std::pow(2.0, pitchSt_.load() / 12.0);
        player_->setSpeed(ratio);
        player_->processBlock(out, numFrames, channels);
        return;
    }

    if (m == Mode::RePitch || m == Mode::BeatSlice) {
        auto& s = getState(this);

        if (m == Mode::BeatSlice) {
            std::lock_guard<std::mutex> lk(beatsMutex_);
            const double curPos = player_->getPosition();
            if (curPos < lastPlayerPosForBeatSync_) nextBeatIdx_ = 0;
            while (nextBeatIdx_ < beatPositions_.size()
                   && beatPositions_[nextBeatIdx_] <= curPos) {
                // Fresh slice on every beat: discard lingering input so the
                s.st.clear();
                ++nextBeatIdx_;
            }
            lastPlayerPosForBeatSync_ = curPos;
        }

        const int chunkFrames = 1024;
        pullScratch_.resize(static_cast<size_t>(chunkFrames) * channels);
        float recvBuf[8192];
        const int maxRecvFrames = 8192 / channels;

        int produced = 0;
        while (produced < numFrames) {
            unsigned int avail = s.st.numSamples();
            while (avail < static_cast<unsigned int>(numFrames - produced)) {
                pullFromPlayer(pullScratch_.data(), chunkFrames, channels);
                s.st.putSamples(pullScratch_.data(),
                                static_cast<unsigned int>(chunkFrames));
                const unsigned int newAvail = s.st.numSamples();
                if (newAvail == avail) break; // player producing silence
                avail = newAvail;
                if (avail >= static_cast<unsigned int>(numFrames - produced)) break;
            }
            const int want = std::min(numFrames - produced, maxRecvFrames);
            const unsigned int got = s.st.receiveSamples(
                recvBuf, static_cast<unsigned int>(want));
            if (got == 0) break;
            std::memcpy(out + produced * channels, recvBuf,
                        got * channels * sizeof(float));
            produced += static_cast<int>(got);
        }
        if (produced < numFrames) {
            std::fill(out + produced * channels,
                      out + numFrames * channels, 0.0f);
        }
        return;
    }

#ifdef HAS_SIGNALSMITH
    if (m == Mode::FormantCorrection) {
        auto& s = getState(this);
        if (!s.sigConfigured) configureFromParams();

        const double tempo = tempoRatio_.load();
        const int outFrames = numFrames;
        const int inFrames = std::max(1,
            static_cast<int>(std::round(outFrames / std::max(0.01, tempo))));

        for (int ch = 0; ch < channels; ++ch) {
            s.sigInCh[ch].resize(inFrames);
            s.sigOutCh[ch].resize(outFrames);
        }
        pullScratch_.resize(static_cast<size_t>(inFrames) * channels);
        pullFromPlayer(pullScratch_.data(), inFrames, channels);

        for (int i = 0; i < inFrames; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                s.sigInCh[ch][i] = pullScratch_[i * channels + ch];
            }
        }

        std::vector<float*> inPtrs(channels), outPtrs(channels);
        for (int ch = 0; ch < channels; ++ch) {
            inPtrs[ch]  = s.sigInCh[ch].data();
            outPtrs[ch] = s.sigOutCh[ch].data();
        }
        s.sig.process(inPtrs.data(), inFrames, outPtrs.data(), outFrames);

        for (int i = 0; i < outFrames; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                out[i * channels + ch] = s.sigOutCh[ch][i];
            }
        }
        return;
    }
#else
    if (m == Mode::FormantCorrection) {
        // Library missing at compile time → degrade to RePitch.
        setMode(Mode::RePitch);
        processBlock(out, numFrames, channels);
        return;
    }
#endif

    // Unknown mode defensive silence.
    std::fill(out, out + numFrames * channels, 0.0f);
}

} // namespace BeatMate::Core
