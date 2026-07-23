#include "TokenVault.h"
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <Windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <CommonCrypto/CommonCrypto.h>
#include <vector>
#include <cstring>
#endif
namespace BeatMate::Services::Security {
#ifdef __APPLE__
namespace {
static const char kVaultMagic[4] = { 'B', 'M', 'V', '1' };

static bool readVaultKey(std::vector<uint8_t>& keyOut)
{
    CFStringRef service = CFSTR("BeatMate");
    CFStringRef account = CFSTR("TokenVaultKey");
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit };
    const void* vals[] = { kSecClassGenericPassword, service, account, kCFBooleanTrue, kSecMatchLimitOne };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 5,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFTypeRef result = nullptr;
    OSStatus st = SecItemCopyMatching(query, &result);
    CFRelease(query);
    bool ok = false;
    if (st == errSecSuccess && result && CFGetTypeID(result) == CFDataGetTypeID()) {
        CFDataRef d = static_cast<CFDataRef>(result);
        const uint8_t* p = CFDataGetBytePtr(d);
        keyOut.assign(p, p + CFDataGetLength(d));
        ok = keyOut.size() == 32;
    }
    if (result) CFRelease(result);
    return ok;
}

static bool getOrCreateVaultKey(std::vector<uint8_t>& keyOut)
{
    if (readVaultKey(keyOut)) return true;
    std::vector<uint8_t> k(32);
    if (SecRandomCopyBytes(kSecRandomDefault, k.size(), k.data()) != errSecSuccess)
        return false;
    CFStringRef service = CFSTR("BeatMate");
    CFStringRef account = CFSTR("TokenVaultKey");
    CFDataRef keyData = CFDataCreate(kCFAllocatorDefault, k.data(), static_cast<CFIndex>(k.size()));
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData };
    const void* vals[] = { kSecClassGenericPassword, service, account, keyData };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 4,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    OSStatus st = SecItemAdd(query, nullptr);
    CFRelease(query);
    CFRelease(keyData);
    if (st == errSecSuccess) { keyOut = k; return true; }
    if (st == errSecDuplicateItem) return readVaultKey(keyOut);
    return false;
}

static std::string aesEncrypt(const std::vector<uint8_t>& key, const std::string& plain)
{
    uint8_t iv[kCCBlockSizeAES128];
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(iv), iv) != errSecSuccess)
        return {};
    std::vector<uint8_t> out(plain.size() + kCCBlockSizeAES128);
    size_t moved = 0;
    CCCryptorStatus st = CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
                                 key.data(), key.size(), iv,
                                 plain.data(), plain.size(),
                                 out.data(), out.size(), &moved);
    if (st != kCCSuccess) return {};
    std::string result;
    result.reserve(sizeof(kVaultMagic) + sizeof(iv) + moved);
    result.append(kVaultMagic, sizeof(kVaultMagic));
    result.append(reinterpret_cast<const char*>(iv), sizeof(iv));
    result.append(reinterpret_cast<const char*>(out.data()), moved);
    return result;
}

static bool aesDecrypt(const std::vector<uint8_t>& key, const std::string& blob, std::string& outStr)
{
    const size_t headerLen = sizeof(kVaultMagic) + kCCBlockSizeAES128;
    if (blob.size() < headerLen) return false;
    if (std::memcmp(blob.data(), kVaultMagic, sizeof(kVaultMagic)) != 0) return false;
    const uint8_t* iv = reinterpret_cast<const uint8_t*>(blob.data()) + sizeof(kVaultMagic);
    const char* cipher = blob.data() + headerLen;
    size_t cipherLen = blob.size() - headerLen;
    std::vector<uint8_t> out(cipherLen + kCCBlockSizeAES128);
    size_t moved = 0;
    CCCryptorStatus st = CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
                                 key.data(), key.size(), iv,
                                 cipher, cipherLen,
                                 out.data(), out.size(), &moved);
    if (st != kCCSuccess) return false;
    outStr.assign(reinterpret_cast<const char*>(out.data()), moved);
    return true;
}
} // namespace
#endif
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
#elif defined(__APPLE__)
    std::vector<uint8_t> key;
    if (getOrCreateVaultKey(key)) {
        std::string enc = aesEncrypt(key, data);
        if (!enc.empty()) return enc;
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
#elif defined(__APPLE__)
    std::vector<uint8_t> key;
    std::string outStr;
    if (getOrCreateVaultKey(key) && aesDecrypt(key, data, outStr))
        return outStr;
#endif
    return data;
}
} // namespace BeatMate::Services::Security
