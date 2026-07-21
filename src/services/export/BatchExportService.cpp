#include "BatchExportService.h"
#include "VideoExportService.h"
#include "../library/TrackDataProvider.h"
#include "../library/TrackMetadata.h"
#include "../djsoftware/rekordbox/RekordboxXmlExporter.h"
#include "../djsoftware/traktor/TraktorNmlExporter.h"
#include "../djsoftware/virtualdj/VirtualDJExporter.h"
#include "../djsoftware/serato/SeratoTagWriter.h"
#include "../../core/audio/AudioTrack.h"
#include "../../core/analysis/EbuR128AnalyzerService.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <mutex>
#include <set>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Export {

namespace {

juce::String sanitizeFileName(const juce::String& name)
{
    return name.replaceCharacters("\\/:*?\"<>|", "_________");
}

std::string statusName(TrackExportOutcome::Status s)
{
    switch (s)
    {
        case TrackExportOutcome::Status::Copied:    return "copied";
        case TrackExportOutcome::Status::Converted: return "converted";
        case TrackExportOutcome::Status::Cancelled: return "cancelled";
        default:                                    return "failed";
    }
}
}

class BatchExportService::ExportJob : public juce::ThreadPoolJob {
public:
    explicit ExportJob(BatchExportService& owner)
        : juce::ThreadPoolJob("BatchExport"), m_owner(owner) {}

    JobStatus runJob() override
    {
        while (true)
        {
            const int index = m_owner.m_nextIndex.fetch_add(1);
            if (index >= static_cast<int>(m_owner.m_prepared.size()))
                break;
            m_owner.processItem(index);
            const int done = m_owner.m_doneCount.fetch_add(1) + 1;
            if (done == static_cast<int>(m_owner.m_prepared.size()))
                m_owner.finishBatch();
        }
        return jobHasFinished;
    }

private:
    BatchExportService& m_owner;
};

BatchExportService::BatchExportService(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
}

BatchExportService::~BatchExportService()
{
    m_alive->store(false);
    cancel();
    if (m_pool)
        m_pool->removeAllJobs(true, 8000);
}

bool BatchExportService::isFfmpegAvailable()
{
    return !VideoExportService::findFfmpeg().empty();
}

std::string BatchExportService::formatExtension(int formatId)
{
    switch (formatId)
    {
        case 2: return ".wav";
        case 3: return ".flac";
        case 4: return ".ogg";
        case 5: return ".aac";
        case 6: return ".aiff";
        default: return ".mp3";
    }
}

void BatchExportService::cancel()
{
    m_cancelRequested.store(true);
}

bool BatchExportService::start(std::vector<BatchExportItem> items,
                               BatchExportSettings settings,
                               Callbacks callbacks)
{
    if (m_running.load() || items.empty() || settings.destinationDir.empty())
        return false;

    juce::File destDir(juce::String::fromUTF8(settings.destinationDir.c_str()));
    if (settings.structureId == 2)
        destDir = destDir.getChildFile("Contents").getChildFile("Tracks");
    else if (settings.structureId == 3)
        destDir = destDir.getChildFile("Engine Library").getChildFile("Music");
    if (!destDir.isDirectory() && !destDir.createDirectory().wasOk())
        return false;

    m_settings = std::move(settings);
    m_callbacks = std::move(callbacks);
    m_prepared.clear();
    m_outcomes.clear();
    m_nextIndex.store(0);
    m_doneCount.store(0);
    m_cancelRequested.store(false);

    const std::string ext = formatExtension(m_settings.formatId);
    std::set<juce::String> usedNames;

    for (auto& item : items)
    {
        PreparedItem p;
        p.item = item;
        if (m_provider && item.trackId > 0)
        {
            p.track = m_provider->getTrack(item.trackId);
            p.inLibrary = p.track.id == item.trackId;
            if (p.inLibrary)
                p.cues = m_provider->getCuePoints(item.trackId);
        }
        if (!p.inLibrary)
        {
            Services::Library::TrackMetadata meta;
            auto read = meta.readMetadata(item.sourcePath);
            if (read.has_value())
                p.track = std::move(*read);
            else
            {
                p.track = Models::Track{};
                p.track.filePath = item.sourcePath;
                p.track.title = item.title;
                p.track.artist = item.artist;
            }
        }

        juce::String safeName = sanitizeFileName(
            item.artist.empty() ? juce::String::fromUTF8(item.title.c_str())
                                : juce::String::fromUTF8(item.artist.c_str()) + " - "
                                      + juce::String::fromUTF8(item.title.c_str()));
        if (safeName.trim().isEmpty())
            safeName = juce::File(juce::String::fromUTF8(item.sourcePath.c_str()))
                           .getFileNameWithoutExtension();
        juce::String uniqueName = safeName;
        int suffix = 2;
        while (usedNames.count(uniqueName) > 0)
            uniqueName = safeName + " (" + juce::String(suffix++) + ")";
        usedNames.insert(uniqueName);
        p.destFile = destDir.getChildFile(uniqueName + juce::String(ext));

        m_prepared.push_back(std::move(p));
    }

    m_running.store(true);
    const int threads = juce::jlimit(1, 4, juce::SystemStats::getNumCpus() / 2);
    m_pool = std::make_unique<juce::ThreadPool>(threads);
    for (int i = 0; i < threads; ++i)
        m_pool->addJob(new ExportJob(*this), true);

    spdlog::info("[BatchExport] Started: {} tracks, format={}, {} threads, normalize={}",
                 m_prepared.size(), ext, threads, m_settings.normalize);
    return true;
}

