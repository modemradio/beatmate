#include "ClapEmbedQueue.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

#include "../library/TrackDatabase.h"

namespace BeatMate::Services::AI {

ClapEmbedQueue::ClapEmbedQueue(std::shared_ptr<Library::TrackDatabase> db)
    : db_(std::move(db)) {
    published_ = std::make_shared<const EmbeddingMap>();
    tagEncoder_ = std::make_unique<ClapEncoder>(ClapEncoder::defaultTagModelDirectory());
}

ClapEmbedQueue::~ClapEmbedQueue() {
    stop();
}

void ClapEmbedQueue::start() {
    if (started_.exchange(true)) return;
    if (!db_) return;
    if (!encoder_.isAvailable()) {
        spdlog::info("[ClapEmbed] modele absent ({}) — similarite IA desactivee",
                     encoder_.modelDirectory().getFullPathName().toStdString());
        return;
    }
    const int nWorkers = juce::jlimit(1, 2, juce::SystemStats::getNumCpus() / 4);
    for (int i = 0; i < nWorkers; ++i) {
        auto w = std::make_unique<Worker>(*this, "ClapEmbed" + juce::String(i + 1));
        w->startThread(juce::Thread::Priority::low);
        workers_.push_back(std::move(w));
    }
    spdlog::info("[ClapEmbed] demarre ({} workers)", nWorkers);
}

void ClapEmbedQueue::stop() {
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        w->signalThreadShouldExit();
        w->stopThread(15000);
    }
    workers_.clear();
}

std::shared_ptr<const ClapEmbedQueue::EmbeddingMap> ClapEmbedQueue::snapshot() const {
    std::lock_guard<std::mutex> lk(mapMutex_);
    return published_;
}

void ClapEmbedQueue::setOnPublish(std::function<void(int, int)> cb) {
    std::lock_guard<std::mutex> lk(cbMutex_);
    onPublish_ = std::move(cb);
}

bool ClapEmbedQueue::enqueueUnlocked(int64_t trackId, bool high) {
    if (trackId <= 0 || failed_.count(trackId) || !queued_.insert(trackId).second) return false;
    if (high) high_.push_back(trackId);
    else normal_.push_back(trackId);
    return true;
}

void ClapEmbedQueue::prioritizeTrack(int64_t trackId) {
    prioritizeTracks({ trackId });
}

void ClapEmbedQueue::prioritizeTracks(const std::vector<int64_t>& trackIds) {
    if (!started_.load() || workers_.empty()) return;
    {
        std::lock_guard<std::mutex> mapLk(mapMutex_);
        std::lock_guard<std::mutex> lk(queueMutex_);
        for (int64_t id : trackIds) {
            if (master_.count(id)) continue;
            if (queued_.count(id)) {
                auto it = std::find(normal_.begin(), normal_.end(), id);
                if (it != normal_.end()) {
                    normal_.erase(it);
                    high_.push_back(id);
                }
                continue;
            }
            enqueueUnlocked(id, true);
        }
    }
    cv_.notify_all();
}

void ClapEmbedQueue::rescanLibrary() {
    if (!started_.load() || workers_.empty() || !db_) return;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        if (stopping_ || !bootstrapDone_) return;
    }
    auto all = db_->getAllTracks();
    int added = 0;
    {
        std::lock_guard<std::mutex> mapLk(mapMutex_);
        std::lock_guard<std::mutex> lk(queueMutex_);
        for (const auto& t : all) {
            if (t.id <= 0 || t.filePath.empty() || master_.count(t.id)) continue;
            if (enqueueUnlocked(t.id, false)) ++added;
        }
        if (added > 0) total_ += added;
    }
    if (added > 0) {
        spdlog::info("[ClapEmbed] rescan: {} nouvelles pistes a analyser", added);
        cv_.notify_all();
    }
}

void ClapEmbedQueue::workerLoop(juce::Thread& thread) {
    {
        std::unique_lock<std::mutex> lk(queueMutex_);
        if (!bootstrapDone_) {
            bootstrapDone_ = true;
            lk.unlock();
            bootstrap();
        }
    }

    while (!thread.threadShouldExit()) {
        int64_t id = 0;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            cv_.wait_for(lk, std::chrono::milliseconds(500), [&] {
                return stopping_ || !high_.empty() || !normal_.empty();
            });
            if (stopping_ || thread.threadShouldExit()) return;
            if (!high_.empty()) { id = high_.front(); high_.pop_front(); }
            else if (!normal_.empty()) { id = normal_.front(); normal_.pop_front(); }
        }
        if (id == 0) {
            publish(false);
            continue;
        }

        const bool ok = processOne(id);
        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            queued_.erase(id);
            if (!ok) failed_.insert(id);
        }
        publish(false);
    }
}

