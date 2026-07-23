#include "AnalysisRunner.h"
#include "../library/TrackDataProvider.h"
#include "../library/TrackCacheService.h"
#include "../../app/ServiceLocator.h"
#include "../../core/analysis/AudioAnalysisPipeline.h"
#include "../../core/analysis/EnergyAnalyzer.h"
#include "../../core/analysis/MoodDetector.h"
#include "../../core/analysis/StructureDetector.h"
#include "../../core/analysis/WaveformCacheService.h"
#include "../../core/stems/StemSepSotaService.h"
#include "../../models/TrackAnalysis.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::Services::Analysis {

AnalysisRunner::AnalysisRunner(Library::TrackDataProvider& provider)
    : provider_(provider) {}

AnalysisRunner::~AnalysisRunner()
{
    cancel();
    joinWorkers();
}

void AnalysisRunner::joinWorkers()
{
    for (auto& worker : workers_)
        if (worker.joinable())
            worker.join();
    workers_.clear();
}

bool AnalysisRunner::start(std::vector<Models::Track> tracks,
                           const AnalysisOptions& options,
                           AnalysisCallbacks callbacks)
{
    if (running_.load()) {
        spdlog::warn("[AnalysisRunner] start ignored: a run is already in progress");
        return false;
    }
    joinWorkers();

    if (tracks.empty()) {
        spdlog::warn("[AnalysisRunner] start ignored: no tracks");
        return false;
    }

    tracks_ = std::move(tracks);
    options_ = options;
    callbacks_ = std::move(callbacks);
    total_ = static_cast<int>(tracks_.size());
    cancelRequested_.store(false);
    nextIndex_.store(0);
    processed_.store(0);
    skipped_.store(0);
    running_.store(true);

    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    const int workerCount = std::min(juce::jlimit(1, 4, hw / 2), total_);
    activeWorkers_.store(workerCount);

    spdlog::info("[AnalysisRunner] Starting {} tracks on {} workers (BPM={} Key={} Energy={} Structure={} Waveform={} Mood={} UltraStems={})",
                 total_, workerCount, options_.bpm, options_.key, options_.energy,
                 options_.structure, options_.waveform, options_.mood, options_.ultraStems);

    provider_.beginBatch();
    for (int w = 0; w < workerCount; ++w)
        workers_.emplace_back([this] { workerLoop(); });
    return true;
}

void AnalysisRunner::cancel()
{
    if (running_.load()) {
        spdlog::info("[AnalysisRunner] Cancel requested");
        cancelRequested_.store(true);
    }
}

void AnalysisRunner::workerLoop()
{
    Core::AudioAnalysisPipeline pipeline;
    pipeline.setAnalyzeBPM(options_.bpm);
    pipeline.setAnalyzeKey(options_.key || options_.mood);
    pipeline.setAnalyzeEnergy(options_.energy || options_.mood);
    pipeline.setAnalyzeStructure(options_.structure);
    pipeline.setGenerateWaveform(options_.waveform);
    pipeline.setCancelFlag(&cancelRequested_);

    while (!cancelRequested_.load()) {
        const int index = nextIndex_.fetch_add(1);
        if (index >= total_)
            break;
        processTrack(tracks_[static_cast<size_t>(index)], pipeline);
    }

    if (activeWorkers_.fetch_sub(1) == 1) {
        provider_.endBatch();
        const bool cancelled = cancelRequested_.load();
        const int done = processed_.load();
        const int skip = skipped_.load();
        const int tot = total_;
        running_.store(false);
        spdlog::info("[AnalysisRunner] Finished: {}/{} tracks ({} skipped, cancelled={})",
                     done, tot, skip, cancelled);
        if (auto cb = callbacks_.onFinished)
            juce::MessageManager::callAsync([cb, done, tot, skip, cancelled] {
                cb(done, tot, skip, cancelled);
            });
    }
}

void AnalysisRunner::notifyProgress()
{
    if (auto cb = callbacks_.onProgress) {
        const int done = processed_.load();
        const int skip = skipped_.load();
        const int tot = total_;
        juce::MessageManager::callAsync([cb, done, tot, skip] { cb(done, tot, skip); });
    }
}