void BatchExportService::processItem(int index)
{
    const auto& prepared = m_prepared[static_cast<size_t>(index)];

    TrackExportOutcome outcome;
    if (m_cancelRequested.load())
    {
        outcome.trackId = prepared.item.trackId;
        outcome.sourcePath = prepared.item.sourcePath;
        outcome.status = TrackExportOutcome::Status::Cancelled;
    }
    else
    {
        outcome = exportOne(prepared);
    }

    {
        std::lock_guard<std::mutex> lock(m_outcomesMutex);
        m_outcomes.push_back(outcome);
    }

    if (m_callbacks.onProgress)
    {
        auto alive = m_alive;
        auto cb = m_callbacks.onProgress;
        const int done = m_doneCount.load() + 1;
        const int total = static_cast<int>(m_prepared.size());
        const juce::String current = juce::String::fromUTF8(prepared.item.title.c_str());
        juce::MessageManager::callAsync([alive, cb, done, total, current]() {
            if (!alive->load()) return;
            cb(done, total, current);
        });
    }
}

TrackExportOutcome BatchExportService::exportOne(const PreparedItem& prepared)
{
    TrackExportOutcome outcome;
    outcome.trackId = prepared.item.trackId;
    outcome.sourcePath = prepared.item.sourcePath;

    juce::File srcFile(juce::String::fromUTF8(prepared.item.sourcePath.c_str()));
    if (!srcFile.existsAsFile())
    {
        outcome.message = "source introuvable";
        return outcome;
    }

    const juce::File& destFile = prepared.destFile;
    const juce::String srcExt = srcFile.getFileExtension().toLowerCase();
    const juce::String destExt = destFile.getFileExtension().toLowerCase();

    bool audioWritten = false;
    if (srcExt == destExt && !m_settings.normalize)
    {
        destFile.deleteFile();
        if (!srcFile.copyFileTo(destFile))
        {
            outcome.message = "copie impossible";
            return outcome;
        }
        outcome.status = TrackExportOutcome::Status::Copied;
        audioWritten = true;
    }

    if (!audioWritten)
    {
        juce::AudioFormatManager fmtMgr;
        fmtMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(srcFile));
        if (!reader)
        {
            outcome.message = "lecture source impossible";
            return outcome;
        }
        if (reader->lengthInSamples <= 0
            || reader->lengthInSamples > static_cast<juce::int64>(INT_MAX - 64))
        {
            outcome.message = "source invalide";
            return outcome;
        }

        const int numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
        const int numFrames = static_cast<int>(reader->lengthInSamples);
        juce::AudioBuffer<float> buffer(numChannels, numFrames);
        if (!reader->read(&buffer, 0, numFrames, 0, true, numChannels > 1))
        {
            outcome.message = "decodage impossible";
            return outcome;
        }
        const int srcRate = static_cast<int>(reader->sampleRate);
        reader.reset();

        if (m_cancelRequested.load())
        {
            outcome.status = TrackExportOutcome::Status::Cancelled;
            return outcome;
        }

        if (m_settings.normalize)
        {
            std::vector<float> interleaved(static_cast<size_t>(numFrames) * numChannels);
            for (int f = 0; f < numFrames; ++f)
                for (int c = 0; c < numChannels; ++c)
                    interleaved[static_cast<size_t>(f) * numChannels + c] = buffer.getSample(c, f);

            Core::AudioTrack analysisTrack;
            analysisTrack.loadData(std::move(interleaved), srcRate, numChannels);
            Core::EbuR128AnalyzerService analyzer;
            auto lufs = analyzer.analyze(analysisTrack);
            outcome.measuredLufs = lufs.integratedLUFS;

            if (lufs.integratedLUFS <= -69.5f)
            {
                outcome.normalizationSkipped = true;
                outcome.message = "normalisation ignoree (niveau non mesurable)";
            }
            else
            {
                float gainDb = m_settings.targetLufs - lufs.integratedLUFS;
                const float headroom = -1.0f - lufs.truePeakdBTP;
                if (gainDb > headroom)
                {
                    gainDb = headroom;
                    outcome.gainLimitedByPeak = true;
                }
                outcome.appliedGainDb = gainDb;
                outcome.normalized = true;
                buffer.applyGain(juce::Decibels::decibelsToGain(gainDb));
            }
        }

        int outRate = srcRate;
        if (m_settings.sampleRate > 0 && m_settings.sampleRate != srcRate)
        {
            const double ratio = static_cast<double>(srcRate) / m_settings.sampleRate;
            const int outFrames = static_cast<int>(numFrames / ratio);
            juce::AudioBuffer<float> resampled(numChannels, outFrames);
            for (int c = 0; c < numChannels; ++c)
            {
                juce::LagrangeInterpolator interp;
                interp.process(ratio, buffer.getReadPointer(c),
                               resampled.getWritePointer(c), outFrames);
            }
            buffer = std::move(resampled);
            outRate = m_settings.sampleRate;
        }

        if (m_cancelRequested.load())
        {
            outcome.status = TrackExportOutcome::Status::Cancelled;
            return outcome;
        }

        std::string encodeError;
        bool ok = false;
        if (m_settings.formatId == 1 || m_settings.formatId == 5)
            ok = encodeWithFfmpeg(buffer, outRate, destFile, encodeError);
        else
            ok = encodeWithJuce(buffer, outRate, destFile, encodeError);

        if (!ok)
        {
            outcome.message = encodeError.empty() ? "encodage impossible" : encodeError;
            destFile.deleteFile();
            if (m_cancelRequested.load())
                outcome.status = TrackExportOutcome::Status::Cancelled;
            return outcome;
        }
        outcome.status = TrackExportOutcome::Status::Converted;
    }

    outcome.outputPath = destFile.getFullPathName().toStdString();
    outcome.fileSizeBytes = destFile.getSize();

    if (m_settings.writeTags)
    {
        Models::Track tagTrack = prepared.track;
        if (tagTrack.key.empty() && !tagTrack.camelotKey.empty())
            tagTrack.key = tagTrack.camelotKey;
        Services::Library::TrackMetadata meta;
        outcome.tagsWritten = meta.writeMetadata(outcome.outputPath, tagTrack);
        if (!outcome.tagsWritten && outcome.message.empty())
            outcome.message = "tags non ecrits";
        if (prepared.track.bpm <= 0.0 && outcome.message.empty())
            outcome.message = "BPM absent (non analyse)";
    }

    if (m_settings.targetSerato && prepared.inLibrary && !prepared.cues.empty())
    {
        Services::Serato::SeratoTagWriter writer;
        auto seratoCues = Services::Serato::SeratoTagWriter::fromBeatMateCues(prepared.cues);
        outcome.seratoWritten = writer.writeToFile(outcome.outputPath, seratoCues, {},
                                                   static_cast<float>(prepared.track.bpm), 0.0);
    }

    return outcome;
}

