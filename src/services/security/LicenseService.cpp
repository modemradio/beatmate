#include "LicenseService.h"
#include "LicenseValidator.h"
#include "HardwareId.h"
#include "EncryptionService.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

namespace BeatMate::Services::Security {

static std::string deobfuscate(const char* data, size_t len, char key)
{
    std::string result(len, '\0');
    for (size_t i = 0; i < len; ++i)
        result[i] = data[i] ^ key;
    return result;
}

static std::pair<std::vector<char>, size_t> obfuscateStr(const char* str, char key)
{
    size_t len = std::strlen(str);
    std::vector<char> buf(len);
    for (size_t i = 0; i < len; ++i)
        buf[i] = str[i] ^ key;
    return {buf, len};
}

static constexpr char kObfKey = 0x5A;

// Obfuscated at static init - not visible as plaintext in binary
static const auto kObfActivated = obfuscateStr("LicenseService: Activated", kObfKey);
static const auto kObfDeactivated = obfuscateStr("LicenseService: Deactivated", kObfKey);
static const auto kObfIntegrityFail = obfuscateStr("LicenseService: Integrity check failed", kObfKey);

static std::string getHwidKey()
{
    HardwareId hwid;
    return hwid.getHardwareId();
}

static std::vector<std::string> featuresForTier(LicenseTier tier)
{
    std::vector<std::string> f = {"streaming", "ai", "stems", "export", "plugins"};
    if (tier == LicenseTier::Professional || tier == LicenseTier::Premium)
    {
        f.push_back("studio");
        f.push_back("jingle");
    }
    if (tier == LicenseTier::Premium)
    {
        f.push_back("live");
        f.push_back("perfdj");
    }
    return f;
}

LicenseService::LicenseService()
{
    loadTrialState();
    loadLicenseFromDisk();
}

// [OVERLAY MAC] Dossier de données de BeatMate, par plateforme.
// Windows : %APPDATA%\BeatMate (comportement d'origine, inchangé).
// macOS   : ~/Library/Application Support/BeatMate (au lieu du dossier courant).
namespace {
std::filesystem::path beatmateDataDir()
{
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path base = appdata ? std::filesystem::path(appdata)
                                         : std::filesystem::path(".");
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    std::filesystem::path base = (home ? std::filesystem::path(home)
                                       : std::filesystem::path("."))
                                 / "Library" / "Application Support";
#else
    const char* home = std::getenv("HOME");
    std::filesystem::path base = (home ? std::filesystem::path(home)
                                       : std::filesystem::path("."))
                                 / ".config";
#endif
    auto dir = base / "BeatMate";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
} // namespace

std::string LicenseService::getTrialFilePath() const
{
    return (beatmateDataDir() / "trial.dat").string();
}

std::string LicenseService::getLicenseFilePath() const
{
    return (beatmateDataDir() / "license.key").string();
}

void LicenseService::loadTrialState()
{
    auto path = getTrialFilePath();
    std::ifstream file(path, std::ios::binary);
    if (file.is_open())
    {
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        file.close();

        if (data.size() >= 24) { // Minimum size for Blowfish CBC (16 IV + 8 block)
            EncryptionService encService;
            auto decrypted = encService.decrypt(data, getHwidKey());

            if (decrypted.size() >= sizeof(int64_t) + 32) {
                std::vector<uint8_t> tsBytes(decrypted.begin(), decrypted.begin() + sizeof(int64_t));
                juce::SHA256 hash(tsBytes.data(), tsBytes.size());
                auto hashStr = hash.toHexString().toStdString();

                std::string storedHash(decrypted.begin() + sizeof(int64_t),
                                        decrypted.begin() + sizeof(int64_t) + 32);

                if (hashStr.substr(0, 32) == storedHash) {
                    std::memcpy(&trialStartTimestamp_, tsBytes.data(), sizeof(int64_t));
                } else {
                    spdlog::warn("LicenseService: Trial file integrity check failed - resetting");
                    trialStartTimestamp_ = 0;
                }
            } else {
                spdlog::warn("LicenseService: Trial file decryption failed - resetting");
                trialStartTimestamp_ = 0;
            }
        } else if (data.size() == sizeof(int64_t)) {
            // Legacy unencrypted format - migrate
            std::memcpy(&trialStartTimestamp_, data.data(), sizeof(int64_t));
            saveTrialState();
            spdlog::info("LicenseService: Migrated trial file to Blowfish encrypted format");
        }
    }

    if (trialStartTimestamp_ == 0) {
        initTrial();
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (trialStartTimestamp_ > now + 86400) { // more than 1 day in future
        spdlog::warn("LicenseService: Clock tampering detected - trial start in future");
        trialStartTimestamp_ = now;
        saveTrialState();
    }

    spdlog::info("LicenseService: Trial started at {}, days remaining: {}",
                 trialStartTimestamp_, trialDaysRemaining());
}

void LicenseService::initTrial()
{
    trialStartTimestamp_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    saveTrialState();
    spdlog::info("LicenseService: Trial period started (7 days)");
}

void LicenseService::saveTrialState()
{
    auto path = getTrialFilePath();

    std::vector<uint8_t> data(sizeof(int64_t) + 32);
    std::memcpy(data.data(), &trialStartTimestamp_, sizeof(int64_t));

    juce::SHA256 hash(data.data(), sizeof(int64_t));
    auto hashStr = hash.toHexString().toStdString().substr(0, 32);
    std::memcpy(data.data() + sizeof(int64_t), hashStr.data(), 32);

    EncryptionService encService;
    auto encrypted = encService.encrypt(data, getHwidKey());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(encrypted.data()),
                   static_cast<std::streamsize>(encrypted.size()));
        file.close();
    }
}

void LicenseService::saveLicenseToDisk()
{
    auto path = getLicenseFilePath();

    // Format: KEY|HWID|EXPIRY|TYPE|SERVER_SIGNATURE|EMAIL|LOCAL_SIG
    HardwareId hwid;
    auto machineId = hwid.getHardwareId();

    std::string content = license_.key + "|" + machineId + "|" +
                          std::to_string(license_.expiresAt) + "|" + license_.type + "|" +
                          license_.signature + "|" + license_.email;

    // Local SHA-256 signature (binds payload + machine, distinct from server HMAC)
    juce::SHA256 sig(content.data(), content.size());
    content += "|" + sig.toHexString().toStdString().substr(0, 32);

    EncryptionService encService;
    std::vector<uint8_t> plaintext(content.begin(), content.end());
    auto encrypted = encService.encrypt(plaintext, getHwidKey());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(encrypted.data()),
                   static_cast<std::streamsize>(encrypted.size()));
    }
}

