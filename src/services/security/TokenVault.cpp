#include "TokenVault.h"
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <Windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif
namespace BeatMate::Services::Security {
void TokenVault::store(const std::string& service, const std::string& token) {
    tokens_[service] = protectData(token);
    spdlog::debug("TokenVault: Stored token for {}", service);
}
std::string TokenVault::retrieve(const std::string& service) const {
    auto it = tokens_.find(service);
    return (it != tokens_.end()) ? unprotectData(it->second) : "";
}
bool TokenVault::has(const std::string& service) const { return tokens_.count(service) > 0; }
void TokenVault::remove(const std::string& service) { tokens_.erase(service); }
std::string TokenVault::protectData(const std::string& data) const {
#ifdef _WIN32
    DATA_BLOB input, output;
    input.pbData = (BYTE*)data.data(); input.cbData = static_cast<DWORD>(data.size());
    if (CryptProtectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
        std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return result;
    }
#endif
    return data; // Fallback: no encryption
}
std::string TokenVault::unprotectData(const std::string& data) const {
#ifdef _WIN32
    DATA_BLOB input, output;
    input.pbData = (BYTE*)data.data(); input.cbData = static_cast<DWORD>(data.size());
    if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
        std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return result;
    }
#endif
    return data;
}
} // namespace BeatMate::Services::Security
