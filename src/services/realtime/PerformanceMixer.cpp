#include "PerformanceMixer.h"

#include "../../core/audio/AudioPlayer.h"
#include "../../core/effects/CrossfadeEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace BeatMate::Services::Realtime {

PerformanceMixer::PerformanceMixer()  = default;
PerformanceMixer::~PerformanceMixer() = default;

void PerformanceMixer::setBufferSize(int maxFrames, int channels)
{
    if (channels < 1) channels = 2;
    if (maxFrames <= maxFrames_ && channels == channels_) return;
    channels_  = channels;
    maxFrames_ = std::max(maxFrames, maxFrames_);
    scratchA_.assign(static_cast<size_t>(maxFrames_ * channels_), 0.0f);
    scratchB_.assign(static_cast<size_t>(maxFrames_ * channels_), 0.0f);
}

void PerformanceMixer::setSampleRate(double sr) noexcept
{
    if (sr > 8000.0 && sr < 384000.0) sampleRate_ = sr;
}

void PerformanceMixer::setChannelGain(char channel, float linear)
{
    const float v = std::clamp(linear, 0.0f, 4.0f);
    if (channel == 'A' || channel == 'a') gainA_.store(v);
    else                                   gainB_.store(v);
}

void PerformanceMixer::setChannelVolume(char channel, float linear)
{
    const float v = std::clamp(linear, 0.0f, 1.5f);
    if (channel == 'A' || channel == 'a') volA_.store(v);
    else                                   volB_.store(v);
}

void PerformanceMixer::setCrossfader(float position)
{
    crossfader_.store(std::clamp(position, -1.0f, 1.0f));
}

void PerformanceMixer::setMasterVolume(float linear)
{
    masterVolume_.store(std::clamp(linear, 0.0f, 2.0f));
}

void PerformanceMixer::setPfl(char channel, bool enabled)
{
    if (channel == 'A' || channel == 'a') pflA_.store(enabled);
    else                                   pflB_.store(enabled);
}

void PerformanceMixer::setCueVolume(float linear)
{
    cueVolume_.store(std::clamp(linear, 0.0f, 1.5f));
}

void PerformanceMixer::setChannelEQ(char channel, float lowDb, float midDb, float highDb) noexcept
{
    const float lo = std::clamp(lowDb,  -24.0f, 24.0f);
    const float md = std::clamp(midDb,  -24.0f, 24.0f);
    const float hi = std::clamp(highDb, -24.0f, 24.0f);
    if (channel == 'A' || channel == 'a') {
        eqLowA_.store(lo);  eqMidA_.store(md);  eqHighA_.store(hi);
    } else {
        eqLowB_.store(lo);  eqMidB_.store(md);  eqHighB_.store(hi);
    }
}

void PerformanceMixer::setChannelFilter(char channel, float pos) noexcept
{
    const float p = std::clamp(pos, -1.0f, 1.0f);
    if (channel == 'A' || channel == 'a') filterA_.store(p);
    else                                   filterB_.store(p);
}