void ClapEmbedQueue::bootstrap() {
    const auto rows = db_->loadAllTrackEmbeddings(ClapEncoder::kModelVersion);
    {
        std::lock_guard<std::mutex> lk(mapMutex_);
        for (const auto& e : rows)
            if ((int) e.vec.size() == ClapEncoder::kDim)
                master_.emplace(e.trackId, e.vec);
    }

    auto all = db_->getAllTracks();
    std::stable_sort(all.begin(), all.end(), [](const Models::Track& a, const Models::Track& b) {
        if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed;
        return a.dateAdded > b.dateAdded;
    });

    int added = 0;
    {
        std::lock_guard<std::mutex> mapLk(mapMutex_);
        std::lock_guard<std::mutex> lk(queueMutex_);
        for (const auto& t : all) {
            if (t.id <= 0 || t.filePath.empty() || master_.count(t.id)) continue;
            enqueueUnlocked(t.id, false);
            ++added;
        }
        total_ = (int) master_.size() + added;
    }
    publish(true);
    spdlog::info("[ClapEmbed] boot: {} vecteurs en base, {} pistes a analyser",
                 (int) (total_ - added), added);
    cv_.notify_all();
}

bool ClapEmbedQueue::processOne(int64_t trackId) {
    auto t = db_->getTrack(trackId);
    if (!t || t->filePath.empty()) return false;
    const juce::String pathStr(t->filePath);
    if (!juce::File::isAbsolutePath(pathStr)) return false;
    const juce::File f(pathStr);
    if (!f.existsAsFile()) return false;

    {
        std::lock_guard<std::mutex> lk(mapMutex_);
        if (master_.count(trackId)) return true;
    }

    auto vec = encoder_.encodeFile(f);
    if ((int) vec.size() != ClapEncoder::kDim) return false;

    db_->upsertTrackEmbedding(trackId, vec, ClapEncoder::kModelVersion,
                              f.getLastModificationTime().toMilliseconds());

    const bool wantGenre = t->genre.empty() && tagEncoder_ && tagEncoder_->zeroShotAvailable();
    const bool wantMood  = t->mood.empty()  && tagEncoder_ && tagEncoder_->moodZeroShotAvailable();
    if (wantGenre || wantMood) {
        const auto tagVec = tagEncoder_->encodeFile(f);
        if ((int) tagVec.size() == ClapEncoder::kDim) {
            if (wantGenre) {
                const auto tag = tagEncoder_->bestGenreLabel(tagVec);
                if (! tag.label.empty() && tag.score >= kGenreTagMinScore) {
                    if (db_->updateTrackGenreIfEmpty(trackId, tag.label))
                        spdlog::info("[ClapEmbed] genre auto '{}' ({:.2f}) pour '{}'",
                                     tag.label, tag.score, t->title.empty() ? t->filePath : t->title);
                }
            }
            if (wantMood) {
                const auto tag = tagEncoder_->bestMoodLabel(tagVec);
                if (! tag.label.empty() && tag.score >= kGenreTagMinScore) {
                    if (db_->updateTrackMoodIfEmpty(trackId, tag.label))
                        spdlog::info("[ClapEmbed] mood auto '{}' ({:.2f}) pour '{}'",
                                     tag.label, tag.score, t->title.empty() ? t->filePath : t->title);
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(mapMutex_);
        master_[trackId] = std::move(vec);
        ++pendingSincePublish_;
    }
    return true;
}

void ClapEmbedQueue::publish(bool force) {
    std::function<void(int, int)> cb;
    int done = 0, total = 0;
    {
        std::lock_guard<std::mutex> lk(mapMutex_);
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (!force && (pendingSincePublish_ == 0
                       || (pendingSincePublish_ < 16 && nowMs - lastPublishMs_ < 15000.0)))
            return;
        if (pendingSincePublish_ == 0 && !force) return;
        published_ = std::make_shared<const EmbeddingMap>(master_);
        pendingSincePublish_ = 0;
        lastPublishMs_ = nowMs;
        done = (int) master_.size();
    }
    {
        std::lock_guard<std::mutex> qlk(queueMutex_);
        total = total_;
    }
    {
        std::lock_guard<std::mutex> clk(cbMutex_);
        cb = onPublish_;
    }
    spdlog::info("[ClapEmbed] couverture {}/{}", done, total);
    if (cb) cb(done, total);
}

} // namespace BeatMate::Services::AI
