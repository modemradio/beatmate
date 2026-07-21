#include "ClapEncoder.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <onnxruntime_cxx_api.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace BeatMate::Services::AI {

namespace {

constexpr int kSampleRate = 48000;
constexpr int kMaxSamples = kSampleRate * 10;
constexpr int kFftSize = 1024;
constexpr int kHop = 480;
constexpr int kMelBins = 64;
constexpr int kSpecBins = kFftSize / 2 + 1;

#ifdef _WIN32
std::wstring utf8ToWide(const juce::String& s) {
    const auto utf8 = s.toStdString();
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int) utf8.size(), nullptr, 0);
    std::wstring w((size_t) len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int) utf8.size(), w.data(), len);
    return w;
}
#endif

std::vector<float> loadNpyF32(const juce::File& f, int expectedRows, int expectedCols) {
    juce::FileInputStream is(f);
    if (is.failedToOpen()) return {};
    char magic[8] = {};
    if (is.read(magic, 8) != 8 || std::memcmp(magic, "\x93NUMPY", 6) != 0) return {};
    const int headerLen = (uint8_t) is.readByte() | ((uint8_t) is.readByte() << 8);
    std::vector<char> header((size_t) headerLen + 1, 0);
    if (is.read(header.data(), headerLen) != headerLen) return {};
    const juce::String h(header.data());
    if (! h.contains("'<f4'") || h.contains("True")) return {};
    const juce::String want = "(" + juce::String(expectedRows) + ", " + juce::String(expectedCols) + ")";
    if (! h.contains(want)) return {};
    const size_t count = (size_t) expectedRows * (size_t) expectedCols;
    std::vector<float> data(count);
    if (is.read(data.data(), (int) (count * sizeof(float))) != (int) (count * sizeof(float))) return {};
    return data;
}

} // namespace

struct ClapEncoder::Impl {
    Ort::Env env { ORT_LOGGING_LEVEL_ERROR, "BeatMateClap" };
    std::unique_ptr<Ort::Session> session;
    std::string inputName, outputName;
    std::vector<float> melFilters;
    juce::AudioFormatManager formatManager;
    std::vector<std::pair<std::string, std::vector<float>>> labels;
    std::vector<std::pair<std::string, std::vector<float>>> moodLabels;

    Impl() { formatManager.registerBasicFormats(); }
};

static bool loadClapLabelFile(const juce::File& f, int dim,
                              std::vector<std::pair<std::string, std::vector<float>>>& out) {
    juce::FileInputStream is(f);
    if (! f.existsAsFile() || is.failedToOpen()) return false;
    const int count = is.readInt();
    if (count <= 0 || count > 512) return false;
    std::vector<std::pair<std::string, std::vector<float>>> loaded;
    for (int i = 0; i < count; ++i) {
        const int nameLen = is.readInt();
        if (nameLen <= 0 || nameLen > 256) return false;
        std::vector<char> name((size_t) nameLen);
        if (is.read(name.data(), nameLen) != nameLen) return false;
        std::vector<float> vec((size_t) dim, 0.0f);
        if (is.read(vec.data(), dim * (int) sizeof(float)) != dim * (int) sizeof(float)) return false;
        loaded.emplace_back(std::string(name.begin(), name.end()), std::move(vec));
    }
    out = std::move(loaded);
    return true;
}

ClapEncoder::ClapEncoder() = default;
ClapEncoder::ClapEncoder(const juce::File& modelDir) : modelDir_(modelDir) {}
ClapEncoder::~ClapEncoder() = default;

static juce::File& modelDirOverride() {
    static juce::File dir;
    return dir;
}

juce::File ClapEncoder::defaultModelDirectory() {
    if (modelDirOverride().isDirectory()) return modelDirOverride();
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getChildFile("resources").getChildFile("models").getChildFile("clap");
}

juce::File ClapEncoder::defaultTagModelDirectory() {
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getChildFile("resources").getChildFile("models").getChildFile("clap_tags");
}

void ClapEncoder::setModelDirectoryOverride(const juce::File& dir) {
    modelDirOverride() = dir;
}

juce::File ClapEncoder::modelDirectory() const {
    return modelDir_.isDirectory() ? modelDir_ : defaultModelDirectory();
}

bool ClapEncoder::isAvailable() const {
    const auto dir = modelDirectory();
    return dir.getChildFile("audio_model.onnx").existsAsFile()
        && dir.getChildFile("mel_slaney.npy").existsAsFile();
}