void AnalysisRunner::processTrack(const Models::Track& track, Core::AudioAnalysisPipeline& pipeline)
{
    auto reportFailure = [this, &track] {
        skipped_.fetch_add(1);
        processed_.fetch_add(1);
        if (auto cb = callbacks_.onTrackFinished) {
            TrackRowResult row;
            row.trackId = track.id;
            row.path = track.filePath;
            row.title = juce::String(track.title);
            juce::MessageManager::callAsync([cb, row] { cb(row); });
        }
        notifyProgress();
    };

    if (track.filePath.empty() || !juce::File(track.filePath).existsAsFile()) {
        spdlog::debug("[AnalysisRunner] Skipping '{}' - file not found: {}", track.title, track.filePath);
        reportFailure();
        return;
    }

    if (auto cb = callbacks_.onTrackStarted) {
        auto path = juce::String(track.filePath);
        juce::MessageManager::callAsync([cb, path] { cb(path); });
    }

    try {
        spdlog::info("[AnalysisRunner] Analyzing '{}' path='{}'", track.title, track.filePath);
        auto result = pipeline.analyzeTrack(track.filePath);

        if (cancelRequested_.load())
            return;

        if (!result.complete) {
            spdlog::warn("[AnalysisRunner] Analysis incomplete for '{}'", track.title);
            reportFailure();
            return;
        }

        persistResult(track, result);
        if (options_.ultraStems)
            runUltraStems(track);

        processed_.fetch_add(1);

        if (auto cb = callbacks_.onTrackFinished) {
            TrackRowResult row;
            row.trackId = track.id;
            row.path = track.filePath;
            row.title = juce::String(track.title);
            row.bpm = result.bpm.bpm;
            row.bpmConfidence = static_cast<float>(result.bpm.confidence);
            row.key = juce::String(result.key.key);
            row.camelotKey = juce::String(result.key.camelotKey);
            row.keyConfidence = static_cast<float>(result.key.confidence);
            row.energy = result.energy.overall;
            row.lufs = result.loudness.integratedLUFS;
            row.ok = true;
            juce::MessageManager::callAsync([cb, row] { cb(row); });
        }
        notifyProgress();
    }
    catch (const std::exception& e) {
        spdlog::error("[AnalysisRunner] Error analyzing '{}': {}", track.title, e.what());
        reportFailure();
    }
}

