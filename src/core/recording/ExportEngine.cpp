#include "ExportEngine.h"
#include "../audio/AudioTrack.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

bool ExportEngine::exportTrack(const AudioTrack& track, const std::string& outputPath,
                                const ExportOptions& options, ExportProgressCallback progress) {
    spdlog::info("ExportEngine: exporting to {} (format={})", outputPath, options.format);

    WriteOptions writeOpts;
    writeOpts.bitRate = options.bitRate;
    writeOpts.sampleRate = options.sampleRate;
    writeOpts.bitDepth = options.bitDepth;

    AudioFileWriter writer;
    bool result = writer.writeFile(track, outputPath, writeOpts,
        [&progress](float p) { if (progress) progress(p); });

    if (result) {
        spdlog::info("ExportEngine: export complete");
    } else {
        spdlog::error("ExportEngine: export failed");
    }

    return result;
}

} // namespace BeatMate::Core
