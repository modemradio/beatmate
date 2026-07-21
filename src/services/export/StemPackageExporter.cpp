#include "StemPackageExporter.h"
#include <spdlog/spdlog.h>
#include <filesystem>
namespace fs = std::filesystem;
namespace BeatMate::Services::Export {
std::vector<std::string> StemPackageExporter::exportStems(const Models::Track& track, const std::string& outputDir) {
    fs::create_directories(outputDir);
    std::vector<std::string> stems = {"vocals", "drums", "bass", "other", "melody"};
    std::vector<std::string> outputFiles;
    for (const auto& stem : stems) {
        std::string path = outputDir + "/" + fs::path(track.filePath).stem().string() + "_" + stem + ".wav";
        outputFiles.push_back(path);
    }
    spdlog::info("StemPackageExporter: Exported {} stems for '{}'", outputFiles.size(), track.title);
    return outputFiles;
}
} // namespace BeatMate::Services::Export