void AnalysisRunner::persistResult(const Models::Track& track, const Core::TrackAnalysis& result)
{
    Models::TrackAnalysis analysis;
    analysis.trackId = track.id;
    analysis.bpm = result.bpm.bpm;
    analysis.bpmConfidence = static_cast<float>(result.bpm.confidence);
    analysis.key = result.key.key;
    analysis.keyConfidence = static_cast<float>(result.key.confidence);
    analysis.energy = static_cast<float>(result.energy.overall) / 10.0f;
    analysis.loudness = result.loudness.integratedLUFS;
    analysis.peakLevel = result.loudness.truePeakdBTP;
    analysis.loudnessRange = result.loudness.loudnessRange;
    analysis.analyzedAt = juce::Time::currentTimeMillis() / 1000;
    for (const auto& sec : Core::EnergyAnalyzer::sectionize(result.energy))
        analysis.energySegments.emplace_back(sec.startSec, sec.endSec, sec.energy);
    for (const auto& sec : result.structure)
        analysis.sections.emplace_back(Core::StructureDetector::sectionTypeToString(sec.type),
                                       sec.startTime, sec.endTime, sec.label);
    if (options_.mood)
        analysis.mood = Core::MoodDetector::classify(result.key, result.energy).moodName;

    provider_.saveAnalysis(track.id, analysis);

    {
        Models::Track updatedTrack = provider_.getTrack(track.id);
        updatedTrack.bpm = result.bpm.bpm;
        updatedTrack.key = result.key.key;
        updatedTrack.energy = static_cast<float>(result.energy.overall);
        updatedTrack.analyzed = true;
        updatedTrack.analyzedDate = juce::Time::currentTimeMillis() / 1000;
        if (!result.key.camelotKey.empty())
            updatedTrack.camelotKey = result.key.camelotKey;
        provider_.updateTrack(updatedTrack);
    }

    {
        if (auto* cacheService = BeatMate::g_serviceLocator
                ? BeatMate::g_serviceLocator->tryGet<Services::Library::TrackCacheService>()
                : nullptr)
        {
            Services::Library::TrackAnalysisCache cacheData;
            cacheData.trackId = track.id;
            cacheData.filePath = track.filePath;
            cacheData.bpm = result.bpm.bpm;
            cacheData.bpmConfidence = result.bpm.confidence;
            cacheData.key = result.key.key;
            cacheData.keyConfidence = result.key.confidence;
            cacheData.energy = static_cast<float>(result.energy.overall);
            cacheData.peakLoudness = static_cast<float>(result.loudness.truePeakdBTP);
            cacheData.averageLoudness = static_cast<float>(result.loudness.integratedLUFS);
            cacheData.analyzedAt = juce::Time::currentTimeMillis() / 1000;
            cacheData.analysisVersion = "1.0";
            cacheData.isValid = true;
            cacheService->putCache(cacheData);
        }
    }

    if (!result.waveform.peaks.empty() || !result.multiBandWaveform.low.empty())
    {
        Core::WaveformCacheService wfCache;
        auto cacheDir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate").getChildFile("waveform_cache")
            .getFullPathName().toStdString();
        Core::ColouredWaveformData wfData;
        wfData.duration = result.duration;
        wfData.resolution = result.waveform.resolution;

        size_t numPts = result.waveform.peaks.size();
        size_t numBand = result.multiBandWaveform.low.size();
        size_t count = std::max(numPts, numBand);
        wfData.points.resize(count);
        for (size_t i = 0; i < count; ++i) {
            auto& cwp = wfData.points[i];
            cwp.amplitude = (i < numPts) ? result.waveform.peaks[i] : 0.0f;
            cwp.low  = (i < numBand) ? result.multiBandWaveform.low[i]  : 0.0f;
            cwp.mid  = (i < numBand) ? result.multiBandWaveform.mid[i]  : 0.0f;
            cwp.high = (i < numBand) ? result.multiBandWaveform.high[i] : 0.0f;
            cwp.color = { static_cast<uint8_t>(cwp.low * 255),
                          static_cast<uint8_t>(cwp.mid * 255),
                          static_cast<uint8_t>(cwp.high * 255), 255 };
        }
        wfCache.save(std::to_string(track.id), wfData, cacheDir);
    }
}

void AnalysisRunner::runUltraStems(const Models::Track& track)
{
    if (cancelRequested_.load())
        return;
    const juce::File src(track.filePath);
    const auto model = Core::Stems::StemSepSotaService::Model::FourStems;
    if (!src.existsAsFile()
        || Core::Stems::StemSepSotaService::cachedResultExists(src, model))
        return;

    std::lock_guard<std::mutex> lock(stemsMutex_);
    if (cancelRequested_.load())
        return;

    spdlog::info("[AnalysisRunner] Pre-separation Ultra (MDX-GPU) pour '{}'", track.title);
   #if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
   #elif defined(__APPLE__)
    {
        int policy = SCHED_OTHER;
        sched_param param{};
        pthread_getschedparam(pthread_self(), &policy, &param);
        param.sched_priority = sched_get_priority_min(SCHED_OTHER);
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
    }
   #endif
    Core::Stems::StemSepSotaService sota;
    auto stemRes = sota.separate(src, model);
   #if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
   #elif defined(__APPLE__)
    {
        int policy = SCHED_OTHER;
        sched_param param{};
        pthread_getschedparam(pthread_self(), &policy, &param);
        param.sched_priority = (sched_get_priority_min(SCHED_OTHER)
                                + sched_get_priority_max(SCHED_OTHER)) / 2;
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
    }
   #endif
    if (stemRes.ok)
        spdlog::info("[AnalysisRunner] Stems Ultra precalcules pour '{}'", track.title);
    else
        spdlog::warn("[AnalysisRunner] Pre-separation Ultra echouee pour '{}': {}",
                     track.title, stemRes.message.toStdString());
}

}