bool ClapEncoder::ensureLoaded() {
    std::lock_guard<std::mutex> lock(loadMutex_);
    if (impl_ && impl_->session) return true;
    if (loadFailed_) return false;
    if (! isAvailable()) { loadFailed_ = true; return false; }

    auto impl = std::make_unique<Impl>();
    impl->melFilters = loadNpyF32(modelDirectory().getChildFile("mel_slaney.npy"), kSpecBins, kMelBins);
    if (impl->melFilters.empty()) {
        spdlog::error("[ClapEncoder] mel_slaney.npy invalide");
        loadFailed_ = true;
        return false;
    }

    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        const auto modelPath = modelDirectory().getChildFile("audio_model.onnx").getFullPathName();
#ifdef _WIN32
        impl->session = std::make_unique<Ort::Session>(impl->env, utf8ToWide(modelPath).c_str(), options);
#else
        impl->session = std::make_unique<Ort::Session>(impl->env, modelPath.toStdString().c_str(), options);
#endif
        Ort::AllocatorWithDefaultOptions alloc;
        impl->inputName = impl->session->GetInputNameAllocated(0, alloc).get();
        impl->outputName = impl->session->GetOutputNameAllocated(0, alloc).get();
    } catch (const Ort::Exception& e) {
        spdlog::error("[ClapEncoder] chargement du modele impossible: {}", e.what());
        loadFailed_ = true;
        return false;
    }

    impl_ = std::move(impl);
    spdlog::info("[ClapEncoder] modele charge ({})", kModelVersion);
    return true;
}

std::vector<float> ClapEncoder::monoWindow48k(juce::AudioFormatReader& reader, double centerFrac) const {
    const int channels = (int) reader.numChannels;
    const double srIn = reader.sampleRate;
    const juce::int64 total = reader.lengthInSamples;
    if (channels <= 0 || srIn <= 0 || total <= 0) return {};

    auto readMono = [&](juce::int64 start, juce::int64 count) -> std::vector<float> {
        if (count <= 0) return {};
        juce::AudioBuffer<float> buf(channels, (int) count);
        if (! reader.read(buf.getArrayOfWritePointers(), channels, start, (int) count)) return {};
        std::vector<float> mono((size_t) count, 0.0f);
        for (int c = 0; c < channels; ++c) {
            const float* src = buf.getReadPointer(c);
            for (juce::int64 s = 0; s < count; ++s) mono[(size_t) s] += src[s];
        }
        const float inv = 1.0f / (float) channels;
        for (auto& v : mono) v *= inv;
        return mono;
    };

    auto windowStart = [&](juce::int64 nOut) -> juce::int64 {
        juce::int64 start = (juce::int64) std::llround(centerFrac * (double) nOut) - kMaxSamples / 2;
        return juce::jlimit<juce::int64>(0, nOut - kMaxSamples, start);
    };

    std::vector<float> out;

    if ((int) std::llround(srIn) == kSampleRate) {
        if (total >= kMaxSamples) {
            return readMono(windowStart(total), kMaxSamples);
        }
        out = readMono(0, total);
    } else {
        const juce::int64 nOut = (juce::int64) std::llround((double) total * kSampleRate / srIn);
        if (nOut <= 0) return {};
        const double step = (double) total / (double) nOut;

        auto interpRange = [&](juce::int64 firstOut, juce::int64 countOut) -> std::vector<float> {
            const double x0 = (double) firstOut * step;
            const double x1 = (double) (firstOut + countOut - 1) * step;
            juce::int64 s0 = (juce::int64) std::floor(x0);
            juce::int64 s1 = std::min<juce::int64>((juce::int64) std::floor(x1) + 2, total);
            s0 = std::max<juce::int64>(0, s0);
            const auto src = readMono(s0, s1 - s0);
            if (src.empty()) return {};
            std::vector<float> res((size_t) countOut, 0.0f);
            for (juce::int64 j = 0; j < countOut; ++j) {
                const double x = (double) (firstOut + j) * step;
                juce::int64 i = (juce::int64) std::floor(x);
                if (i >= total - 1) {
                    res[(size_t) j] = src[src.size() - 1];
                    continue;
                }
                const double frac = x - (double) i;
                const size_t idx = (size_t) (i - s0);
                res[(size_t) j] = (float) ((double) src[idx] + frac * ((double) src[idx + 1] - (double) src[idx]));
            }
            return res;
        };

        if (nOut >= kMaxSamples)
            return interpRange(windowStart(nOut), kMaxSamples);
        out = interpRange(0, nOut);
    }

    if (out.empty()) return {};
    std::vector<float> tiled((size_t) kMaxSamples);
    for (size_t j = 0; j < (size_t) kMaxSamples; ++j) tiled[j] = out[j % out.size()];
    return tiled;
}

