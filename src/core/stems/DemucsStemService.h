#pragma once

#include <array>
#include <functional>
#include <string>

namespace BeatMate::Core::Stems {

// Wrapper over the Demucs CLI (Python package `demucs`, invoked via
class DemucsStemService {
public:
    struct Result {
        bool ok = false;
        std::array<std::string, 4> stemPaths;    // drums, bass, other, vocals
        std::string message;
    };

    using ProgressCallback = std::function<void(float pct, const std::string& phase)>;

    // Synchronous. Use from a worker thread to avoid blocking the UI.
    Result separate(const std::string& inputWav,
                    const std::string& outputDir,
                    ProgressCallback progress = nullptr);

    // Detect the Demucs launcher: returns "demucs" (or "python -m demucs")
    static std::string findDemucsLauncher();
};

} // namespace BeatMate::Core::Stems