void LicenseService::loadLicenseFromDisk()
{
    auto path = getLicenseFilePath();
    if (!std::filesystem::exists(path)) return;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    file.close();
    if (data.empty()) return;

    EncryptionService encService;
    auto decrypted = encService.decrypt(data, getHwidKey());
    if (decrypted.empty()) {
        spdlog::warn("LicenseService: License file decryption failed - tampered or wrong machine");
        return;
    }
    std::string content(decrypted.begin(), decrypted.end());

    // Parse: KEY|HWID|EXPIRY|TYPE|SERVER_SIG|EMAIL|LOCAL_SIG
    auto parts = juce::StringArray::fromTokens(juce::String(content), "|", "");
    // Backward compat: old format had 5 parts (KEY|HWID|EXPIRY|TYPE|LOCAL_SIG).
    const bool isLegacy = (parts.size() == 5);
    if (parts.size() < 5) return;

    auto key        = parts[0].toStdString();
    auto storedHwid = parts[1].toStdString();
    auto expiry     = parts[2].getLargeIntValue();
    auto type       = parts[3].toStdString();
    std::string serverSig;
    std::string email;
    std::string storedSig;
    if ( isLegacy ) {
        storedSig = parts[4].toStdString();
        spdlog::warn("LicenseService: legacy license file (no server signature) — re-activation required.");
    } else {
        if (parts.size() < 7) return;
        serverSig = parts[4].toStdString();
        email     = parts[5].toStdString();
        storedSig = parts[6].toStdString();
    }

    std::string sigContent = isLegacy
        ? (key + "|" + storedHwid + "|" + parts[2].toStdString() + "|" + type)
        : (key + "|" + storedHwid + "|" + parts[2].toStdString() + "|" + type + "|" + serverSig + "|" + email);
    juce::SHA256 sig(sigContent.data(), sigContent.size());
    auto computedSig = sig.toHexString().toStdString().substr(0, 32);

    if (computedSig != storedSig) {
        spdlog::warn("LicenseService: License file signature mismatch - tampered");
        return;
    }

    if (serverSig.empty()) {
        spdlog::warn("LicenseService: no server signature on disk — license not honored.");
        return;
    }

    HardwareId hwid;
    auto currentHwid = hwid.getHardwareId();
    if (storedHwid != currentHwid) {
        spdlog::warn("LicenseService: License bound to different machine");
        return;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (expiry > 0 && expiry < now) {
        spdlog::warn("LicenseService: License expired");
        return;
    }

    LicenseValidator validator;
    if (!validator.validate(key)) {
        spdlog::warn("LicenseService: Stored key failed validation");
        return;
    }

    license_.key = key;
    license_.isValid = true;
    license_.type = type;
    license_.expiresAt = expiry;
    license_.signature = serverSig;
    license_.email = email;

    char tierChar = key.empty() ? 'P' : key[0];
    if      (type == "Premium"      || tierChar == 'E') license_.tier = LicenseTier::Premium;
    else if (type == "Professional" || tierChar == 'R') license_.tier = LicenseTier::Professional;
    else                                                license_.tier = LicenseTier::Personal;

    license_.features = featuresForTier(license_.tier);
    activated_ = true;
    spdlog::info("{}", deobfuscate(kObfActivated.first.data(), kObfActivated.second, kObfKey));
}

LicenseState LicenseService::getState() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);

    // Anti-crack: if activated_ is true but key is empty, it's been patched
    if (activated_ && license_.key.empty()) {
        spdlog::warn("{}", deobfuscate(kObfIntegrityFail.first.data(), kObfIntegrityFail.second, kObfKey));
        activated_ = false;
        license_.isValid = false;
        return LicenseState::TrialExpired;
    }

    // Anti-crack: activated_ must match license_.isValid
    if (activated_ != license_.isValid) {
        spdlog::warn("{}", deobfuscate(kObfIntegrityFail.first.data(), kObfIntegrityFail.second, kObfKey));
        activated_ = false;
        license_.isValid = false;
        return LicenseState::TrialExpired;
    }

    if (activated_ && license_.isValid)
        return LicenseState::Licensed;

    if (trialStartTimestamp_ == 0)
        return LicenseState::Unlicensed;

    // trialDaysRemaining() also locks — release first to avoid recursion.
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t elapsed = now - trialStartTimestamp_;
    int64_t trialDuration = static_cast<int64_t>(kTrialDays) * 24 * 3600;
    if (trialDuration - elapsed > 0)
        return LicenseState::Trial;

    return LicenseState::TrialExpired;
}

