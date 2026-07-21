#include "SelectionStemsService.h"

#include "DemucsStemService.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Core::Stems {

namespace {

class TempDirGuard {
public:
    explicit TempDirGuard(juce::File d) : dir(std::move(d)) {}
    ~TempDirGuard() {
        if (dir.exists())
            dir.deleteRecursively();
    }
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;

    juce::File dir;
};

class WorkerThread : public juce::Thread {
public:
    WorkerThread(juce::String name, std::function<void(juce::Thread&)> body)
        : juce::Thread(std::move(name)), m_body(std::move(body)) {}

    void run() override {
        if (m_body) m_body(*this);
    }

private:
    std::function<void(juce::Thread&)> m_body;
};

// NOTE: callbacks are invoked directly from the worker thread.
void postProgress(SelectionStemsService::ProgressCb cb, float pct, juce::String phase) {
    if (!cb) return;
    cb(pct, phase);
}

void postDone(SelectionStemsService::DoneCb cb, StemRangeResult result) {
    if (!cb) return;
    cb(std::move(result));
}

} // namespace

SelectionStemsService::SelectionStemsService() = default;

SelectionStemsService::~SelectionStemsService() {
    cancel();
}

void SelectionStemsService::separateRangeAsync(const juce::File& sourceAudio,
                                               double startSec, double endSec,
                                               std::vector<StemKind> wantedStems,
                                               ProgressCb onProgress,
                                               DoneCb     onDone)
{
    if (m_busy.exchange(true)) {
        StemRangeResult r;
        r.errorMessage = "SelectionStemsService busy";
        postDone(std::move(onDone), std::move(r));
        return;
    }
    m_cancel.store(false);

    {
        const juce::ScopedLock sl(m_lock);
        if (m_worker) {
            m_worker->stopThread(2000);
            m_worker.reset();
        }
    }

    auto body = [this, sourceAudio, startSec, endSec,
                 wanted = std::move(wantedStems),
                 onProgress, onDone](juce::Thread& self) mutable
    {
        StemRangeResult result;
        result.startSec    = startSec;
        result.endSec      = endSec;
        result.sampleRate  = 44100.0;

        const juce::File tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("BeatMate_StemsRange_" + juce::Uuid().toString());
        if (!tempRoot.createDirectory().wasOk()) {
            result.errorMessage = "Cannot create temp dir: " + tempRoot.getFullPathName();
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        TempDirGuard guard(tempRoot);

        if (self.threadShouldExit() || m_cancel.load()) {
            result.errorMessage = "Cancelled";
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        postProgress(onProgress, 0.02f, "extracting range");

        juce::File tempWav = tempRoot.getChildFile("range.wav");
        juce::String err;
        if (!extractRangeToWav(sourceAudio, startSec, endSec, tempWav, err)) {
            result.errorMessage = "Range extraction failed: " + err;
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        if (self.threadShouldExit() || m_cancel.load()) {
            result.errorMessage = "Cancelled";
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        postProgress(onProgress, 0.10f, "running stem separation");

        if (!runSeparation(tempWav, tempRoot, onProgress, err)) {
            result.errorMessage = "Separation failed: " + err;
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        if (self.threadShouldExit() || m_cancel.load()) {
            result.errorMessage = "Cancelled";
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        postProgress(onProgress, 0.95f, "loading stems");

        if (!loadStemsFromDir(tempRoot, result.sampleRate, result, err)) {
            result.errorMessage = "Loading stems failed: " + err;
            postDone(onDone, std::move(result));
            m_busy.store(false);
            return;
        }

        (void) wanted; // stems are loaded into all 4 buffers; caller may pick.

        result.ok = true;
        postProgress(onProgress, 1.0f, "done");
        postDone(onDone, std::move(result));
        m_busy.store(false);
    };

    {
        const juce::ScopedLock sl(m_lock);
        m_worker = std::make_unique<WorkerThread>("SelectionStemsService", std::move(body));
        m_worker->startThread();
    }
}

void SelectionStemsService::cancel() {
    m_cancel.store(true);
    std::unique_ptr<juce::Thread> toJoin;
    {
        const juce::ScopedLock sl(m_lock);
        toJoin = std::move(m_worker);
    }
    if (toJoin) {
        toJoin->signalThreadShouldExit();
        toJoin->stopThread(5000);
    }
    m_busy.store(false);
}

bool SelectionStemsService::extractRangeToWav(const juce::File& sourceAudio,
                                              double startSec, double endSec,
                                              juce::File& outTempWav,
                                              juce::String& errorMessage)
{
    if (!sourceAudio.existsAsFile()) {
        errorMessage = "Source not found: " + sourceAudio.getFullPathName();
        return false;
    }
    if (endSec <= startSec) {
        errorMessage = "Invalid range";
        return false;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(sourceAudio));
    if (reader == nullptr) {
        errorMessage = "Cannot read source audio";
        return false;
    }

    const double srcRate    = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    const int    srcChans   = juce::jmax(1, (int) reader->numChannels);
    const juce::int64 srcLen = (juce::int64) reader->lengthInSamples;

    const juce::int64 startSample = (juce::int64) std::llround(juce::jmax(0.0, startSec) * srcRate);
    const juce::int64 endSample   = (juce::int64) std::llround(endSec * srcRate);
    const juce::int64 startC      = juce::jlimit<juce::int64>(0, srcLen, startSample);
    const juce::int64 endC        = juce::jlimit<juce::int64>(startC, srcLen, endSample);
    const int numSamples          = (int) (endC - startC);

    if (numSamples <= 0) {
        errorMessage = "Empty range";
        return false;
    }

    juce::AudioBuffer<float> buf(srcChans, numSamples);
    buf.clear();
    if (!reader->read(&buf, 0, numSamples, startC, true, srcChans > 1)) {
        errorMessage = "AudioFormatReader::read failed";
        return false;
    }

    // Convert to stereo 44.1k 16-bit for downstream tooling stability.
    const double dstRate = 44100.0;
    const int    dstCh   = 2;

    juce::AudioBuffer<float> stereoBuf(dstCh, numSamples);
    stereoBuf.clear();
    for (int c = 0; c < dstCh; ++c) {
        const int srcCh = juce::jmin(c, srcChans - 1);
        stereoBuf.copyFrom(c, 0, buf, srcCh, 0, numSamples);
    }

    juce::AudioBuffer<float> resampledBuf;
    juce::AudioBuffer<float>* writeBuf = &stereoBuf;
    int writeNumSamples = numSamples;

    if (std::abs(srcRate - dstRate) > 0.5) {
        const double ratio = srcRate / dstRate;
        const int newLen   = (int) std::llround((double) numSamples / ratio);
        if (newLen <= 0) {
            errorMessage = "Resample produced 0 samples";
            return false;
        }
        resampledBuf.setSize(dstCh, newLen);
        resampledBuf.clear();
        for (int c = 0; c < dstCh; ++c) {
            juce::LagrangeInterpolator interp;
            interp.process(ratio,
                           stereoBuf.getReadPointer(c),
                           resampledBuf.getWritePointer(c),
                           newLen);
        }
        writeBuf = &resampledBuf;
        writeNumSamples = newLen;
    }

    if (outTempWav.existsAsFile()) outTempWav.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os(outTempWav.createOutputStream());
    if (os == nullptr) {
        errorMessage = "Cannot open output stream: " + outTempWav.getFullPathName();
        return false;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(os.get(), dstRate, (unsigned int) dstCh, 16, {}, 0));
    if (writer == nullptr) {
        errorMessage = "Cannot create wav writer";
        return false;
    }
    os.release(); // writer owns the stream now.

    if (!writer->writeFromAudioSampleBuffer(*writeBuf, 0, writeNumSamples)) {
        errorMessage = "WAV write failed";
        return false;
    }
    writer.reset();
    return true;
}

bool SelectionStemsService::runSeparation(const juce::File& tempWav,
                                          const juce::File& outDir,
                                          ProgressCb onProgress,
                                          juce::String& errorMessage)
{
    DemucsStemService demucs;
    const auto launcher = DemucsStemService::findDemucsLauncher();
    if (!launcher.empty()) {
        auto progressBridge = [onProgress](float pct, const std::string& phase) {
            postProgress(onProgress,
                         juce::jlimit(0.10f, 0.94f, 0.10f + 0.84f * pct),
                         juce::String(phase));
        };
        const auto res = demucs.separate(tempWav.getFullPathName().toStdString(),
                                         outDir.getFullPathName().toStdString(),
                                         progressBridge);
        if (!res.ok) {
            errorMessage = juce::String(res.message);
            return false;
        }
        // Demucs writes to <outDir>/htdemucs/<basename>/{drums,bass,other,vocals}.wav.
        const juce::File stemDir = outDir.getChildFile("htdemucs")
                                         .getChildFile(tempWav.getFileNameWithoutExtension());
        const char* names[4] = { "drums.wav", "bass.wav", "other.wav", "vocals.wav" };
        for (auto* n : names) {
            const juce::File src = stemDir.getChildFile(n);
            const juce::File dst = outDir.getChildFile(n);
            if (src.existsAsFile()) {
                if (dst.existsAsFile()) dst.deleteFile();
                src.moveFileTo(dst);
            }
        }
        return true;
    }

    errorMessage = "Demucs launcher not available; spectral fallback "
                   "non implemente pour plage isolee";
    spdlog::warn("SelectionStemsService: Demucs unavailable, no fallback executed");
    return false;
}

bool SelectionStemsService::loadStemsFromDir(const juce::File& dir,
                                             double sampleRate,
                                             StemRangeResult& out,
                                             juce::String& errorMessage)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto loadOne = [&](const juce::String& name, juce::AudioBuffer<float>& target) -> bool {
        const juce::File f = dir.getChildFile(name);
        if (!f.existsAsFile()) {
            errorMessage = "Missing stem file: " + f.getFullPathName();
            return false;
        }
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (r == nullptr) {
            errorMessage = "Cannot read stem: " + f.getFullPathName();
            return false;
        }
        const int chans = juce::jmax(1, (int) r->numChannels);
        const int len   = (int) r->lengthInSamples;
        target.setSize(chans, len);
        target.clear();
        if (!r->read(&target, 0, len, 0, true, chans > 1)) {
            errorMessage = "Read failed: " + f.getFullPathName();
            return false;
        }
        out.sampleRate = r->sampleRate > 0.0 ? r->sampleRate : sampleRate;
        return true;
    };

    if (!loadOne("vocals.wav", out.vocals)) return false;
    if (!loadOne("drums.wav",  out.drums))  return false;
    if (!loadOne("bass.wav",   out.bass))   return false;
    if (!loadOne("other.wav",  out.other))  return false;
    return true;
}

} // namespace BeatMate::Core::Stems
