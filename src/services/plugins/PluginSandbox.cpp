#include "PluginSandbox.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <system_error>
namespace BeatMate::Services::Plugins {
namespace {
// Falls back to lexically_normal if weakly_canonical fails
std::string canonicalize(const std::string& raw, bool isDir) {
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(std::filesystem::path(raw), ec);
    if (ec) p = std::filesystem::path(raw).lexically_normal();
    auto s = p.make_preferred().string();
    if (isDir && !s.empty() && s.back() != std::filesystem::path::preferred_separator) {
        s.push_back(std::filesystem::path::preferred_separator);
    }
    return s;
}
} // namespace

bool PluginSandbox::isPathAllowed(const std::string& path) const {
    // Canonicalize so "../", symlinks and mixed separators cannot bypass the check
    const std::string canonical = canonicalize(path, /*isDir=*/false);
    for (const auto& allowed : allowedPaths_) {
        const std::string allowedCanon = canonicalize(allowed, /*isDir=*/true);
        if (allowedCanon.empty()) continue;
        // Trailing separator in allowedCanon prevents prefix bypass (/foo vs /foobar)
        if (canonical.size() >= allowedCanon.size() &&
            canonical.compare(0, allowedCanon.size(), allowedCanon) == 0) {
            return true;
        }
    }
    spdlog::warn("PluginSandbox: Path access denied: {}", path);
    return false;
}
} // namespace BeatMate::Services::Plugins
