#pragma once
#include <string>
#include <vector>
namespace BeatMate::Core {
class SamplerEngine;
struct SampleBankEntry { int padIndex; std::string filePath; std::string name; float volume; float pitch; };
class SampleBank {
public:
    SampleBank() = default;
    bool save(const std::string& name, const SamplerEngine& engine, const std::string& bankDir = "SampleBanks");
    bool load(const std::string& name, SamplerEngine& engine, const std::string& bankDir = "SampleBanks");
    static std::vector<std::string> listBanks(const std::string& bankDir = "SampleBanks");
};
} // namespace BeatMate::Core
