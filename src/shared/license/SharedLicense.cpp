#include "SharedLicense.h"

#include "services/network/HttpClient.h"
#include "services/wordpress/WordPressLicenseClient.h"
#include "services/wordpress/LicenseHeartbeatService.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#ifndef BEATMATE_WP_BASE_URL
 #define BEATMATE_WP_BASE_URL "https://beatmate.fr"
#endif
#ifndef BEATMATE_WP_API_KEY
 #define BEATMATE_WP_API_KEY ""
#endif

namespace BeatMate::Shared {

using Services::Security::LicenseService;
using Services::Security::LicenseState;
using Services::Network::HttpClient;
using Services::WordPress::WordPressLicenseClient;
using Services::WordPress::LicenseHeartbeatService;

SharedLicense& SharedLicense::instance()
{
    static SharedLicense inst;
    return inst;
}

SharedLicense::SharedLicense()
{
    license_ = std::make_unique<LicenseService>();
    creds_   = loadCredentials();
    http_    = std::make_shared<HttpClient>();
    rebuildClient();
}

SharedLicense::~SharedLicense()
{
    shutdown();
}

std::string SharedLicense::configFilePath()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BeatMate");
    dir.createDirectory();
    return dir.getChildFile("wp.json").getFullPathName().toStdString();
}

WpCredentials SharedLicense::loadCredentials() const
{
    WpCredentials c;
    c.baseUrl = BEATMATE_WP_BASE_URL;
    c.apiKey  = BEATMATE_WP_API_KEY;

    const juce::File f(configFilePath());
    if (f.existsAsFile())
    {
        juce::var parsed;
        if (juce::JSON::parse(f.loadFileAsString(), parsed).wasOk() && parsed.isObject())
        {
            auto url = parsed.getProperty("base_url", juce::var()).toString().trim();
            auto key = parsed.getProperty("api_key",  juce::var()).toString().trim();
            if (url.isNotEmpty()) c.baseUrl = url.toStdString();
            if (key.isNotEmpty()) c.apiKey  = key.toStdString();
        }
    }
    while (! c.baseUrl.empty() && c.baseUrl.back() == '/') c.baseUrl.pop_back();
    return c;
}

void SharedLicense::saveCredentials(const WpCredentials& creds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    creds_ = creds;
    while (! creds_.baseUrl.empty() && creds_.baseUrl.back() == '/') creds_.baseUrl.pop_back();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("base_url", juce::String(creds_.baseUrl));
    obj->setProperty("api_key",  juce::String(creds_.apiKey));
    juce::var v(obj);
    juce::File(configFilePath()).replaceWithText(juce::JSON::toString(v, true));

    rebuildClient();

    // Si le heartbeat tourne, le recreer avec le nouveau client (nouveau serveur).
    if (heartbeat_ != nullptr)
    {
        heartbeat_->stop();
        heartbeat_.reset();
        startHeartbeatLocked();
    }
}

WpCredentials SharedLicense::credentials() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return creds_;
}

void SharedLicense::rebuildClient()
{
    wpClient_ = std::make_shared<WordPressLicenseClient>(http_, creds_.baseUrl, creds_.apiKey);
    wpClient_->setAppVersion(
#ifdef BEATMATE_SUITE_VERSION
        BEATMATE_SUITE_VERSION
#else
        "12.0.0"
#endif
    );
}

LicenseState SharedLicense::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->getState();
}

bool SharedLicense::canRunApp() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->canUseApp();
}

bool SharedLicense::isFullLicense() const   { return state() == LicenseState::Licensed; }
bool SharedLicense::isTrial() const         { return state() == LicenseState::Trial; }
bool SharedLicense::isTrialExpired() const  { return state() == LicenseState::TrialExpired; }

int SharedLicense::trialDaysRemaining() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->trialDaysRemaining();
}

double SharedLicense::maxPlaybackSeconds() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->getMaxPlaybackSeconds();
}