bool LicenseService::canUseApp() const
{
    auto state = getState();

    if (state == LicenseState::Licensed) {
        if (!verifyDiskConsistency()) {
            spdlog::warn("LicenseService: Disk consistency check failed in canUseApp");
            std::lock_guard<std::mutex> lock(stateMutex_);
            activated_ = false;
            license_.isValid = false;
            return false;
        }
    }

    return state == LicenseState::Licensed || state == LicenseState::Trial;
}

bool LicenseService::verifyDiskConsistency() const
{
    if (!activated_) return true;

    auto path = getLicenseFilePath();
    if (!std::filesystem::exists(path)) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    file.close();
    if (data.empty()) return false;

    EncryptionService encService;
    auto decrypted = encService.decrypt(data, getHwidKey());
    if (decrypted.empty()) return false;

    std::string content(decrypted.begin(), decrypted.end());
    auto parts = juce::StringArray::fromTokens(juce::String(content), "|", "");
    if (parts.size() < 4) return false;

    auto diskKey = parts[0].toStdString();
    auto diskType = parts[3].toStdString();

    return (diskKey == license_.key && diskType == license_.type);
}

bool LicenseService::verifyIntegrity()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    bool tampered = false;

    bool expectedActivated = (license_.isValid && !license_.key.empty());
    if (activated_ != expectedActivated) {
        spdlog::warn("{}", deobfuscate(kObfIntegrityFail.first.data(), kObfIntegrityFail.second, kObfKey)
                     + " - activated/valid mismatch");
        tampered = true;
    }

    if (activated_ && !tampered) {
        HardwareId hwid;
        auto currentHwid = hwid.getHardwareId();

        auto path = getLicenseFilePath();
        if (std::filesystem::exists(path)) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
                file.close();

                EncryptionService encService;
                auto decrypted = encService.decrypt(data, getHwidKey());
                if (!decrypted.empty()) {
                    std::string content(decrypted.begin(), decrypted.end());
                    auto parts = juce::StringArray::fromTokens(juce::String(content), "|", "");
                    if (parts.size() >= 2) {
                        auto storedHwid = parts[1].toStdString();
                        if (storedHwid != currentHwid) {
                            spdlog::warn("{}", deobfuscate(kObfIntegrityFail.first.data(), kObfIntegrityFail.second, kObfKey)
                                         + " - HWID mismatch");
                            tampered = true;
                        }
                    }
                } else {
                    tampered = true;
                }
            }
        } else {
            tampered = true;
        }
    }

    if (activated_ && !tampered && license_.expiresAt > 0) {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (license_.expiresAt < now) {
            spdlog::warn("{}", deobfuscate(kObfIntegrityFail.first.data(), kObfIntegrityFail.second, kObfKey)
                         + " - license expired");
            tampered = true;
        }
    }

    if (tampered) {
        activated_ = false;
        license_.isValid = false;
        license_.key.clear();
        license_.features.clear();

        if (integrityFailCallback_) {
            integrityFailCallback_();
        }

        return false;
    }

    return true;
}

