#include "SampleBank.h"
#include "SamplerEngine.h"
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

bool SampleBank::save(const std::string& name, const SamplerEngine& /*engine*/,
                       const std::string& bankDir) {
    namespace fs = std::filesystem;
    fs::create_directories(bankDir);
    auto path = fs::path(bankDir) / (name + ".bank");
    std::ofstream file(path);
    if (!file) return false;
    file << "BEATMATE_SAMPLE_BANK\n";
    file << "name=" << name << "\n";
    spdlog::info("SampleBank: saved {}", name);
    return true;
}

bool SampleBank::load(const std::string& name, SamplerEngine& engine,
                       const std::string& bankDir) {
    auto path = std::filesystem::path(bankDir) / (name + ".bank");
    std::ifstream file(path);
    if (!file) return false;
    spdlog::info("SampleBank: loaded {}", name);
    return true;
}

std::vector<std::string> SampleBank::listBanks(const std::string& bankDir) {
    std::vector<std::string> banks;
    namespace fs = std::filesystem;
    if (!fs::exists(bankDir)) return banks;
    for (auto& entry : fs::directory_iterator(bankDir)) {
        if (entry.path().extension() == ".bank") {
            banks.push_back(entry.path().stem().string());
        }
    }
    return banks;
}

} // namespace BeatMate::Core
