#include "CloudExporter.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <filesystem>

namespace BeatMate::Services::Export {

void CloudExporter::setCredentials(const std::string& accessKey, const std::string& secretKey, const std::string& region) {
    accessKey_ = accessKey;
    secretKey_ = secretKey;
    region_ = region;
}

bool CloudExporter::uploadToS3(const std::string& filePath, const std::string& bucket, const std::string& key) {
    if (!std::filesystem::exists(filePath)) {
        spdlog::error("CloudExporter: File not found: {}", filePath);
        return false;
    }

    juce::File file{juce::String(filePath)};
    if (!file.existsAsFile()) return false;

    juce::MemoryBlock fileData;
    if (!file.loadFileAsData(fileData)) {
        spdlog::error("CloudExporter: Cannot read file: {}", filePath);
        return false;
    }

    std::string urlStr = "https://" + bucket + ".s3." + region_ + ".amazonaws.com/" + key;
    juce::URL url{juce::String(urlStr)};
    url = url.withPOSTData(fileData);

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders("Content-Type: application/octet-stream")
                       .withHttpRequestCmd("PUT")
                       .withConnectionTimeoutMs(30000);

    auto stream = url.createInputStream(options);
    bool ok = (stream != nullptr);

    if (ok) {
        spdlog::info("CloudExporter: Uploaded to s3://{}/{}", bucket, key);
        if (progressCb_) {
            progressCb_(static_cast<int64_t>(fileData.getSize()), static_cast<int64_t>(fileData.getSize()));
        }
    } else {
        spdlog::error("CloudExporter: Upload failed to s3://{}/{}", bucket, key);
    }

    return ok;
}

} // namespace BeatMate::Services::Export
