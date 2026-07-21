#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;
class TrackDataProvider;

struct MissingTrack {
    Models::Track track;
};

struct RelinkCandidate {
    int64_t trackId = 0;
    std::string title;
    std::string artist;
    std::string oldPath;
    std::string newPath;
    float confidence = 0.0f;
    bool accepted = false;
};

class TrackRelinkService {
public:
    explicit TrackRelinkService(TrackDataProvider* provider);

    std::vector<MissingTrack> findMissingTracks(std::function<void(int, int)> progress = {});

    std::vector<RelinkCandidate> findCandidates(const std::vector<MissingTrack>& missing,
                                                const std::vector<std::string>& searchRoots,
                                                std::function<void(int, int)> progress = {});

    int applyRelinks(const std::vector<RelinkCandidate>& accepted);

    // Roots worth scanning by default: parents of every known library path.
    std::vector<std::string> defaultSearchRoots();

private:
    TrackDataProvider* provider_ = nullptr;
};

} // namespace BeatMate::Services::Library
