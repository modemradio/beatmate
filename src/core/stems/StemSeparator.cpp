#include "StemSeparator.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "../dsp/FilterProcessor.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

StemSeparator::StemSeparator() = default;
StemSeparator::~StemSeparator() = default;

StemResult StemSeparator::separate(const AudioTrack& track, StemProgressCallback progress) {
    return separateWithSpectral(track, progress);
}

StemResult StemSeparator::separateWithSpectral(const AudioTrack& track, StemProgressCallback progress) {
    spdlog::info("StemSeparator: using spectral separation for {}", track.getFilePath());

    StemResult result;
    int sr = track.getSampleRate();
    int ch = track.getChannels();
    size_t totalSamples = track.getTotalSamples();
    const float* data = track.getRawData();

    auto makeFilteredTrack = [&](FilterType type, float freq, float q) -> std::shared_ptr<AudioTrack> {
        auto stemTrack = std::make_shared<AudioTrack>();
        std::vector<float> stemData(data, data + totalSamples);

        FilterProcessor filter;
        filter.setSampleRate(sr);
        filter.setType(type);
        filter.setFrequency(freq);
        filter.setQ(q);
        filter.process(stemData.data(), static_cast<int>(totalSamples / ch), ch);

        stemTrack->loadData(std::move(stemData), sr, ch);
        return stemTrack;
    };

    if (progress) progress(0.1f, "Separating bass");
    result.bass = makeFilteredTrack(FilterType::LowPass, 200.0f, 0.707f);

    if (progress) progress(0.3f, "Separating drums");
    {
        auto drumTrack = std::make_shared<AudioTrack>();
        std::vector<float> drumData(data, data + totalSamples);

        FilterProcessor hpFilter, lpFilter;
        hpFilter.setSampleRate(sr);
        hpFilter.setType(FilterType::HighPass);
        hpFilter.setFrequency(100.0f);
        hpFilter.setQ(0.707f);
        hpFilter.process(drumData.data(), static_cast<int>(totalSamples / ch), ch);

        lpFilter.setSampleRate(sr);
        lpFilter.setType(FilterType::LowPass);
        lpFilter.setFrequency(8000.0f);
        lpFilter.setQ(0.707f);
        lpFilter.process(drumData.data(), static_cast<int>(totalSamples / ch), ch);

        float prevSample = 0.0f;
        for (size_t i = 0; i < totalSamples; ++i) {
            const float orig = drumData[i];
            const float transient = std::fabs(orig - prevSample);
            drumData[i] = orig * (1.0f + transient * 2.0f);
            prevSample = orig;
        }

        drumTrack->loadData(std::move(drumData), sr, ch);
        result.drums = drumTrack;
    }

    if (progress) progress(0.6f, "Separating vocals");
    if (ch >= 2) {
        auto vocalTrack = std::make_shared<AudioTrack>();
        std::vector<float> vocalData(totalSamples, 0.0f);
        size_t frames = totalSamples / ch;

        for (size_t i = 0; i < frames; ++i) {
            const float L = data[i * ch];
            const float R = data[i * ch + 1];
            const float side = (L - R) * 0.5f;
            vocalData[i * ch]     = side;
            vocalData[i * ch + 1] = side;
        }

        FilterProcessor bpHigh, bpLow;
        bpHigh.setSampleRate(sr);
        bpHigh.setType(FilterType::HighPass);
        bpHigh.setFrequency(200.0f);
        bpHigh.setQ(0.707f);
        bpHigh.process(vocalData.data(), static_cast<int>(frames), ch);

        bpLow.setSampleRate(sr);
        bpLow.setType(FilterType::LowPass);
        bpLow.setFrequency(6000.0f);
        bpLow.setQ(0.707f);
        bpLow.process(vocalData.data(), static_cast<int>(frames), ch);

        vocalTrack->loadData(std::move(vocalData), sr, ch);
        result.vocals = vocalTrack;
    } else {
        result.vocals = makeFilteredTrack(FilterType::BandPass, 1000.0f, 0.5f);
    }

    if (progress) progress(0.9f, "Separating other");
    result.other = makeFilteredTrack(FilterType::HighPass, 5000.0f, 0.707f);

    result.success = true;
    if (progress) progress(1.0f, "Complete");

    spdlog::info("StemSeparator: spectral separation complete");
    return result;
}

}