bool BatchExportService::writeTempWav(const juce::AudioBuffer<float>& buffer, int sampleRate,
                                      const juce::File& tempFile, std::string& error)
{
    tempFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(tempFile.createOutputStream());
    if (!stream)
    {
        error = "creation temporaire impossible";
        return false;
    }
    juce::WavAudioFormat wav;
    const int bits = m_settings.bitDepth >= 24 ? 24 : 16;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sampleRate,
                            static_cast<unsigned int>(buffer.getNumChannels()), bits, {}, 0));
    if (!writer)
    {
        error = "writer WAV indisponible";
        return false;
    }
    stream.release();
    const bool ok = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    if (!ok)
        error = "ecriture WAV temporaire impossible";
    return ok;
}

bool BatchExportService::encodeWithFfmpeg(const juce::AudioBuffer<float>& buffer, int sampleRate,
                                          const juce::File& dest, std::string& error)
{
    const std::string ffmpeg = VideoExportService::findFfmpeg();
    if (ffmpeg.empty())
    {
        error = "ffmpeg introuvable";
        return false;
    }

    juce::File tempWav = dest.getSiblingFile(".bm_tmp_"
        + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()) + ".wav");
    if (!writeTempWav(buffer, sampleRate, tempWav, error))
    {
        tempWav.deleteFile();
        return false;
    }

    juce::StringArray args;
    args.add(juce::String::fromUTF8(ffmpeg.c_str()));
    args.add("-y");
    args.add("-i");
    args.add(tempWav.getFullPathName());
    if (m_settings.formatId == 1)
    {
        args.add("-codec:a");
        args.add("libmp3lame");
        if (m_settings.vbr)
        {
            args.add("-q:a");
            args.add("0");
        }
        else
        {
            args.add("-b:a");
            args.add(juce::String(m_settings.bitRateKbps) + "k");
        }
    }
    else
    {
        args.add("-c:a");
        args.add("aac");
        args.add("-b:a");
        args.add(juce::String(m_settings.bitRateKbps) + "k");
    }
    args.add("-ar");
    args.add(juce::String(sampleRate));
    args.add(dest.getFullPathName());

    dest.deleteFile();
    juce::ChildProcess process;
    bool ok = false;
    if (process.start(args, 0))
    {
        while (process.isRunning())
        {
            if (m_cancelRequested.load())
            {
                process.kill();
                break;
            }
            juce::Thread::sleep(100);
        }
        ok = !m_cancelRequested.load() && process.getExitCode() == 0
             && dest.existsAsFile() && dest.getSize() > 0;
        if (!ok && !m_cancelRequested.load())
            error = "ffmpeg a echoue (code " + std::to_string(process.getExitCode()) + ")";
    }
    else
    {
        error = "lancement ffmpeg impossible";
    }

    tempWav.deleteFile();
    if (m_cancelRequested.load())
        dest.deleteFile();
    return ok;
}