namespace {
    static inline void makeLowShelf(float fc, float dB, double sr,
                                    float& b0, float& b1, float& b2, float& a1, float& a2) {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
        const double cosw = std::cos(w0);
        const double sinw = std::sin(w0);
        const double A = std::pow(10.0, dB / 40.0);
        const double S = 1.0;
        const double alpha = sinw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/S - 1.0) + 2.0);
        const double beta  = 2.0 * std::sqrt(A) * alpha;
        const double Ap1 = A + 1.0, Am1 = A - 1.0;
        const double a0 = (Ap1) + (Am1)*cosw + beta;
        b0 = (float)( A*((Ap1) - (Am1)*cosw + beta) / a0);
        b1 = (float)(2*A*((Am1) - (Ap1)*cosw)        / a0);
        b2 = (float)( A*((Ap1) - (Am1)*cosw - beta)  / a0);
        a1 = (float)(-2*((Am1) + (Ap1)*cosw)         / a0);
        a2 = (float)(((Ap1) + (Am1)*cosw - beta)     / a0);
    }
    static inline void makeHighShelf(float fc, float dB, double sr,
                                     float& b0, float& b1, float& b2, float& a1, float& a2) {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
        const double cosw = std::cos(w0);
        const double sinw = std::sin(w0);
        const double A = std::pow(10.0, dB / 40.0);
        const double S = 1.0;
        const double alpha = sinw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/S - 1.0) + 2.0);
        const double beta  = 2.0 * std::sqrt(A) * alpha;
        const double Ap1 = A + 1.0, Am1 = A - 1.0;
        const double a0 = (Ap1) - (Am1)*cosw + beta;
        b0 = (float)( A*((Ap1) + (Am1)*cosw + beta) / a0);
        b1 = (float)(-2*A*((Am1) + (Ap1)*cosw)      / a0);
        b2 = (float)( A*((Ap1) + (Am1)*cosw - beta) / a0);
        a1 = (float)(2*((Am1) - (Ap1)*cosw)         / a0);
        a2 = (float)(((Ap1) - (Am1)*cosw - beta)    / a0);
    }
    static inline void makePeak(float fc, float dB, double sr,
                                float& b0, float& b1, float& b2, float& a1, float& a2) {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
        const double cosw = std::cos(w0);
        const double sinw = std::sin(w0);
        const double A = std::pow(10.0, dB / 40.0);
        const double Q = 0.9;
        const double alpha = sinw / (2.0 * Q);
        const double a0 = 1.0 + alpha / A;
        b0 = (float)((1.0 + alpha * A) / a0);
        b1 = (float)((-2.0 * cosw)     / a0);
        b2 = (float)((1.0 - alpha * A) / a0);
        a1 = (float)((-2.0 * cosw)     / a0);
        a2 = (float)((1.0 - alpha / A) / a0);
    }
}

void PerformanceMixer::updateChannelCoeffs(ChannelDSP& dsp, float lowDb, float midDb,
                                           float highDb, float filterPos) noexcept
{
    const double sr = sampleRate_;
    if (dsp.lastSr != sr || dsp.lastLowDb != lowDb) {
        makeLowShelf(250.0f, lowDb, sr,
                     dsp.lowShelf.b0, dsp.lowShelf.b1, dsp.lowShelf.b2,
                     dsp.lowShelf.a1, dsp.lowShelf.a2);
        dsp.lastLowDb = lowDb;
    }
    if (dsp.lastSr != sr || dsp.lastMidDb != midDb) {
        makePeak(1000.0f, midDb, sr,
                 dsp.midPeak.b0, dsp.midPeak.b1, dsp.midPeak.b2,
                 dsp.midPeak.a1, dsp.midPeak.a2);
        dsp.lastMidDb = midDb;
    }
    if (dsp.lastSr != sr || dsp.lastHighDb != highDb) {
        makeHighShelf(5000.0f, highDb, sr,
                      dsp.highShelf.b0, dsp.highShelf.b1, dsp.highShelf.b2,
                      dsp.highShelf.a1, dsp.highShelf.a2);
        dsp.lastHighDb = highDb;
    }
    if (dsp.lastSr != sr || dsp.lastFilterPos != filterPos) {
        const float ap = std::abs(filterPos);
        const float lpFc = (filterPos < 0.0f)
            ? std::pow(10.0f, 4.30103f - ap * 2.0f)
            : 20000.0f;
        const float hpFc = (filterPos > 0.0f)
            ? std::pow(10.0f, 1.30103f + ap * 2.0f)
            : 20.0f;
        dsp.lp.a = 1.0f - std::exp(-2.0f * 3.14159265f * lpFc / (float)sr);
        dsp.hp.a = 1.0f - std::exp(-2.0f * 3.14159265f * hpFc / (float)sr);
        dsp.lastFilterPos = filterPos;
    }
    dsp.lastSr = sr;
}

