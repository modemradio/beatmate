#include "AudioListenerService.h"

#include "../library/TrackDataProvider.h"
#include "../../models/Track.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <audioclient.h>
 #include <mmdeviceapi.h>
 #include <avrt.h>
 #include <functiondiscoverykeys_devpkey.h>
 #pragma comment(lib, "Avrt.lib")
 #pragma comment(lib, "Ole32.lib")
#endif

namespace BeatMate::Services::Audio {

namespace {

void fftRadix2(std::vector<std::complex<float>>& x) {
    const size_t n = x.size();
    if (n <= 1) return;
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / (float) len;
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = x[i + k];
                auto v = x[i + k + len / 2] * w;
                x[i + k]             = u + v;
                x[i + k + len / 2]   = u - v;
                w *= wlen;
            }
        }
    }
}

std::vector<float> hann(int n) {
    std::vector<float> w(n);
    for (int i = 0; i < n; ++i)
        w[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i / (n - 1));
    return w;
}

uint32_t quantizeChromaToHash(const std::vector<float>& chroma) {
    // 3 bits par bin * 12 bins = 36 bits, replie sur 32 bits par XOR.
    uint64_t combined = 0;
    for (int i = 0; i < 12; ++i) {
        int q = (int) std::round(chroma[i] * 7.0f);
        q = std::clamp(q, 0, 7);
        combined |= ((uint64_t) q) << (i * 3);
    }
    return (uint32_t) (combined ^ (combined >> 32));
}

// Downmix to mono and resample (crude linear) to fsOut.
std::vector<float> downsampleMono(const float* interleaved, int channels,
                                  size_t frames, int srIn, int srOut) {
    if (srIn <= 0 || srOut <= 0) return {};
    const double ratio = (double) srOut / (double) srIn;
    const size_t outFrames = (size_t) (frames * ratio);
    std::vector<float> out(outFrames, 0.0f);
    for (size_t i = 0; i < outFrames; ++i) {
        const double srcIdx = (double) i / ratio;
        const size_t i0 = (size_t) srcIdx;
        if (i0 >= frames) break;
        float s = 0.0f;
        for (int c = 0; c < channels; ++c)
            s += interleaved[i0 * channels + c];
        out[i] = s / channels;
    }
    return out;
}

} // namespace

AudioListenerService::AudioListenerService() {
    ring_.assign((size_t) fsHash_ * ringSeconds_, 0.0f);
}

AudioListenerService::~AudioListenerService() { stop(); }

void AudioListenerService::setProvider(BeatMate::Services::Library::TrackDataProvider* p) {
    {
        std::lock_guard<std::mutex> lk(providerMutex_);
        provider_ = p;
    }
    // Reveille buildIndexLoop qui peut etre en attente.
    providerCv_.notify_all();
}

void AudioListenerService::setOnDetection(DetectionCallback cb) {
    std::lock_guard<std::mutex> lk(detectionCbMutex_);
    onDetection_ = std::move(cb);
}

void AudioListenerService::setDJHint(const DJHint& hint) {
    std::lock_guard<std::mutex> lk(hintMutex_);
    hint_ = hint;
    hintValid_ = true;
}

void AudioListenerService::clearDJHint() {
    std::lock_guard<std::mutex> lk(hintMutex_);
    hintValid_ = false;
    hint_ = {};
}

AudioListenerService::Status AudioListenerService::getStatus() const {
    std::lock_guard<std::mutex> lk(statusMutex_);
    return status_;
}

bool AudioListenerService::start() {
    if (running_.exchange(true)) return true;

    indexRunning_.store(true);
    indexThread_ = std::thread([this]() { buildIndexLoop(); });

    captureThread_ = std::thread([this]() { captureLoop(); });

    spdlog::info("AudioListenerService: started");
    return true;
}

void AudioListenerService::stop() {
    if (!running_.exchange(false)) return;
    indexRunning_.store(false);
    // Reveille un eventuel wait dans buildIndexLoop.
    providerCv_.notify_all();
    if (indexThread_.joinable()) indexThread_.join();
    if (captureThread_.joinable()) captureThread_.join();
    spdlog::info("AudioListenerService: stopped");
}