bool BatchExportService::encodeWithJuce(const juce::AudioBuffer<float>& buffer, int sampleRate,
                                        const juce::File& dest, std::string& error)
{
    std::unique_ptr<juce::AudioFormat> format;
    int bits = m_settings.bitDepth >= 24 ? 24 : 16;
    int qualityIndex = 0;

    switch (m_settings.formatId)
    {
        case 2: format = std::make_unique<juce::WavAudioFormat>(); break;
        case 3: format = std::make_unique<juce::FlacAudioFormat>(); break;
        case 6: format = std::make_unique<juce::AiffAudioFormat>(); break;
        case 4:
        {
            auto ogg = std::make_unique<juce::OggVorbisAudioFormat>();
            qualityIndex = juce::jmax(0, ogg->getQualityOptions().size() - 1);
            bits = 16;
            format = std::move(ogg);
            break;
        }
        default:
            error = "format non supporte";
            return false;
    }

    dest.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(dest.createOutputStream());
    if (!stream)
    {
        error = "creation fichier impossible";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format->createWriterFor(stream.get(), sampleRate,
                                static_cast<unsigned int>(buffer.getNumChannels()),
                                bits, {}, qualityIndex));
    if (!writer)
    {
        error = "writer indisponible";
        return false;
    }
    stream.release();
    const bool ok = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    if (!ok)
        error = "ecriture impossible";
    return ok && dest.existsAsFile() && dest.getSize() > 0;
}

