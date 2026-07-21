#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Services::Config {

class LocalizationService : public juce::ChangeBroadcaster {
public:
    LocalizationService();

    void setLanguage(const std::string& code);
    std::string getCurrentLanguage() const { return currentLang_; }
    std::string tr(const std::string& key) const;
    std::vector<std::string> getAvailableLanguages() const;

private:
    void loadTranslations(const std::string& langCode);

    std::string currentLang_ = "fr";
    mutable std::mutex lock_;
    std::map<std::string, std::string> translations_;
};

} // namespace BeatMate::Services::Config