void AudioListenerService::captureLoop() {
#if JUCE_WINDOWS
    bool needCoUninit = false;
    {
        HRESULT hrInit = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
            spdlog::error("AudioListenerService: CoInitializeEx failed (hr=0x{:08x})",
                          (uint32_t) hrInit);
            std::lock_guard<std::mutex> lk(statusMutex_);
            status_.lastError  = "CoInitializeEx failed";
            status_.deviceOpen = false;
            return;
        }
        // S_OK ou S_FALSE => on a augmente le compteur, il faudra liberer.
        needCoUninit = (hrInit == S_OK || hrInit == S_FALSE);
    }

    HRESULT hr = S_OK;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioCaptureClient* capture    = nullptr;
    WAVEFORMATEX*        mixFormat  = nullptr;
    HANDLE               eventHandle = nullptr;
    auto lastMatch = std::chrono::steady_clock::now();

    auto setErr = [this](const std::string& e) {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.lastError = e;
        status_.deviceOpen = false;
        spdlog::warn("AudioListenerService: {}", e);
    };

    hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                            __uuidof(IMMDeviceEnumerator), (void**) &enumerator);
    if (FAILED(hr)) { setErr("CoCreateInstance(MMDeviceEnumerator) failed"); goto cleanup; }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) { setErr("GetDefaultAudioEndpoint failed"); goto cleanup; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**) &client);
    if (FAILED(hr)) { setErr("IMMDevice::Activate failed"); goto cleanup; }

    hr = client->GetMixFormat(&mixFormat);
    if (FAILED(hr)) { setErr("GetMixFormat failed"); goto cleanup; }

    // Loopback + shared mode + event-driven ; 1s buffer suffit.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_LOOPBACK
                                | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                             10000000LL /*1s*/, 0, mixFormat, nullptr);
    if (FAILED(hr)) { setErr("IAudioClient::Initialize(LOOPBACK) failed"); goto cleanup; }

    eventHandle = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) { setErr("CreateEvent failed"); goto cleanup; }

    hr = client->SetEventHandle(eventHandle);
    if (FAILED(hr)) { setErr("SetEventHandle failed"); goto cleanup; }

    hr = client->GetService(__uuidof(IAudioCaptureClient), (void**) &capture);
    if (FAILED(hr)) { setErr("GetService(CaptureClient) failed"); goto cleanup; }

    hr = client->Start();
    if (FAILED(hr)) { setErr("IAudioClient::Start failed"); goto cleanup; }

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.deviceOpen = true;
        status_.lastError.clear();
    }

    lastMatch = std::chrono::steady_clock::now();
    while (running_.load()) {
        DWORD waitRc = ::WaitForSingleObject(eventHandle, 200);
        if (waitRc == WAIT_FAILED) { setErr("WaitForSingleObject failed"); break; }
        if (!running_.load()) break;

        UINT32 framesAvailable = 0;
        hr = capture->GetNextPacketSize(&framesAvailable);
        if (FAILED(hr)) { setErr("GetNextPacketSize failed"); break; }

        while (framesAvailable > 0 && running_.load()) {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { setErr("GetBuffer failed"); break; }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                spdlog::warn("AudioListenerService: audio discontinuity (driver glitch)");
            }

            if (numFrames > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                const int channels = mixFormat->nChannels;
                const int srIn     = (int) mixFormat->nSamplesPerSec;

                bool      isFloat       = false;
                int       pcmBitsPerSmp = 0;
                if (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                    isFloat = true;
                } else if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                    auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat);
                    if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
                        isFloat = true;
                    else if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
                        pcmBitsPerSmp = mixFormat->wBitsPerSample;
                } else if (mixFormat->wFormatTag == WAVE_FORMAT_PCM) {
                    pcmBitsPerSmp = mixFormat->wBitsPerSample;
                }

                std::vector<float> floatBuf;
                const float*       interleaved = nullptr;

                if (isFloat) {
                    interleaved = reinterpret_cast<const float*>(data);
                } else if (pcmBitsPerSmp == 16) {
                    const int16_t* s16 = reinterpret_cast<const int16_t*>(data);
                    floatBuf.resize((size_t) numFrames * channels);
                    for (UINT32 i = 0; i < numFrames; ++i)
                        for (int c = 0; c < channels; ++c)
                            floatBuf[(size_t) i * channels + c] =
                                (float) s16[i * channels + c] / 32768.0f;
                    interleaved = floatBuf.data();
                } else if (pcmBitsPerSmp == 24) {
                    // 3 bytes par sample, little-endian, signe.
                    floatBuf.resize((size_t) numFrames * channels);
                    for (UINT32 i = 0; i < numFrames; ++i) {
                        for (int c = 0; c < channels; ++c) {
                            const BYTE* p = data + (i * channels + c) * 3;
                            int32_t v = (int32_t) p[0]
                                       | ((int32_t) p[1] << 8)
                                       | ((int32_t) p[2] << 16);
                            // Sign-extend depuis 24 bits.
                            if (v & 0x00800000) v |= 0xFF000000;
                            floatBuf[(size_t) i * channels + c] =
                                (float) v / 8388608.0f;
                        }
                    }
                    interleaved = floatBuf.data();
                } else if (pcmBitsPerSmp == 32) {
                    const int32_t* s32 = reinterpret_cast<const int32_t*>(data);
                    floatBuf.resize((size_t) numFrames * channels);
                    for (UINT32 i = 0; i < numFrames; ++i)
                        for (int c = 0; c < channels; ++c)
                            floatBuf[(size_t) i * channels + c] =
                                (float) s32[i * channels + c] / 2147483648.0f;
                    interleaved = floatBuf.data();
                }

                if (interleaved != nullptr) {
                    auto mono = downsampleMono(interleaved, channels, numFrames,
                                                srIn, fsHash_);
                    if (!mono.empty()) {
                        std::lock_guard<std::mutex> lk(ringMutex_);
                        const size_t cap = ring_.size();
                        if (!ringFilled_ && ringWritePos_ + mono.size() >= cap)
                            ringFilled_ = true;
                        for (float s : mono) {
                            ring_[ringWritePos_] = s;
                            ringWritePos_ = (ringWritePos_ + 1) % cap;
                        }
                    }
                }
            }

            capture->ReleaseBuffer(numFrames);

            hr = capture->GetNextPacketSize(&framesAvailable);
            if (FAILED(hr)) { setErr("GetNextPacketSize failed"); break; }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastMatch).count() >= 5) {
            lastMatch = now;

            std::vector<float> snapshot;
            {
                std::lock_guard<std::mutex> lk(ringMutex_);
                if (!ringFilled_) continue;
                snapshot.resize(ring_.size());
                const size_t n = ring_.size();
                for (size_t i = 0; i < n; ++i)
                    snapshot[i] = ring_[(ringWritePos_ + i) % n];
            }

            auto liveHashes = computeChromaHashes(snapshot, fsHash_);
            if (liveHashes.empty()) continue;

            auto [trackId, confidence] = matchAgainstIndex(liveHashes);

            DJHint hintCopy{};
            bool hintOk = false;
            {
                std::lock_guard<std::mutex> lk(hintMutex_);
                hintOk = hintValid_;
                hintCopy = hint_;
            }

            if (trackId != 0 && confidence > 0.15) {
                if (trackId == lastDetected_) lastDetectedHits_++;
                else {
                    lastDetected_ = trackId;
                    lastDetectedHits_ = 1;
                    lastConfidenceEma_ = 0.0;
                }
                lastConfidenceEma_ = 0.4 * confidence + 0.6 * lastConfidenceEma_;

                if (provider_) {
                    auto track = provider_->getTrack(trackId);
                    bool hybridLock = false;
                    double finalConf = lastConfidenceEma_;
                    if (hintOk && !track.title.empty()) {
                        bool titleOk = !hintCopy.title.empty()
                            && juce::String(track.title).containsIgnoreCase(juce::String(hintCopy.title));
                        bool artistOk = !hintCopy.artist.empty()
                            && juce::String(track.artist).containsIgnoreCase(juce::String(hintCopy.artist));
                        bool bpmOk = (hintCopy.bpm > 0.0 && track.bpm > 0.0)
                            && std::abs(hintCopy.bpm - track.bpm) / track.bpm < 0.02;
                        if (titleOk || artistOk || bpmOk) {
                            hybridLock = true;
                            finalConf = 1.0;
                        }
                    }
                    if (lastConfidenceEma_ >= 0.85 && lastDetectedHits_ >= 3) {
                        finalConf = 1.0;
                    }

                    const bool shouldEmit = hybridLock
                        || (lastDetectedHits_ >= 2 && lastConfidenceEma_ >= 0.30)
                        || (lastDetectedHits_ >= 3 && lastConfidenceEma_ >= 0.20);

                    if (shouldEmit && !track.title.empty()) {
                        Detection d;
                        d.trackId    = trackId;
                        d.title      = track.title;
                        d.artist     = track.artist;
                        d.confidence = finalConf;
                        d.at         = std::chrono::system_clock::now();
                        {
                            std::lock_guard<std::mutex> lk(statusMutex_);
                            status_.lastMatch = track.artist + " - " + track.title
                                              + " (" + std::to_string((int)(finalConf*100)) + "%"
                                              + (hybridLock ? " hybrid" : "") + ")";
                        }
                        spdlog::info("[AudioListener] match '{} - {}' raw={:.2f} ema={:.2f} final={:.2f} {}",
                                     track.artist, track.title,
                                     confidence, lastConfidenceEma_, finalConf,
                                     hybridLock ? "[DJ hint confirmed]" : "");
                        DetectionCallback cb;
                        {
                            std::lock_guard<std::mutex> lk(detectionCbMutex_);
                            cb = onDetection_;
                        }
                        juce::MessageManager::callAsync([cb, d]() { if (cb) cb(d); });
                        if (hybridLock || finalConf >= 0.99) {
                            lastDetectedHits_ = 0;
                            lastConfidenceEma_ = 0.0;
                        }
                    }
                }
            } else {
                lastDetected_ = 0;
                lastDetectedHits_ = 0;
                lastConfidenceEma_ = 0.0;
            }
        }
    }

    client->Stop();

