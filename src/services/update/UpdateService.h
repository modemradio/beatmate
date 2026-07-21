#pragma once

#include <string>
#include <functional>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Update {

struct UpdateInfo {
    bool        available = false;
    std::string latestVersion;
    std::string downloadUrl;   // URL absolue du nouveau MSI
    std::string notes;
    bool        mandatory = false;
    int64_t     latestEpoch = 0;   // date de modification du MSI (epoch unix, depuis le manifeste auto)
    std::string date;              // date ISO du MSI
    std::string error;
};

// Met a jour BeatMate depuis un dossier hebergeur (latest.json) ou un MSI local.
// OU depuis un fichier MSI local. Declenche depuis le logiciel (UI Reglages).
class UpdateService {
public:
    explicit UpdateService (std::string currentVersion)
        : currentVersion_ (std::move (currentVersion)) {}

    void setBaseUrl (const std::string& url) { baseUrl_ = url; }
    const std::string& baseUrl() const { return baseUrl_; }
    const std::string& currentVersion() const { return currentVersion_; }

    // Epoch de build de l'app courante (pour la detection de MAJ "par date du fichier").
    void setBuildEpoch (int64_t e) { buildEpoch_ = e; }
    int64_t buildEpoch() const { return buildEpoch_; }

    // Lit <baseUrl>/latest.json et compare a la version courante. Synchrone : appeler hors message thread.
    UpdateInfo checkRemote() const;
    // Variante async ; callback rappele sur le message thread.
    void checkRemoteAsync (std::function<void (UpdateInfo)> cb) const;

    // Telecharge le MSI (streaming -> fichier temp) puis lance l'installeur et quitte l'app.
    void downloadAndInstallAsync (std::string msiUrl,
                                  std::function<void (bool, std::string)> done,
                                  std::function<void (double)> progress = {}) const;

    // Installe depuis un MSI deja present sur le disque (local). Lance msiexec.
    bool installFromFile (const juce::File& msi, std::string& err) const;

    // -1 si a<b, 0 si egal, +1 si a>b (comparaison numerique par segments).
    static int compareVersions (const std::string& a, const std::string& b);

    // Decide s'il y a une mise a jour. La version fait foi ; les dates ne servent de
    // repli que si les deux numeros ne sont pas comparables entre eux.
    static bool decideAvailable (const std::string& currentVersion,
                                 const std::string& latestVersion,
                                 int64_t latestEpoch,
                                 int64_t buildEpoch);

    // Lit la fenetre de mises a jour de la licence a vie (epoch, 0 = illimite).
    static int64_t readUpdatesUntilEpoch();

private:
    bool launchInstaller (const juce::File& msi, std::string& err) const;

    std::string currentVersion_;
    std::string baseUrl_ { "https://beatmate.fr/beatmate-mise-a-jour" };
    int64_t buildEpoch_ {
#ifdef BEATMATE_BUILD_EPOCH
        BEATMATE_BUILD_EPOCH
#else
        0
#endif
    };
};

} // namespace BeatMate::Services::Update
