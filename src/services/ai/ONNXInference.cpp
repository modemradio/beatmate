#include "ONNXInference.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <numeric>
#include <onnxruntime_cxx_api.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::AI {

namespace {
#ifdef _WIN32
    std::wstring utf8ToWide(const std::string& s) {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
        std::wstring w((size_t) len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), len);
        return w;
    }
#endif
}

struct ONNXInference::Impl {
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "BeatMate" };
    Ort::AllocatorWithDefaultOptions allocator;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<std::vector<int64_t>> inputShapes;
};

ONNXInference::ONNXInference() = default;
ONNXInference::~ONNXInference() = default;

bool ONNXInference::loadModel(const std::string& modelPath) {
    if (!fs::exists(modelPath)) {
        spdlog::error("ONNXInference: Model not found: {}", modelPath);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // Env ONNX Runtime créé paresseusement, seulement quand un modèle est chargé
    if (!impl_) impl_ = std::make_unique<Impl>();
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (useGPU_ && isGPUAvailable()) {
            OrtCUDAProviderOptions cuda {};
            options.AppendExecutionProvider_CUDA(cuda);
        }

#ifdef _WIN32
        impl_->session = std::make_unique<Ort::Session>(impl_->env, utf8ToWide(modelPath).c_str(), options);
#else
        impl_->session = std::make_unique<Ort::Session>(impl_->env, modelPath.c_str(), options);
#endif

        impl_->inputNames.clear();
        impl_->outputNames.clear();
        impl_->inputShapes.clear();

        const size_t nIn = impl_->session->GetInputCount();
        for (size_t i = 0; i < nIn; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, impl_->allocator);
            impl_->inputNames.emplace_back(name.get());
            auto info = impl_->session->GetInputTypeInfo(i);
            impl_->inputShapes.push_back(info.GetTensorTypeAndShapeInfo().GetShape());
        }
        const size_t nOut = impl_->session->GetOutputCount();
        for (size_t i = 0; i < nOut; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, impl_->allocator);
            impl_->outputNames.emplace_back(name.get());
        }

        modelPath_ = modelPath;
        loaded_ = true;
        spdlog::info("ONNXInference: Model loaded from {} (inputs:{}, outputs:{}, GPU:{})",
                     modelPath, nIn, nOut, useGPU_ && isGPUAvailable());
        return true;
    } catch (const Ort::Exception& e) {
        spdlog::error("ONNXInference: Failed to load model {}: {}", modelPath, e.what());
        impl_->session.reset();
        loaded_ = false;
        return false;
    }
}

std::vector<float> ONNXInference::run(const std::vector<float>& input) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_ || !impl_ || !impl_->session) {
        spdlog::error("ONNXInference: No model loaded");
        return {};
    }

    try {
        std::vector<int64_t> shape;
        if (!impl_->inputShapes.empty()) shape = impl_->inputShapes.front();
        if (shape.empty()) shape = { 1, static_cast<int64_t>(input.size()) };

        int64_t known = 1;
        int dynamicCount = 0;
        for (auto d : shape) { if (d <= 0) ++dynamicCount; else known *= d; }
        if (dynamicCount > 0 && known > 0) {
            int64_t fill = static_cast<int64_t>(input.size()) / known;
            if (fill < 1) fill = 1;
            bool first = true;
            for (auto& d : shape) {
                if (d <= 0) { d = first ? fill : 1; first = false; }
            }
        }

        int64_t total = 1;
        for (auto d : shape) total *= d;
        if (total <= 0) total = static_cast<int64_t>(input.size());

        std::vector<float> buf = input;
        if (static_cast<int64_t>(buf.size()) != total)
            buf.resize(static_cast<size_t>(total), 0.0f);

        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, buf.data(), buf.size(), shape.data(), shape.size());

        std::vector<const char*> inNames, outNames;
        inNames.reserve(impl_->inputNames.size());
        outNames.reserve(impl_->outputNames.size());
        for (auto& n : impl_->inputNames) inNames.push_back(n.c_str());
        for (auto& n : impl_->outputNames) outNames.push_back(n.c_str());

        auto outputs = impl_->session->Run(Ort::RunOptions { nullptr },
                                           inNames.data(), &inputTensor, 1,
                                           outNames.data(), outNames.size());

        std::vector<float> result;
        if (!outputs.empty() && outputs.front().IsTensor()) {
            auto& out = outputs.front();
            const size_t cnt = out.GetTensorTypeAndShapeInfo().GetElementCount();
            const float* data = out.GetTensorData<float>();
            result.assign(data, data + cnt);
        }
        spdlog::debug("ONNXInference: ran inference, in:{} out:{}", input.size(), result.size());
        return result;
    } catch (const Ort::Exception& e) {
        spdlog::error("ONNXInference: Inference failed: {}", e.what());
        return {};
    }
}

bool ONNXInference::isGPUAvailable() const {
    const auto providers = Ort::GetAvailableProviders();
    for (const auto& p : providers)
        if (p == "CUDAExecutionProvider" || p == "DmlExecutionProvider" || p == "TensorrtExecutionProvider")
            return true;
    return false;
}

} // namespace BeatMate::Services::AI
