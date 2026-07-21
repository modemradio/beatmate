#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Analysis {

// Contrôle d'intégrité des fichiers audio, basé sur le ffmpeg
class AudioIntegrityChecker {
public:
    enum class Status { Unknown, Ok, Warning, Corrupt, Unreadable, Repaired };

    struct Report {
        Status status = Status::Unknown;
        juce::String details;      // premières lignes d'erreur ffmpeg
        int64_t checkedAt = 0;     // unix seconds
    };

    AudioIntegrityChecker();

    static bool isAvailable();                 // ffmpeg localisable ?
    static juce::String statusLabel(Status s); // libellé FR

    // Vérifie le fichier (bloquant, à appeler depuis un thread de fond).
    Report check(const juce::String& filePath);

    // Répare par re-multiplexage ffmpeg -c copy. L'original est sauvegardé
    Report repair(const juce::String& filePath, juce::String* errorOut = nullptr);

    // Statut connu (Unknown si jamais vérifié ou si le fichier a changé).
    Report statusFor(const juce::String& filePath) const;

    // Recharge le rapport depuis le disque (utile quand une autre vue a scanné).
    void reload() { load(); }

private:
    struct Entry {
        juce::String status;
        juce::String details;
        int64_t checkedAt = 0;
        int64_t fileSize = 0;
        int64_t fileMtime = 0;
    };

    void load();
    void save() const;
    void store(const juce::String& filePath, Status s, const juce::String& details);

    static juce::File reportFile();
    static juce::String statusToKey(Status s);
    static Status keyToStatus(const juce::String& k);

    mutable std::mutex mutex_;
    std::map<juce::String, Entry> entries_;
};

} // namespace BeatMate::Services::Analysis