void PerformanceMixer::processChannelDSP(ChannelDSP& dsp, float* buf, int numFrames,
                                         int channels, float filterPos) noexcept
{
    const int chs = std::min(channels, 2);
    const float lpA = dsp.lp.a;
    const float hpA = dsp.hp.a;
    const bool useLP = filterPos < -0.001f;
    const bool useHP = filterPos >  0.001f;

    for (int f = 0; f < numFrames; ++f) {
        const int base = f * channels;

        if (chs >= 1) {
            float x = buf[base + 0];
            x = dsp.lowShelf.process (x, dsp.lowShelf.z1L,  dsp.lowShelf.z2L);
            x = dsp.midPeak.process  (x, dsp.midPeak.z1L,   dsp.midPeak.z2L);
            x = dsp.highShelf.process(x, dsp.highShelf.z1L, dsp.highShelf.z2L);
            if (useLP) { dsp.lp.zL += lpA * (x - dsp.lp.zL); x = dsp.lp.zL; }
            if (useHP) { dsp.hp.zL += hpA * (x - dsp.hp.zL); x = x - dsp.hp.zL; }
            buf[base + 0] = x;
        }
        if (chs >= 2) {
            float x = buf[base + 1];
            x = dsp.lowShelf.process (x, dsp.lowShelf.z1R,  dsp.lowShelf.z2R);
            x = dsp.midPeak.process  (x, dsp.midPeak.z1R,   dsp.midPeak.z2R);
            x = dsp.highShelf.process(x, dsp.highShelf.z1R, dsp.highShelf.z2R);
            if (useLP) { dsp.lp.zR += lpA * (x - dsp.lp.zR); x = dsp.lp.zR; }
            if (useHP) { dsp.hp.zR += hpA * (x - dsp.hp.zR); x = x - dsp.hp.zR; }
            buf[base + 1] = x;
        }
    }
}

void PerformanceMixer::processBlock(float* output, int numFrames, int channels)
{
    if (!output || numFrames <= 0 || channels <= 0) return;

    // Re-size only if a bigger block arrives than we ever sized for. Should
    if (numFrames > maxFrames_ || channels != channels_)
        setBufferSize(numFrames, channels);

    const int samples = numFrames * channels;
    std::fill_n(output, samples, 0.0f);

    std::fill_n(scratchA_.data(), samples, 0.0f);
    std::fill_n(scratchB_.data(), samples, 0.0f);
    if (deckA_) deckA_->processBlock(scratchA_.data(), numFrames, channels);
    if (deckB_) deckB_->processBlock(scratchB_.data(), numFrames, channels);

    const float fA = filterA_.load();
    const float fB = filterB_.load();
    updateChannelCoeffs(dspA_, eqLowA_.load(), eqMidA_.load(), eqHighA_.load(), fA);
    updateChannelCoeffs(dspB_, eqLowB_.load(), eqMidB_.load(), eqHighB_.load(), fB);
    processChannelDSP(dspA_, scratchA_.data(), numFrames, channels, fA);
    processChannelDSP(dspB_, scratchB_.data(), numFrames, channels, fB);

    const float gA = gainA_.load();
    const float gB = gainB_.load();
    const float vA = volA_.load();
    const float vB = volB_.load();
    const float xf = crossfader_.load();
    const float mv = masterVolume_.load();

    const float tNorm = (xf + 1.0f) * 0.5f;
    const float angle = tNorm * 1.5707963f;
    const float xfA   = std::cos(angle);
    const float xfB   = std::sin(angle);

    const float targetLineA = gA * vA * xfA * mv;
    const float targetLineB = gB * vB * xfB * mv;

    // Linear ramp from prevLine* to targetLine* over the block to suppress
    const float invDen = (numFrames > 0) ? (1.0f / static_cast<float>(numFrames)) : 0.0f;
    const float deltaA = targetLineA - prevLineA_;
    const float deltaB = targetLineB - prevLineB_;

    const int masterCh = std::min(channels, 2);
    for (int f = 0; f < numFrames; ++f) {
        const float t = static_cast<float>(f) * invDen;
        const float lineA = prevLineA_ + deltaA * t;
        const float lineB = prevLineB_ + deltaB * t;
        const int base = f * channels;
        for (int c = 0; c < masterCh; ++c) {
            output[base + c] =
                  scratchA_[base + c] * lineA
                + scratchB_[base + c] * lineB;
        }
    }
    prevLineA_ = targetLineA;
    prevLineB_ = targetLineB;

    if (channels >= 4) {
        const bool pA = pflA_.load();
        const bool pB = pflB_.load();
        const float cv = cueVolume_.load();
        const float preA = pA ? gA * cv : 0.0f;
        const float preB = pB ? gB * cv : 0.0f;
        for (int f = 0; f < numFrames; ++f) {
            const int base = f * channels;
            for (int c = 0; c < 2; ++c) {
                output[base + 2 + c] =
                      scratchA_[base + c] * preA
                    + scratchB_[base + c] * preB;
            }
        }
    }
}

}
