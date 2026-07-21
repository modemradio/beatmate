#include "TrackRelinkService.h"
#include "TrackDataProvider.h"
#include "TrackScanner.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

TrackRelinkService::TrackRelinkService(TrackDataProvider* provider)
    : provider_(provider) {}

std::vector<MissingTrack> TrackRelinkService::findMissingTracks(std::function<void(int, int)> progress)
{
    std::vector<MissingTrack> missing;
    if (!provider_) return missing;

    auto all = provider_->getAllTracks();
    const int total = (int) all.size();
    int done = 0;
    for (auto& t : all) {
        ++done;
        if (progress && (done % 100 == 0)) progress(done, total);
        if (t.filePath.empty()) continue;
        if (!juce::File(juce::String(t.filePath)).existsAsFile())
            missing.push_back({ std::move(t) });
    }
    spdlog::info("[Relink] {} missing track(s) out of {}", missing.size(), total);
    return missing;
}

std::vector<std::string> TrackRelinkService::defaultSearchRoots()
{
    std::vector<std::string> roots;
    if (!provider_) return roots;

    std::set<std::string> uniqueRoots;
    for (const auto& t : provider_->getAllTracks()) {
        if (t.filePath.empty()) continue;
        juce::File parent = juce::File(juce::String(t.filePath)).getParentDirectory();
        for (int up = 0; up < 2 && parent.getParentDirectory() != parent; ++up)
            parent = parent.getParentDirectory();
        if (parent.isDirectory())
            uniqueRoots.insert(parent.getFullPathName().toStdString());
    }
    uniqueRoots.insert(juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                           .getFullPathName().toStdString());
    roots.assign(uniqueRoots.begin(), uniqueRoots.end());
    return roots;
}

std::vector<RelinkCandidate> TrackRelinkService::findCandidates(
    const std::vector<MissingTrack>& missing,
    const std::vector<std::string>& searchRoots,
    std::function<void(int, int)> progress)
{
    std::vector<RelinkCandidate> out;
    if (missing.empty()) return out;

    std::unordered_multimap<std::string, std::string> index;
    int rootsDone = 0;
    for (const auto& root : searchRoots) {
        ++rootsDone;
        if (progress) progress(rootsDone, (int) searchRoots.size());
        TrackScanner scanner;
        for (auto& path : scanner.scanFolder(root, true)) {
            auto name = juce::File(juce::String(path)).getFileName().toLowerCase().toStdString();
            index.emplace(std::move(name), path);
        }
    }
    spdlog::info("[Relink] Indexed {} candidate file(s) across {} root(s)",
                 index.size(), searchRoots.size());

    for (const auto& m : missing) {
        const auto& t = m.track;
        const auto wantedName = juce::File(juce::String(t.filePath))
                                    .getFileName().toLowerCase().toStdString();
        if (wantedName.empty()) continue;

        RelinkCandidate best;
        auto range = index.equal_range(wantedName);
        for (auto it = range.first; it != range.second; ++it) {
            const auto& candidatePath = it->second;
            if (candidatePath == t.filePath) continue;
            juce::File cf{ juce::String(candidatePath) };
            float score = 0.6f;
            if (t.fileSize > 0 && cf.getSize() == t.fileSize) score += 0.3f;
            else if (t.fileSize > 0
                     && std::abs((double) cf.getSize() - (double) t.fileSize)
                            < (double) t.fileSize * 0.02) score += 0.15f;
            if (score > best.confidence) {
                best.trackId    = t.id;
                best.title      = t.title;
                best.artist     = t.artist;
                best.oldPath    = t.filePath;
                best.newPath    = candidatePath;
                best.confidence = score;
                best.accepted   = score >= 0.9f;
            }
        }
        if (best.trackId != 0 && best.confidence >= 0.6f)
            out.push_back(std::move(best));
    }
    spdlog::info("[Relink] {} candidate match(es) for {} missing track(s)",
                 out.size(), missing.size());
    return out;
}

int TrackRelinkService::applyRelinks(const std::vector<RelinkCandidate>& accepted)
{
    if (!provider_) return 0;
    int applied = 0;
    provider_->beginBatch();
    for (const auto& c : accepted) {
        if (!c.accepted || c.trackId <= 0 || c.newPath.empty()) continue;
        auto track = provider_->getTrack(c.trackId);
        if (track.id <= 0) continue;
        track.filePath = c.newPath;
        provider_->updateTrack(track);
        ++applied;
    }
    provider_->endBatch();
    spdlog::info("[Relink] {} track(s) relinked", applied);
    return applied;
}

} // namespace BeatMate::Services::Library
