#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../../models/Track.h"
namespace BeatMate::Services::Export {
enum class USBFormat { Rekordbox, EngineDJ, Generic };
using USBProgressCallback = std::function<void(int processed, int total)>;
class USBExporter {
public:
    USBExporter() = default;
    bool exportToUSB(const std::vector<Models::Track>& tracks, const std::string& drivePath, USBFormat format = USBFormat::Generic, USBProgressCallback callback = nullptr);
};
} // namespace BeatMate::Services::Export