cleanup:
    if (capture)    capture->Release();
    if (client)     client->Release();
    if (device)     device->Release();
    if (enumerator) enumerator->Release();
    if (mixFormat)  ::CoTaskMemFree(mixFormat);
    ::CoUninitialize();
#else
    // Non-Windows : no-op for now. Could use PulseAudio monitor / CoreAudio tap.
    while (running_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
}

void AudioListenerService::buildIndexLoop() {
    BeatMate::Services::Library::TrackDataProvider* prov = nullptr;
    {
        std::unique_lock<std::mutex> lk(providerMutex_);
        providerCv_.wait(lk, [this]() {
            return !indexRunning_.load() || provider_ != nullptr;
        });
        prov = provider_;
    }
    if (!indexRunning_.load() || prov == nullptr) return;

    auto tracks = prov->getAllTracks();
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.tracksTotal = (int) tracks.size();
    }

    juce::AudioFormatManager fmts; fmts.registerBasicFormats();
    int done = 0;

    for (const auto& t : tracks) {
        if (!indexRunning_.load()) return;
        if (t.filePath.empty()) continue;

        juce::File f(juce::String(t.filePath));
        if (!f.existsAsFile()) continue;

        std::unique_ptr<juce::AudioFormatReader> reader(fmts.createReaderFor(f));
        if (!reader) continue;
        const int  sr   = (int) reader->sampleRate;
        const int  ch   = (int) reader->numChannels;
        const auto len  = reader->lengthInSamples;
        if (sr <= 0 || ch <= 0 || len <= 0) continue;

        // Charge max 60s du milieu pour couvrir les variations intro/outro.
        const int wantSec = 60;
        const int64_t wantFrames = std::min<int64_t>((int64_t) wantSec * sr, len);
        const int64_t startFrame = std::max<int64_t>(0, (len - wantFrames) / 2);

        juce::AudioBuffer<float> buf(ch, (int) wantFrames);
        buf.clear();
        if (!reader->read(&buf, 0, (int) wantFrames, startFrame, true, true)) continue;

        std::vector<float> interleaved((size_t) wantFrames * ch);
        for (int i = 0; i < wantFrames; ++i)
            for (int c = 0; c < ch; ++c)
                interleaved[(size_t) i * ch + c] = buf.getSample(c, i);
        auto mono = downsampleMono(interleaved.data(), ch, (size_t) wantFrames,
                                    sr, fsHash_);
        if (mono.empty()) continue;

        auto hashes = computeChromaHashes(mono, fsHash_);
        if (hashes.empty()) continue;

        {
            std::lock_guard<std::mutex> lk(indexMutex_);
            indexTrackHashCount_[t.id] = (int) hashes.size();
            for (auto h : hashes)
                index_[h].push_back(t.id);
        }

        ++done;
        if ((done & 15) == 0) {
            std::lock_guard<std::mutex> lk(statusMutex_);
            status_.tracksIndexed = done;
        }
    }

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.tracksIndexed = done;
        status_.indexReady    = true;
    }
    spdlog::info("AudioListenerService: index ready ({} tracks fingerprinted)", done);
}

