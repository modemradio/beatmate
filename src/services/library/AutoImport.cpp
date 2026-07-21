#include "AutoImport.h"
#include "TrackDataProvider.h"
#include "TrackDatabase.h"
#include "TrackMetadata.h"
#include "../config/SettingsManager.h"
#include "../ai/ClapEmbedQueue.h"
#include "../../app/Application.h"
#include "../../app/ServiceLocator.h"
#include "../../core/analysis/AudioAnalysisPipeline.h"
#include "../../ui/widgets/ToastNotifier.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <spdlog/spdlog.h>

extern BeatMate::ServiceLocator* g_serviceLocator;

namespace BeatMate::Services::Library {

TrackScanner::FileCallback makeAutoImportHandler()
{
    return [](const std::string& path) {
        juce::MessageManager::callAsync([path]() {
            if (!g_serviceLocator) return;
            auto* provider = g_serviceLocator->tryGet<TrackDataProvider>();
            if (!provider) {
                spdlog::warn("[AutoImport] TrackDataProvider unavailable, skipped: {}", path);
                return;
            }
            if (auto* db = g_serviceLocator->tryGet<TrackDatabase>()) {
                if (db->getTrackByPath(path).has_value()) {
                    spdlog::info("[AutoImport] Already in library: {}", path);
                    return;
                }
                const std::string norm = juce::String(path)
                    .replaceCharacter('\\', '/').toLowerCase().toStdString();
                auto dupes = db->getTracksByQuery(
                    "SELECT * FROM tracks WHERE LOWER(REPLACE(file_path, '\\', '/')) = ? LIMIT 1",
                    { norm });
                if (!dupes.empty()) {
                    spdlog::info("[AutoImport] Already in library (path variant): {}", path);
                    return;
                }
            }

            Models::Track track;
            bool gotMeta = false;
            if (auto* metaSvc = g_serviceLocator->tryGet<TrackMetadata>()) {
                if (auto t = metaSvc->readMetadata(path); t.has_value()) {
                    track = *t;
                    gotMeta = true;
                }
            }
            if (!gotMeta) {
                juce::File f{juce::String(path)};
                track.filePath = path;
                track.title = f.getFileNameWithoutExtension().toStdString();
                track.fileFormat = f.getFileExtension().substring(1).toStdString();
                track.fileSize = f.getSize();
                track.dateAdded = juce::Time::currentTimeMillis() / 1000;
            }

            const int64_t id = provider->addTrack(track);
            spdlog::info("[AutoImport] Imported {} (id={})", path, id);
            if (id > 0) {
                if (auto* clap = g_serviceLocator->tryGet<AI::ClapEmbedQueue>())
                    clap->prioritizeTrack(id);
            }
            UI::Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8(u8"Import automatique"),
                juce::File(juce::String(path)).getFileName(),
                UI::Widgets::ToastNotifier::Kind::Success);

            bool analyze = true;
            if (auto* settings = g_serviceLocator->tryGet<Config::SettingsManager>())
                analyze = settings->get<bool>("library.analyzeOnImport", true);

            if (id > 0 && analyze) {
                if (auto* pool = BeatMate::getBackgroundPool()) {
                    pool->addJob([id, path]() {
                        if (!g_serviceLocator) return;
                        auto* pipeline = g_serviceLocator->tryGet<Core::AudioAnalysisPipeline>();
                        auto* prov = g_serviceLocator->tryGet<TrackDataProvider>();
                        if (!pipeline || !prov) return;
                        try {
                            auto result = pipeline->analyzeTrack(path);
                            Models::TrackAnalysis analysis;
                            analysis.trackId = id;
                            analysis.bpm = result.bpm.bpm;
                            analysis.bpmConfidence = static_cast<float>(result.bpm.confidence);
                            analysis.key = result.key.key;
                            analysis.keyConfidence = static_cast<float>(result.key.confidence);
                            analysis.energy = static_cast<float>(result.energy.overall) / 10.0f;
                            analysis.loudness = result.loudness.integratedLUFS;
                            analysis.peakLevel = result.loudness.truePeakdBTP;
                            analysis.analyzedAt = juce::Time::currentTimeMillis() / 1000;
                            for (const auto& sec : Core::EnergyAnalyzer::sectionize(result.energy))
                                analysis.energySegments.emplace_back(sec.startSec, sec.endSec, sec.energy);
                            prov->saveAnalysis(id, analysis);
                            spdlog::info("[AutoImport] Analysis saved for {}", path);
                        } catch (const std::exception& e) {
                            spdlog::warn("[AutoImport] Analysis failed for {}: {}", path, e.what());
                        }
                    });
                }
            }
        });
    };
}

void restoreWatchFoldersFromSettings(TrackScanner& scanner)
{
    bool autoImport = true;
    int count = 0;

    auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("BeatMate")
                    .getChildFile("appsettings.json");
    if (file.existsAsFile()) {
        auto parsed = juce::JSON::parse(file);
        if (auto* root = parsed.getDynamicObject()) {
            if (auto* library = root->getProperty("library").getDynamicObject()) {
                const auto autoVar = library->getProperty("autoImport");
                if (!autoVar.isVoid()) autoImport = (bool) autoVar;
                if (auto* arr = library->getProperty("watchFolders").getArray()) {
                    for (const auto& f : *arr) {
                        if (scanner.watchFolder(f.toString().toStdString(), true)) ++count;
                    }
                }
            }
        }
    }

    if (count == 0) {
        // No configured folder (fresh install on this PC): default to the
        auto music = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
        if (music.isDirectory()
            && scanner.watchFolder(music.getFullPathName().toStdString(), true))
            ++count;
    }

    if (count > 0 && autoImport) {
        scanner.setOnFileAdded(makeAutoImportHandler());
    }
    spdlog::info("[AutoImport] Restored {} watch folder(s) at boot (autoImport={})", count, autoImport);
}

} // namespace BeatMate::Services::Library
