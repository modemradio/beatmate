#include "SongStructureAnalyzerService.h"
#include "../audio/AudioTrack.h"
#include "BPMDetector.h"
#include "StructureDetector.h"
#include "EnergyAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

SongStructureAnalyzerService::SongStructureAnalyzerService() = default;
SongStructureAnalyzerService::~SongStructureAnalyzerService() = default;

std::vector<float> SongStructureAnalyzerService::computeEnergyProfile(
    const AudioTrack& track, double barDuration) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t barSamples = static_cast<size_t>(barDuration * sr);
    if (barSamples == 0) return {};

    size_t numBars = totalSamples / barSamples;
    std::vector<float> profile;
    profile.reserve(numBars);

    for (size_t bar = 0; bar < numBars; ++bar) {
        const float* barData = data + bar * barSamples;
        size_t len = std::min(barSamples, totalSamples - bar * barSamples);

        double sum = 0.0;
        for (size_t i = 0; i < len; ++i) sum += barData[i] * barData[i];
        profile.push_back(static_cast<float>(std::sqrt(sum / len)));
    }

    float maxVal = 0.0f;
    for (auto& v : profile) maxVal = std::max(maxVal, v);
    if (maxVal > 0) {
        for (auto& v : profile) v /= maxVal;
    }

    return profile;
}

std::string SongStructureAnalyzerService::identifyForm(const std::vector<SongSection>& sections) {
    if (sections.empty()) return "Unknown";

    std::string form;
    char letterMap[8] = { 'I', 'V', 'C', 'B', 'D', 'K', 'U', 'O' };

    for (auto& sec : sections) {
        int idx = static_cast<int>(sec.type);
        if (idx >= 0 && idx < 8) form += letterMap[idx];
        else form += '?';
    }

    bool hasVerse = false, hasChorus = false, hasDrop = false;
    for (auto& sec : sections) {
        if (sec.type == SectionType::Verse) hasVerse = true;
        if (sec.type == SectionType::Chorus) hasChorus = true;
        if (sec.type == SectionType::Drop) hasDrop = true;
    }

    if (hasDrop) return "EDM/Drop";
    if (hasVerse && hasChorus) return "Verse-Chorus";
    if (hasVerse) return "Verse-based";

    return form;
}

std::vector<SongSection> SongStructureAnalyzerService::mergeSections(
    const std::vector<SongSection>& sections, int minBars) {

    if (sections.size() < 2) return sections;

    std::vector<SongSection> merged;
    SongSection current = sections[0];

    for (size_t i = 1; i < sections.size(); ++i) {
        if (current.barCount < minBars && current.type == sections[i].type) {
            current.endTime = sections[i].endTime;
            current.endBar = sections[i].endBar;
            current.barCount = current.endBar - current.startBar;
            current.energy = (current.energy + sections[i].energy) / 2.0f;
        } else {
            if (current.barCount >= 1) merged.push_back(current);
            current = sections[i];
        }
    }
    merged.push_back(current);

    return merged;
}

SongStructure SongStructureAnalyzerService::analyze(const AudioTrack& track, double bpm) {
    spdlog::info("SongStructureAnalyzerService: analyzing {}", track.getFilePath());

    SongStructure result;
    result.totalDuration = track.getDuration();

    if (bpm <= 0) {
        BPMDetector detector;
        auto bpmResult = detector.detect(track);
        bpm = bpmResult.bpm;
    }
    result.bpm = bpm;

    double beatDuration = 60.0 / bpm;
    double barDuration = beatDuration * 4.0; // Assuming 4/4 time
    result.totalBars = static_cast<int>(result.totalDuration / barDuration);

    auto energyProfile = computeEnergyProfile(track, barDuration);

    StructureDetector detector;
    auto rawSections = detector.detect(track);

    for (auto& sec : rawSections) {
        SongSection songSec;
        songSec.type = sec.type;
        songSec.startTime = sec.startTime;
        songSec.endTime = sec.endTime;
        songSec.label = sec.label;
        songSec.confidence = sec.confidence;
        songSec.startBar = static_cast<int>(sec.startTime / barDuration);
        songSec.endBar = static_cast<int>(sec.endTime / barDuration);
        songSec.barCount = songSec.endBar - songSec.startBar;

        float energySum = 0.0f;
        int count = 0;
        for (int bar = songSec.startBar; bar < songSec.endBar && bar < static_cast<int>(energyProfile.size()); ++bar) {
            energySum += energyProfile[bar];
            count++;
        }
        songSec.energy = (count > 0) ? energySum / count : 0.0f;

        result.sections.push_back(songSec);
    }

    result.sections = mergeSections(result.sections, minSectionBars_);

    for (auto& sec : result.sections) {
        double relPos = sec.startTime / result.totalDuration;

        if (relPos < 0.05 && sec.barCount <= 8) {
            sec.type = SectionType::Intro;
            sec.label = "Intro";
        } else if (relPos > 0.9 && sec.barCount <= 8) {
            sec.type = SectionType::Outro;
            sec.label = "Outro";
        } else if (sec.energy > 0.8f) {
            sec.type = SectionType::Drop;
            sec.label = "Drop";
        } else if (sec.energy < 0.3f && sec.barCount >= 4) {
            sec.type = SectionType::Breakdown;
            sec.label = "Breakdown";
        }
    }

    result.formLabel = identifyForm(result.sections);

    for (auto& sec : result.sections) {
        if (sec.type == SectionType::Intro) result.hasIntro = true;
        if (sec.type == SectionType::Outro) result.hasOutro = true;
        if (sec.type == SectionType::Drop) result.hasDrop = true;
    }

    float confSum = 0.0f;
    for (auto& sec : result.sections) confSum += sec.confidence;
    result.structureConfidence = result.sections.empty() ? 0.0f : confSum / result.sections.size();

    spdlog::info("SongStructureAnalyzerService: {} sections, {} bars, form: {}",
                 result.sections.size(), result.totalBars, result.formLabel);
    return result;
}

} // namespace BeatMate::Core