int LicenseService::trialDaysRemaining() const
{
    if (activated_ && license_.isValid)
        return 999;

    if (trialStartTimestamp_ == 0)
        return 0;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t elapsed = now - trialStartTimestamp_;
    int64_t trialDuration = static_cast<int64_t>(kTrialDays) * 24 * 3600;
    int64_t remaining = trialDuration - elapsed;

    if (remaining <= 0) return 0;
    return static_cast<int>((remaining + 86399) / 86400);
}

double LicenseService::getMaxPlaybackSeconds() const
{
    if (getState() == LicenseState::Licensed)
        return 0.0;
    return kTrialMaxPlaybackSeconds;
}

bool LicenseService::isFeatureAvailable(const std::string& feature) const
{
    if (getState() == LicenseState::Trial) {
        // Reserve aux versions payantes : l'essai sert a evaluer l'analyse et
        // l'organisation de la bibliotheque, pas a produire des livrables.
        if (feature == "live" || feature == "perfdj"
            || feature == "studio" || feature == "jingle" || feature == "mix"
            || feature == "agenda" || feature == "stems" || feature == "export")
            return false;
        return true;
    }

    if (!activated_) return false;

    for (const auto& f : license_.features)
        if (f == feature) return true;

    // Les licences emises avant l'ajout de l'Agenda n'ont pas ce drapeau :
    // toute licence payante y donne droit.
    if (feature == "agenda") return true;

    return false;
}

bool LicenseService::activate(const std::string& /*key*/)
{
    // DEPRECATED: activation locale supprimee, passer par le flux WordPress.
    spdlog::error("LicenseService: legacy activate(key) is disabled — use the WordPress activation flow.");
    return false;
}

void LicenseService::activateFromServer(const std::string& key,
                                        const std::string& type,
                                        int64_t expiresAtEpoch,
                                        const std::string& signature,
                                        const std::string& email)
{
    LicenseValidator validator;
    if (!validator.validate(key)) {
        spdlog::error("LicenseService: server returned an invalid-format key — refusing activation.");
        return;
    }
    if (signature.size() != 64) {
        spdlog::error("LicenseService: server signature missing or wrong length — refusing activation.");
        return;
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    license_.key       = key;
    license_.type      = type;
    license_.signature = signature;
    license_.email     = email;
    license_.expiresAt = expiresAtEpoch;
    license_.isValid   = true;
    license_.maxMachines = 1; // 1 license = 1 active machine, swap via deactivate/activate.

    char tierChar    = key.empty() ? 'P' : key[0];
    char billingChar = key.size() > 1 ? key[1] : 'A';
    if      (tierChar == 'E' || type == "Premium")      license_.tier = LicenseTier::Premium;
    else if (tierChar == 'R' || type == "Professional") license_.tier = LicenseTier::Professional;
    else                                                license_.tier = LicenseTier::Personal;

    if (billingChar == 'M' || billingChar == 'm') license_.billing = LicenseBilling::Monthly;
    else                                          license_.billing = LicenseBilling::Annual;

    license_.features = featuresForTier(license_.tier);

    activated_ = true;
    saveLicenseToDisk();

    HardwareId hwid;
    spdlog::info("{} (key={}****, machine={}, type={}, sig={}****)",
                 deobfuscate(kObfActivated.first.data(), kObfActivated.second, kObfKey),
                 key.substr(0, 5), hwid.getHardwareId().substr(0, 8), type, signature.substr(0, 8));
}

void LicenseService::forceLocalRevocation(const std::string& reason)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    spdlog::warn("LicenseService: forced revocation — {}", reason);
    activated_ = false;
    license_.isValid = false;
    license_.key.clear();
    license_.signature.clear();
    license_.email.clear();
    license_.features.clear();
    license_.expiresAt = 0;
    // Wipe disk file so a relaunch can't resurrect a dead license.
    auto path = getLicenseFilePath();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool LicenseService::deactivate()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    activated_ = false;
    license_ = LicenseInfo();

    auto path = getLicenseFilePath();
    std::error_code ec;
    std::filesystem::remove(path, ec);

    spdlog::info("{}", deobfuscate(kObfDeactivated.first.data(), kObfDeactivated.second, kObfKey));
    return true;
}

LicenseInfo LicenseService::checkLicense() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return license_;
}

} // namespace BeatMate::Services::Security
