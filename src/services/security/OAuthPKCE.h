#pragma once
#include <string>
#include <vector>
#include <functional>
namespace BeatMate::Services::Security {
struct OAuthTokens { std::string accessToken; std::string refreshToken; int expiresIn = 3600; std::string scope; };
using OAuthCallback = std::function<void(bool success, const OAuthTokens& tokens)>;
class OAuthPKCE {
public:
    OAuthPKCE() = default;
    std::string startFlow(const std::string& clientId, const std::string& redirectUri, const std::vector<std::string>& scopes);
    OAuthTokens exchangeCode(const std::string& code);
    OAuthTokens refreshToken(const std::string& refreshTok);
    std::string getCodeVerifier() const { return codeVerifier_; }
    static std::string generateVerifier();
    static std::string generateChallenge(const std::string& verifier);
private:
    std::string clientId_, redirectUri_, codeVerifier_, tokenEndpoint_;
};
} // namespace BeatMate::Services::Security
