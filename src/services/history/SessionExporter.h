#pragma once

#include <memory>
#include <string>

#include <juce_core/juce_core.h>

#include "SessionHistoryRecorder.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::History {

// Exporte une session enregistrée en playlist : M3U8, Rekordbox XML, Serato crate, Traktor NML, CSV.
class SessionExporter {
public:
    SessionExporter(std::shared_ptr<SessionHistoryRecorder> recorder,
                    std::shared_ptr<Services::Library::TrackDatabase> db);

    bool exportAsM3U8         (const std::string& sessionId, const juce::File& outFile);
    bool exportAsRekordboxXML (const std::string& sessionId, const juce::File& outFile);
    bool exportAsSeratoCrate  (const std::string& sessionId, const juce::File& outFile);
    bool exportAsTraktorNML   (const std::string& sessionId, const juce::File& outFile);
    bool exportAsCSV          (const std::string& sessionId, const juce::File& outFile);

private:
    // Pull (event, hydrated track) pairs in chronological order.
    struct EventTrack {
        PlayEvent     event;
        Models::Track track;
    };
    std::vector<EventTrack> hydrate(const std::string& sessionId) const;

    std::shared_ptr<SessionHistoryRecorder>           m_recorder;
    std::shared_ptr<Services::Library::TrackDatabase> m_db;
};

} // namespace BeatMate::Services::History
