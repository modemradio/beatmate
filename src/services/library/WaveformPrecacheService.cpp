#include "WaveformPrecacheService.h"
#include "TrackDataProvider.h"
#include <algorithm>
#include <vector>
#include <juce_events/juce_events.h>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

namespace {
juce::File inflightFile()   { return Core::RgbPeaksGenerator::cacheDirectory().getChildFile("precache_inflight.txt"); }
juce::File quarantineFile() { return Core::RgbPeaksGenerator::cacheDirectory().getChildFile("precache_quarantine.txt"); }
}

WaveformPrecacheService::WaveformPrecacheService(TrackDataProvider* provider)
    : juce::Thread("WaveformPrecache"), m_provider(provider)
{
    if (m_provider)
    {
        auto alive = m_alive;
        m_provider->onDataChanged([this, alive] {
            if (!alive->load()) return;
            m_rescanAtMs.store(juce::Time::getMillisecondCounter() + 5000);
            m_rescanPending.store(true);
            notify();
        });
    }
}

WaveformPrecacheService::~WaveformPrecacheService()
{
    m_alive->store(false);
    stopThread(4000);
}

void WaveformPrecacheService::requestPriority(const std::string& audioPath,
                                              ProgressCallback onProgress,
                                              DoneCallback onDone)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find_if(m_queue.begin(), m_queue.end(),
                               [&audioPath](const Job& j) { return j.path == audioPath; });
        if (it != m_queue.end())
            m_queue.erase(it);
        const bool sameJobRunning = (m_inflightPath == audioPath && m_inflightPriority);
        Job job;
        job.path = audioPath;
        job.priority = true;
        job.seq = sameJobRunning ? m_prioritySeq.load() : m_prioritySeq.fetch_add(1) + 1;
        job.onProgress = std::move(onProgress);
        job.onDone = std::move(onDone);
        m_queue.push_front(std::move(job));
    }
    notify();
}

int WaveformPrecacheService::pendingCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_queue.size()) + (m_inflightPath.empty() ? 0 : 1);
}

void WaveformPrecacheService::requestScan()
{
    m_rescanAtMs.store(juce::Time::getMillisecondCounter());
    m_rescanPending.store(true);
    notify();
}

void WaveformPrecacheService::run()
{
    loadQuarantine();
    m_rescanAtMs.store(juce::Time::getMillisecondCounter() + 10000);
    m_rescanPending.store(true);

    while (!threadShouldExit())
    {
        if (m_rescanPending.load()
            && juce::Time::getMillisecondCounter() >= m_rescanAtMs.load()
            && !hasPriorityJob())
        {
            m_rescanPending.store(false);
            enqueueScan();
        }

        Job job;
        if (!popJob(job))
        {
            wait(2000);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_inflightPath = job.path;
            m_inflightPriority = job.priority;
        }
        const JobOutcome outcome = processJob(job);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_inflightPath.clear();
            m_inflightPriority = false;
        }

        if (outcome == JobOutcome::Preempted)
        {
            requeueAfterPriorities(std::move(job));
            continue;
        }

        if (outcome == JobOutcome::Generated && !job.priority)
            wait(200);
    }
}

bool WaveformPrecacheService::popJob(Job& out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.empty())
        return false;
    out = std::move(m_queue.front());
    m_queue.pop_front();
    return true;
}

WaveformPrecacheService::JobOutcome WaveformPrecacheService::processJob(const Job& job)
{
    auto alive = m_alive;
    auto deliver = [alive](const DoneCallback& cb, Core::RgbPeaksData data) {
        if (!cb) return;
        juce::MessageManager::callAsync([alive, cb, data = std::move(data)]() {
            if (!alive->load()) return;
            cb(data);
        });
    };

    if (!job.priority)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_quarantine.count(job.path) > 0)
            return JobOutcome::Skipped;
    }

    if (Core::RgbPeaksGenerator::isCacheValid(job.path))
    {
        if (job.onDone)
        {
            Core::RgbPeaksData data;
            if (Core::RgbPeaksGenerator::read(Core::RgbPeaksGenerator::cacheFileFor(job.path), data))
                deliver(job.onDone, std::move(data));
        }
        return JobOutcome::Skipped;
    }

    if (!juce::File(juce::String(job.path)).existsAsFile())
        return JobOutcome::Skipped;

    inflightFile().replaceWithText(juce::String(job.path));

    Core::RgbPeaksGenerator generator;
    auto progressCb = job.onProgress;
    auto data = generator.generate(
        job.path,
        [this, &job]() { return threadShouldExit() || m_prioritySeq.load() > job.seq; },
        [&deliver, &progressCb](const Core::RgbPeaksData& partial) {
            deliver(progressCb, partial);
        });

    inflightFile().deleteFile();

    if (threadShouldExit())
        return JobOutcome::Skipped;

    if (!data.valid() && m_prioritySeq.load() > job.seq)
        return JobOutcome::Preempted;

    if (data.valid())
    {
        spdlog::info("[WaveformPrecache] Generated peaks for {}", job.path);
        enforceDiskCap();
        deliver(job.onDone, std::move(data));
        return JobOutcome::Generated;
    }

    spdlog::warn("[WaveformPrecache] Failed to generate peaks for {}", job.path);
    addToQuarantine(job.path);
    deliver(job.onDone, Core::RgbPeaksData{});
    return JobOutcome::Generated;
}

