#pragma once

#include <string>
#include <vector>

#include "../../../models/Track.h"

namespace BeatMate::Services::DjayPro {

class DjayProService {
public:
    DjayProService() = default;
    ~DjayProService() = default;

    bool initialize();
    bool isAvailable() const;
    std::vector<Models::Track> readCollection();

private:
    std::string findDatabasePath() const;
    bool initialized_ = false;
};

} // namespace BeatMate::Services::DjayPro
