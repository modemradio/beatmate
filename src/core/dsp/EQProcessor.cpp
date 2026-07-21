#include "EQProcessor.h"
#include "FilterProcessor.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::Core {

EQProcessor::EQProcessor()
    : lowFilter_(std::make_unique<FilterProcessor>()),
      midFilter_(std::make_unique<FilterProcessor>()),
      highFilter_(std::make_unique<FilterProcessor>()) {
    updateFilters();
}

EQProcessor::~EQProcessor() = default;

void EQProcessor::setLow(float gainDb) {
    lowGain_.store(std::clamp(gainDb, -12.0f, 12.0f));
    updateFilters();
}

void EQProcessor::setMid(float gainDb) {
    midGain_.store(std::clamp(gainDb, -12.0f, 12.0f));
    updateFilters();
}

void EQProcessor::setHigh(float gainDb) {
    highGain_.store(std::clamp(gainDb, -12.0f, 12.0f));
    updateFilters();
}

void EQProcessor::setLowFreq(float freq) {
    lowFreq_.store(std::clamp(freq, 20.0f, 500.0f));
    updateFilters();
}

void EQProcessor::setMidFreq(float freq) {
    midFreq_.store(std::clamp(freq, 200.0f, 5000.0f));
    updateFilters();
}

void EQProcessor::setHighFreq(float freq) {
    highFreq_.store(std::clamp(freq, 2000.0f, 20000.0f));
    updateFilters();
}

void EQProcessor::updateFilters() {
    lowFilter_->setSampleRate(sampleRate_);
    midFilter_->setSampleRate(sampleRate_);
    highFilter_->setSampleRate(sampleRate_);

    lowFilter_->setType(FilterType::LowShelf);
    lowFilter_->setFrequency(lowFreq_.load());
    lowFilter_->setGain(lowKill_.load() ? -60.0f : lowGain_.load());
    lowFilter_->setQ(0.707f);

    midFilter_->setType(FilterType::Peak);
    midFilter_->setFrequency(midFreq_.load());
    midFilter_->setGain(midKill_.load() ? -60.0f : midGain_.load());
    midFilter_->setQ(1.0f);

    highFilter_->setType(FilterType::HighShelf);
    highFilter_->setFrequency(highFreq_.load());
    highFilter_->setGain(highKill_.load() ? -60.0f : highGain_.load());
    highFilter_->setQ(0.707f);
}

void EQProcessor::process(float* buffer, int numSamples, int channels) {
    lowFilter_->process(buffer, numSamples, channels);
    midFilter_->process(buffer, numSamples, channels);
    highFilter_->process(buffer, numSamples, channels);
}

void EQProcessor::reset() {
    lowFilter_->reset();
    midFilter_->reset();
    highFilter_->reset();
}

void EQProcessor::onSampleRateChanged() {
    updateFilters();
}

}