bool SharedLicense::isFeatureAvailable(const std::string& feature) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->isFeatureAvailable(feature);
}

std::string SharedLicense::licenseType() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->checkLicense().type;
}

std::string SharedLicense::licenseKeyMasked() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto key = license_->getKey();
    if (key.size() < 5) return {};
    return key.substr(0, 5) + "-XXXXX-XXXXX-XXXXX";
}

int64_t SharedLicense::expiresAtEpoch() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return license_->checkLicense().expiresAt;
}

void SharedLicense::reload()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Le heartbeat detient une reference vers *license_ : on l'arrete AVANT de
    if (heartbeat_ != nullptr) { heartbeat_->stop(); heartbeat_.reset(); }
    license_ = std::make_unique<LicenseService>();
}

void SharedLicense::activate(const std::string& key,
                             const std::string& email,
                             const std::string& prenom,
                             const std::string& nom,
                             std::function<void(ActivationOutcome)> cb)
{
    std::shared_ptr<WordPressLicenseClient> client;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (! creds_.isConfigured())
        {
            if (cb)
                juce::MessageManager::callAsync([cb] {
                    cb({ false, "Serveur de licence non configure (URL/cle API manquante)." });
                });
            return;
        }
        client = wpClient_;
    }

    const auto machineName = juce::SystemStats::getComputerName().toStdString();

    client->activate(key, email, prenom, nom, machineName,
        [this, key, cb](Services::WordPress::ActivationResult r)
        {
            if (shuttingDown_.load()) return;
            ActivationOutcome out;
            if (r.success)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    license_->activateFromServer(key, r.type, r.expiresAtEpoch, r.signature,
                                                 std::string());
                }
                out.success = true;
                out.message = "Licence activee (" + (r.type.empty() ? std::string("BeatMate") : r.type) + ").";
            }
            else
            {
                out.success = false;
                out.message = r.error.empty() ? "Activation refusee." : r.error;
            }
            if (cb)
                juce::MessageManager::callAsync([cb, out] { cb(out); });
        });
}

void SharedLicense::deactivate(std::function<void(ActivationOutcome)> cb)
{
    std::shared_ptr<WordPressLicenseClient> client;
    std::string key;
    bool configured = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client     = wpClient_;
        key        = license_->getKey();
        configured = creds_.isConfigured();
    }

    if (key.empty() || client == nullptr || ! configured)
    {
        removeLocal();
        if (cb)
            juce::MessageManager::callAsync([cb] { cb({ true, "Licence supprimee localement." }); });
        return;
    }

    client->deactivate(key,
        [this, cb](Services::WordPress::DeactivationResult r)
        {
            if (shuttingDown_.load()) return;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                license_->deactivate();
            }
            ActivationOutcome out;
            out.success = true;
            out.message = r.success ? "Licence desactivee. Vous pouvez l'activer sur une autre machine."
                                    : "Licence supprimee localement (serveur injoignable).";
            if (cb)
                juce::MessageManager::callAsync([cb, out] { cb(out); });
        });
}

void SharedLicense::removeLocal()
{
    std::lock_guard<std::mutex> lock(mutex_);
    license_->deactivate();
}

void SharedLicense::startHeartbeat()
{
    std::lock_guard<std::mutex> lock(mutex_);
    startHeartbeatLocked();
}

void SharedLicense::startHeartbeatLocked()
{
    if (heartbeat_ != nullptr) return;
    if (! creds_.isConfigured()) return;
    if (shuttingDown_.load()) return;
    heartbeat_ = std::make_unique<LicenseHeartbeatService>(wpClient_, *license_);
    heartbeat_->start();
}

void SharedLicense::shutdown()
{
    // Empeche les callbacks reseau detaches de toucher mutex_/license_ pendant
    shuttingDown_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (heartbeat_ != nullptr)
    {
        heartbeat_->stop();
        heartbeat_.reset();
    }
}

} // namespace BeatMate::Shared