bool WaveformPrecacheService::hasPriorityJob()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue.empty() && m_queue.front().priority;
}

void WaveformPrecacheService::requeueAfterPriorities(Job&& job)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool queued = std::any_of(m_queue.begin(), m_queue.end(),
                              [&job](const Job& j) { return j.path == job.path; });
    if (queued)
        return;
    auto it = m_queue.begin();
    while (it != m_queue.end() && it->priority)
        ++it;
    job.seq = m_prioritySeq.load();
    m_queue.insert(it, std::move(job));
}

void WaveformPrecacheService::enqueueScan()
{
    if (!m_provider)
        return;

    auto tracks = m_provider->getAllTracks();
    std::set<std::string> skip;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        skip = m_quarantine;
        for (const auto& j : m_queue)
            skip.insert(j.path);
    }
    const uint64_t seqAtStart = m_prioritySeq.load();
    std::vector<std::string> toAdd;
    bool interrupted = false;
    for (const auto& t : tracks)
    {
        if (threadShouldExit())
            return;
        if (m_prioritySeq.load() != seqAtStart)
        {
            interrupted = true;
            break;
        }
        if (t.filePath.empty())
            continue;
        if (skip.count(t.filePath) > 0)
            continue;
        if (Core::RgbPeaksGenerator::isCacheValid(t.filePath))
            continue;
        if (!juce::File{ juce::String(t.filePath) }.existsAsFile())
            continue;
        toAdd.push_back(t.filePath);
    }
    int added = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::set<std::string> queued;
        for (const auto& j : m_queue)
            queued.insert(j.path);
        for (auto& p : toAdd)
        {
            if (queued.count(p) > 0)
                continue;
            Job job;
            job.path = p;
            job.seq = m_prioritySeq.load();
            m_queue.push_back(std::move(job));
            ++added;
        }
    }
    if (interrupted)
    {
        m_rescanAtMs.store(juce::Time::getMillisecondCounter());
        m_rescanPending.store(true);
    }
    if (added > 0)
        spdlog::info("[WaveformPrecache] Scan queued {} tracks for peak generation", added);
}

void WaveformPrecacheService::loadQuarantine()
{
    juce::File qf = quarantineFile();
    if (qf.existsAsFile())
    {
        juce::StringArray lines;
        qf.readLines(lines);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& line : lines)
            if (line.trim().isNotEmpty())
                m_quarantine.insert(line.trim().toStdString());
    }

    juce::File inflight = inflightFile();
    if (inflight.existsAsFile())
    {
        juce::String path = inflight.loadFileAsString().trim();
        if (path.isNotEmpty())
        {
            spdlog::warn("[WaveformPrecache] Previous session crashed while decoding {} — quarantined", path.toStdString());
            addToQuarantine(path.toStdString());
        }
        inflight.deleteFile();
    }
}

void WaveformPrecacheService::addToQuarantine(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_quarantine.count(path) > 0)
            return;
        m_quarantine.insert(path);
    }
    quarantineFile().appendText(juce::String(path) + "\n");
}

void WaveformPrecacheService::enforceDiskCap()
{
    constexpr int64_t capBytes = 4LL * 1024 * 1024 * 1024;
    constexpr int64_t targetBytes = 3584LL * 1024 * 1024;

    auto files = Core::RgbPeaksGenerator::cacheDirectory()
                     .findChildFiles(juce::File::findFiles, false, "*.rgbpeaks3");
    int64_t total = 0;
    for (const auto& f : files)
        total += f.getSize();
    if (total <= capBytes)
        return;

    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        auto ta = a.getLastAccessTime();
        auto tb = b.getLastAccessTime();
        if (ta == juce::Time() || tb == juce::Time())
            return a.getLastModificationTime() < b.getLastModificationTime();
        return ta < tb;
    });

    int purged = 0;
    for (const auto& f : files)
    {
        if (total <= targetBytes)
            break;
        int64_t sz = f.getSize();
        if (f.deleteFile())
        {
            total -= sz;
            ++purged;
        }
    }
    spdlog::info("[WaveformPrecache] Disk cap: purged {} old peak caches, {} MB remaining", purged, total / (1024 * 1024));
}

} // namespace BeatMate::Services::Library
