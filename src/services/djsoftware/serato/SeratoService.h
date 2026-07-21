#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../../../models/SeratoTrack.h"

namespace BeatMate::Services::Serato {

class SeratoDatabase;

class SeratoService {
public:
    SeratoService();
    ~SeratoService();

    bool initialize();
    bool isAvailable() const;

    std::vector<Models::SeratoTrack> readDatabase();
    std::vector<std::string> readCrates();
    std::vector<Models::SeratoTrack> getTracksInCrate(const std::string& crateName);

private:
    std::unique_ptr<SeratoDatabase> database_;
    bool initialized_ = false;
};

} // namespace BeatMate::Services::Serato