void BatchExportService::finishBatch()
{
    BatchExportReport report;
    report.settings = m_settings;
    report.cancelled = m_cancelRequested.load();
    {
        std::lock_guard<std::mutex> lock(m_outcomesMutex);
        report.outcomes = m_outcomes;
    }

    std::vector<const TrackExportOutcome*> succeeded;
    for (const auto& o : report.outcomes)
    {
        switch (o.status)
        {
            case TrackExportOutcome::Status::Copied:
            case TrackExportOutcome::Status::Converted:
                report.succeeded++;
                report.totalBytes += o.fileSizeBytes;
                succeeded.push_back(&o);
                break;
            case TrackExportOutcome::Status::Cancelled:
                report.cancelledCount++;
                break;
            default:
                report.failed++;
                break;
        }
    }

    juce::File destDir(juce::String::fromUTF8(m_settings.destinationDir.c_str()));

    if (m_settings.writeM3U && !succeeded.empty())
    {
        juce::String content = "#EXTM3U\n";
        for (const auto* o : succeeded)
        {
            for (const auto& p : m_prepared)
            {
                if (p.item.sourcePath != o->sourcePath)
                    continue;
                content += "#EXTINF:" + juce::String(static_cast<int>(p.item.durationSec)) + ","
                           + juce::String::fromUTF8(p.item.artist.c_str()) + " - "
                           + juce::String::fromUTF8(p.item.title.c_str()) + "\n";
                break;
            }
            content += juce::String::fromUTF8(o->outputPath.c_str()) + "\n";
        }
        juce::File m3u = destDir.getChildFile("BeatMate Export.m3u8");
        if (m3u.replaceWithText(content, false, false, "\n"))
            report.m3uPath = m3u.getFullPathName().toStdString();
    }

    auto findPrepared = [this](const std::string& sourcePath) -> const PreparedItem* {
        for (const auto& p : m_prepared)
            if (p.item.sourcePath == sourcePath)
                return &p;
        return nullptr;
    };

    if (m_settings.targetRekordbox && !succeeded.empty())
    {
        Services::Rekordbox::RekordboxXmlExporter exporter;
        Services::Rekordbox::RekordboxXmlExporter::ExportPlaylist playlist;
        playlist.name = "BeatMate Export";
        for (const auto* o : succeeded)
        {
            const auto* p = findPrepared(o->sourcePath);
            if (!p || !p->inLibrary) continue;
            auto et = Services::Rekordbox::RekordboxXmlExporter::fromBeatMateTrack(p->track, p->cues);
            et.filePath = o->outputPath;
            exporter.addTrack(et);
            playlist.trackIds.push_back(p->track.id);
        }
        exporter.addPlaylist(playlist);
        juce::File out = destDir.getChildFile("BeatMate Rekordbox.xml");
        if (exporter.exportToFile(out.getFullPathName().toStdString()))
            report.djLibraryFiles.push_back(out.getFullPathName().toStdString());
    }

    if (m_settings.targetTraktor && !succeeded.empty())
    {
        Services::Traktor::TraktorNmlExporter exporter;
        for (const auto* o : succeeded)
        {
            const auto* p = findPrepared(o->sourcePath);
            if (!p || !p->inLibrary) continue;
            auto et = Services::Traktor::TraktorNmlExporter::fromBeatMateTrack(p->track, p->cues);
            et.filePath = o->outputPath;
            exporter.addTrack(et);
        }
        juce::File out = destDir.getChildFile("BeatMate Traktor.nml");
        if (exporter.exportToFile(out.getFullPathName().toStdString()))
            report.djLibraryFiles.push_back(out.getFullPathName().toStdString());
    }

    if (m_settings.targetVirtualDJ && !succeeded.empty())
    {
        Services::VirtualDJ::VirtualDJExporter exporter;
        for (const auto* o : succeeded)
        {
            const auto* p = findPrepared(o->sourcePath);
            if (!p || !p->inLibrary) continue;
            auto et = Services::VirtualDJ::VirtualDJExporter::fromBeatMateTrack(p->track, p->cues);
            et.filePath = o->outputPath;
            exporter.addTrack(et);
        }
        juce::File out = destDir.getChildFile("BeatMate VirtualDJ.xml");
        if (exporter.exportToFile(out.getFullPathName().toStdString()))
            report.djLibraryFiles.push_back(out.getFullPathName().toStdString());
    }

    {
        nlohmann::json j;
        j["succeeded"] = report.succeeded;
        j["failed"] = report.failed;
        j["cancelled"] = report.cancelledCount;
        j["totalBytes"] = report.totalBytes;
        j["djLibraryFiles"] = report.djLibraryFiles;
        j["tracks"] = nlohmann::json::array();
        for (const auto& o : report.outcomes)
        {
            nlohmann::json t;
            t["source"] = o.sourcePath;
            t["output"] = o.outputPath;
            t["status"] = statusName(o.status);
            t["normalized"] = o.normalized;
            t["normalizationSkipped"] = o.normalizationSkipped;
            t["gainLimitedByPeak"] = o.gainLimitedByPeak;
            t["measuredLufs"] = o.measuredLufs;
            t["appliedGainDb"] = o.appliedGainDb;
            t["tagsWritten"] = o.tagsWritten;
            t["seratoWritten"] = o.seratoWritten;
            t["message"] = o.message;
            j["tracks"].push_back(t);
        }
        juce::File jsonFile = destDir.getChildFile("BeatMate Export Report.json");
        if (jsonFile.replaceWithText(juce::String::fromUTF8(j.dump(2).c_str())))
            report.reportJsonPath = jsonFile.getFullPathName().toStdString();
    }

    spdlog::info("[BatchExport] Finished: {} ok, {} failed, {} cancelled, {} MB",
                 report.succeeded, report.failed, report.cancelledCount,
                 report.totalBytes / (1024 * 1024));

    m_running.store(false);

    if (m_callbacks.onFinished)
    {
        auto alive = m_alive;
        auto cb = m_callbacks.onFinished;
        juce::MessageManager::callAsync([alive, cb, report = std::move(report)]() {
            if (!alive->load()) return;
            cb(report);
        });
    }
}

} // namespace BeatMate::Services::Export
