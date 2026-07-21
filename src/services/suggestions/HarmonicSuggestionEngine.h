#pragma once
#include <vector>
#include <string>
#include <map>
#include <memory>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

class HarmonicSuggestionEngine {
public:
    explicit HarmonicSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~HarmonicSuggestionEngine() = default;

    std::vector<RecommendationResult> suggest(const Models::Track& current, int count = 10);

    static std::vector<std::string> getCompatibleKeys(const std::string& camelotKey);
    static std::string openKeyToCamelot(const std::string& openKey);
    static std::string camelotToOpenKey(const std::string& camelotKey);

private:
    std::shared_ptr<Library::TrackDatabase> database_;
    static const std::map<std::string, std::string> openKeyMap_;
};

} // namespace BeatMate::Services::Suggestions