std::vector<float> ClapEncoder::logMelFeatures(const std::vector<float>& audio) const {
    const int pad = kFftSize / 2;
    const int n = (int) audio.size();
    std::vector<double> wav((size_t) (n + 2 * pad));
    for (int i = 0; i < pad; ++i) wav[(size_t) i] = audio[(size_t) (pad - i)];
    for (int i = 0; i < n; ++i) wav[(size_t) (pad + i)] = audio[(size_t) i];
    for (int k = 0; k < pad; ++k) wav[(size_t) (pad + n + k)] = audio[(size_t) (n - 2 - k)];

    std::vector<double> window((size_t) kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / kFftSize);

    juce::dsp::FFT fft(10);
    const int nf = 1 + ((int) wav.size() - kFftSize) / kHop;
    std::vector<float> fftBuf((size_t) kFftSize * 2);
    std::vector<double> spec((size_t) kSpecBins * (size_t) nf);

    for (int t = 0; t < nf; ++t) {
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            fftBuf[(size_t) i] = (float) (wav[(size_t) (t * kHop + i)] * window[(size_t) i]);
        fft.performRealOnlyForwardTransform(fftBuf.data(), true);
        for (int k = 0; k < kSpecBins; ++k) {
            const double re = fftBuf[(size_t) (2 * k)];
            const double im = fftBuf[(size_t) (2 * k + 1)];
            spec[(size_t) k * (size_t) nf + (size_t) t] = re * re + im * im;
        }
    }

    const auto& fb = impl_->melFilters;
    std::vector<float> feats((size_t) nf * kMelBins);
    for (int t = 0; t < nf; ++t) {
        for (int m = 0; m < kMelBins; ++m) {
            double acc = 0.0;
            for (int k = 0; k < kSpecBins; ++k)
                acc += (double) fb[(size_t) k * kMelBins + (size_t) m] * spec[(size_t) k * (size_t) nf + (size_t) t];
            acc = std::max(1e-10, acc);
            feats[(size_t) t * kMelBins + (size_t) m] = (float) (10.0 * std::log10(acc));
        }
    }
    return feats;
}

std::vector<float> ClapEncoder::runModel(const std::vector<float>& mono) {
    auto feats = logMelFeatures(mono);
    const int nf = (int) (feats.size() / kMelBins);

    try {
        const std::array<int64_t, 4> shape { 1, 1, nf, kMelBins };
        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(
            memInfo, feats.data(), feats.size(), shape.data(), shape.size());

        const char* inNames[] = { impl_->inputName.c_str() };
        const char* outNames[] = { impl_->outputName.c_str() };
        auto outputs = impl_->session->Run(Ort::RunOptions { nullptr }, inNames, &input, 1, outNames, 1);
        if (outputs.empty() || ! outputs.front().IsTensor()) return {};

        const size_t cnt = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
        if (cnt != (size_t) kDim) return {};
        const float* data = outputs.front().GetTensorData<float>();
        std::vector<float> emb(data, data + cnt);

        double norm = 0.0;
        for (float v : emb) norm += (double) v * (double) v;
        norm = std::sqrt(norm);
        if (norm > 0.0)
            for (auto& v : emb) v = (float) ((double) v / norm);
        return emb;
    } catch (const Ort::Exception& e) {
        spdlog::error("[ClapEncoder] inference echouee: {}", e.what());
        return {};
    }
}

std::vector<float> ClapEncoder::encodeFile(const juce::File& audioFile) {
    if (! audioFile.existsAsFile() || ! ensureLoaded()) return {};

    std::unique_ptr<juce::AudioFormatReader> reader(impl_->formatManager.createReaderFor(audioFile));
    if (! reader) {
        spdlog::warn("[ClapEncoder] decodage impossible: {}", audioFile.getFullPathName().toStdString());
        return {};
    }

    const double dur48k = reader->sampleRate > 0
        ? (double) reader->lengthInSamples / reader->sampleRate * kSampleRate : 0.0;
    std::vector<double> fracs;
    if (dur48k > (double) kMaxSamples * 1.5) fracs = { 0.25, 0.5, 0.75 };
    else                                     fracs = { 0.5 };

    std::vector<double> acc((size_t) kDim, 0.0);
    int used = 0;
    for (double f : fracs) {
        auto mono = monoWindow48k(*reader, f);
        if (mono.empty()) continue;
        auto emb = runModel(mono);
        if ((int) emb.size() != kDim) continue;
        for (int i = 0; i < kDim; ++i) acc[(size_t) i] += (double) emb[(size_t) i];
        ++used;
    }
    if (used == 0) {
        spdlog::warn("[ClapEncoder] aucune fenetre encodee: {}", audioFile.getFullPathName().toStdString());
        return {};
    }

    double norm = 0.0;
    for (double v : acc) norm += v * v;
    norm = std::sqrt(norm);
    if (norm <= 0.0) return {};
    std::vector<float> out((size_t) kDim, 0.0f);
    for (int i = 0; i < kDim; ++i) out[(size_t) i] = (float) (acc[(size_t) i] / norm);
    return out;
}