std::vector<uint32_t>
AudioListenerService::computeChromaHashes(const std::vector<float>& mono,
                                          int sampleRate) const {
    std::vector<uint32_t> out;
    if ((int) mono.size() < fftSize_) return out;
    const auto window = hann(fftSize_);

    for (size_t offset = 0; offset + fftSize_ <= mono.size(); offset += hopSize_) {
        std::vector<std::complex<float>> x(fftSize_);
        for (int i = 0; i < fftSize_; ++i)
            x[i] = std::complex<float>(mono[offset + i] * window[i], 0.0f);
        fftRadix2(x);

        std::vector<float> chroma(12, 0.0f);
        for (int bin = 1; bin < fftSize_ / 2; ++bin) {
            const double freq = (double) bin * sampleRate / fftSize_;
            if (freq < 60.0 || freq > 4000.0) continue;
            const double midi = 12.0 * std::log2(freq / 440.0) + 69.0;
            int c = ((int) std::round(midi)) % 12;
            if (c < 0) c += 12;
            const float mag = std::abs(x[bin]);
            chroma[c] += mag;
        }
        float maxC = *std::max_element(chroma.begin(), chroma.end());
        if (maxC <= 0.0f) continue;
        for (auto& v : chroma) v /= maxC;
        out.push_back(quantizeChromaToHash(chroma));
    }
    return out;
}

