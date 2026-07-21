#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"
namespace BeatMate::Services::Export {
class StemPackageExporter {
public:
    StemPackageExporter() = default;
    std::vector<std::string> exportStems(const Models::Track& track, const std::string& outputDir);
};
} // namespace BeatMate::Services::Export