bool ClapEncoder::zeroShotAvailable() const {
    return modelDirectory().getChildFile("genre_labels.bin").existsAsFile();
}

bool ClapEncoder::moodZeroShotAvailable() const {
    return modelDirectory().getChildFile("mood_labels.bin").existsAsFile();
}

bool ClapEncoder::ensureLabelsLoaded() {
    if (! ensureLoaded()) return false;
    std::lock_guard<std::mutex> lock(loadMutex_);
    if (! impl_->labels.empty()) return true;
    if (labelsFailed_) return false;
    if (! loadClapLabelFile(modelDirectory().getChildFile("genre_labels.bin"), kDim, impl_->labels)) {
        labelsFailed_ = true;
        return false;
    }
    spdlog::info("[ClapEncoder] {} labels genre zero-shot charges", impl_->labels.size());
    return true;
}

bool ClapEncoder::ensureMoodLabelsLoaded() {
    if (! ensureLoaded()) return false;
    std::lock_guard<std::mutex> lock(loadMutex_);
    if (! impl_->moodLabels.empty()) return true;
    if (moodLabelsFailed_) return false;
    if (! loadClapLabelFile(modelDirectory().getChildFile("mood_labels.bin"), kDim, impl_->moodLabels)) {
        moodLabelsFailed_ = true;
        return false;
    }
    spdlog::info("[ClapEncoder] {} labels mood zero-shot charges", impl_->moodLabels.size());
    return true;
}

std::vector<ClapEncoder::LabelScore> ClapEncoder::topLabelsFrom(
    const std::vector<std::pair<std::string, std::vector<float>>>& labels,
    const std::vector<float>& audioVec, int count) const {
    std::vector<LabelScore> out;
    for (const auto& [label, vec] : labels)
        out.push_back({ label, cosine(audioVec, vec) });
    std::sort(out.begin(), out.end(), [](const LabelScore& a, const LabelScore& b) { return a.score > b.score; });
    if ((int) out.size() > count) out.resize((size_t) count);
    return out;
}

std::vector<ClapEncoder::LabelScore> ClapEncoder::topGenreLabels(const std::vector<float>& audioVec, int count) {
    if ((int) audioVec.size() != kDim || ! ensureLabelsLoaded()) return {};
    return topLabelsFrom(impl_->labels, audioVec, count);
}

ClapEncoder::LabelScore ClapEncoder::bestGenreLabel(const std::vector<float>& audioVec) {
    auto top = topGenreLabels(audioVec, 1);
    return top.empty() ? LabelScore{} : top.front();
}

std::vector<ClapEncoder::LabelScore> ClapEncoder::topMoodLabels(const std::vector<float>& audioVec, int count) {
    if ((int) audioVec.size() != kDim || ! ensureMoodLabelsLoaded()) return {};
    return topLabelsFrom(impl_->moodLabels, audioVec, count);
}

ClapEncoder::LabelScore ClapEncoder::bestMoodLabel(const std::vector<float>& audioVec) {
    auto top = topMoodLabels(audioVec, 1);
    return top.empty() ? LabelScore{} : top.front();
}

float ClapEncoder::cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0.0f;
    double dot = 0.0;
    for (size_t i = 0; i < a.size(); ++i) dot += (double) a[i] * (double) b[i];
    return (float) juce::jlimit(-1.0, 1.0, dot);
}

std::vector<float> ClapEncoder::readVecFile(const juce::File& vecFile) {
    if (! vecFile.existsAsFile()) return {};
    juce::FileInputStream is(vecFile);
    if (is.failedToOpen()) return {};
    const int dim = is.readInt();
    if (dim != kDim) return {};
    std::vector<float> v((size_t) dim, 0.0f);
    for (int i = 0; i < dim; ++i) v[(size_t) i] = is.readFloat();
    return v;
}

} // namespace BeatMate::Services::AI