std::pair<int64_t, double>
AudioListenerService::matchAgainstIndex(const std::vector<uint32_t>& live) const {
    if (live.size() < 2) return { 0, 0.0 };

    std::unordered_map<int64_t, int> uniVotes;
    std::unordered_map<int64_t, int> biVotes;
    {
        std::lock_guard<std::mutex> lk(indexMutex_);
        if (index_.empty()) return { 0, 0.0 };
        for (auto h : live) {
            auto it = index_.find(h);
            if (it == index_.end()) continue;
            for (auto id : it->second) uniVotes[id]++;
        }
        for (size_t i = 0; i + 1 < live.size(); ++i) {
            const uint32_t combo = live[i] ^ (live[i + 1] * 0x9E3779B9u);
            auto it = index_.find(combo);
            if (it == index_.end()) continue;
            for (auto id : it->second) biVotes[id]++;
        }
    }

    int64_t bestId = 0; double bestScore = 0.0;
    for (const auto& kv : uniVotes) {
        int hashesInTrack = 0;
        {
            std::lock_guard<std::mutex> lk(indexMutex_);
            auto itc = indexTrackHashCount_.find(kv.first);
            hashesInTrack = (itc != indexTrackHashCount_.end()) ? itc->second : 0;
        }
        if (hashesInTrack <= 0) continue;
        const double denom = static_cast<double>(
            std::min<int>(hashesInTrack, static_cast<int>(live.size())));
        double score = (double) kv.second / denom;
        const int bi = biVotes[kv.first];
        score += 0.4 * static_cast<double>(bi) / std::max<double>(1.0, (double) live.size() - 1.0);
        if (score > bestScore) { bestScore = score; bestId = kv.first; }
    }
    return { bestId, std::min(1.0, bestScore) };
}

} // namespace BeatMate::Services::Audio
