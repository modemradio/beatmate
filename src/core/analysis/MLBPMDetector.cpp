#include "MLBPMDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

#if __has_include(<onnxruntime_cxx_api.h>)
#define HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace BeatMate::Core {

struct MLBPMDetector::OnnxSession {
#ifdef HAS_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "BeatMateBPM"};
    std::unique_ptr<Ort::Session> session;
#endif
};

MLBPMDetector::MLBPMDetector()
    : session_(std::make_unique<OnnxSession>()),
      fallbackDetector_(std::make_unique<BPMDetector>()) {
}

MLBPMDetector::~MLBPMDetector() = default;

bool MLBPMDetector::loadModel(const std::string& modelPath) {
#ifdef HAS_ONNXRUNTIME
    try {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(4);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        std::wstring wModelPath(modelPath.begin(), modelPath.end());
        session_->session = std::make_unique<Ort::Session>(
            session_->env, wModelPath.c_str(), sessionOptions);
#else
        session_->session = std::make_unique<Ort::Session>(
            session_->env, modelPath.c_str(), sessionOptions);
#endif
        modelLoaded_ = true;
        modelPath_ = modelPath;
        spdlog::info("MLBPMDetector: loaded model from {}", modelPath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("MLBPMDetector: failed to load model: {}", e.what());
        modelLoaded_ = false;
        return false;
    }
#else
    spdlog::warn("MLBPMDetector: ONNX Runtime not available, using fallback");
    modelLoaded_ = false;
    return false;
#endif
}

std::vector<float> MLBPMDetector::extractFeatures(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 2048;
    int hopSize = 512;
    int numMels = 128;
    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    numFrames = std::min(numFrames, static_cast<int>(30.0 * sr / hopSize));

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    std::vector<float> features;
    features.reserve(numFrames * numMels);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        for (int mel = 0; mel < numMels; ++mel) {
            float melFreq = 700.0f * (std::pow(10.0f, (mel * 2595.0f / numMels / 2595.0f)) - 1.0f);
            int bin = static_cast<int>(melFreq * fftSize / sr);
            bin = std::clamp(bin, 0, static_cast<int>(mag.size()) - 1);

            float val = mag[bin];
            features.push_back((val > 1e-10f) ? std::log10(val) : -10.0f);
        }
    }

    return features;
}

BPMResult MLBPMDetector::detect(const AudioTrack& track) {
#ifdef HAS_ONNXRUNTIME
    if (modelLoaded_ && session_->session) {
        try {
            auto features = extractFeatures(track);

            Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
                OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

            int numMels = 128;
            int numFrames = static_cast<int>(features.size()) / numMels;
            std::array<int64_t, 3> inputShape = {1, numFrames, numMels};

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memInfo, features.data(), features.size(),
                inputShape.data(), inputShape.size());

            const char* inputNames[] = {"input"};
            const char* outputNames[] = {"output"};

            auto outputTensors = session_->session->Run(
                Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

            float* outputData = outputTensors[0].GetTensorMutableData<float>();

            BPMResult result;
            result.bpm = static_cast<double>(outputData[0]);
            result.confidence = static_cast<double>(outputData[1]);

            auto fallbackResult = fallbackDetector_->detect(track);
            result.beats = fallbackResult.beats;
            result.offset = fallbackResult.offset;

            spdlog::info("MLBPMDetector: detected {:.1f} BPM (confidence: {:.0f}%)",
                         result.bpm, result.confidence * 100);
            return result;
        } catch (const std::exception& e) {
            spdlog::error("MLBPMDetector inference failed: {}, using fallback", e.what());
        }
    }
#endif

    spdlog::info("MLBPMDetector: using fallback detector");
    return fallbackDetector_->detect(track);
}

} // namespace BeatMate::Core
