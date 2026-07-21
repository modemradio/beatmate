#include "OAuthPKCE.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <random>
namespace BeatMate::Services::Security {
std::string OAuthPKCE::startFlow(const std::string& clientId, const std::string& redirectUri, const std::vector<std::string>& scopes) {
    clientId_ = clientId; redirectUri_ = redirectUri;
    codeVerifier_ = generateVerifier();
    std::string challenge = generateChallenge(codeVerifier_);
    std::string scopeStr;
    for (size_t i = 0; i < scopes.size(); ++i) { if (i > 0) scopeStr += " "; scopeStr += scopes[i]; }
    spdlog::info("OAuthPKCE: Flow started for client {}", clientId);
    return ""; // Return auth URL
}
OAuthTokens OAuthPKCE::exchangeCode(const std::string& code) {
    OAuthTokens tokens;
    spdlog::info("OAuthPKCE: Exchanging code for tokens");
    return tokens;
}
OAuthTokens OAuthPKCE::refreshToken(const std::string& refreshTok) {
    OAuthTokens tokens;
    spdlog::info("OAuthPKCE: Refreshing token");
    return tokens;
}
std::string OAuthPKCE::generateVerifier() {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string v; v.reserve(128);
    std::random_device rd; std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(chars.size()) - 1);
    for (int i = 0; i < 128; ++i) v += chars[dist(gen)];
    return v;
}
std::string OAuthPKCE::generateChallenge(const std::string& verifier) {
    juce::SHA256 sha256(verifier.data(), verifier.size());
    auto hashResult = sha256.getRawData();
    juce::MemoryBlock mb(hashResult.getData(), hashResult.getSize());
    juce::String b64 = mb.toBase64Encoding();
    std::string result = b64.toStdString();
    for (auto& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!result.empty() && result.back() == '=') result.pop_back();
    return result;
}
} // namespace BeatMate::Services::Security
