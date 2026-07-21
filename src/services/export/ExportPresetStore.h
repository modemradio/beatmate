#pragma once
#include <string>
#include <vector>
#include "BatchExportService.h"

namespace BeatMate::Services::Export {

struct ExportPreset {
    std::string name;
    BatchExportSettings settings;
};

class ExportPresetStore {
public:
    static std::vector<ExportPreset> loadAll();
    static void upsert(const ExportPreset& preset);
    static bool remove(const std::string& name);
};

} // namespace BeatMate::Services::Export
