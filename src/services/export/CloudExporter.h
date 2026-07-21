#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace BeatMate::Services::Export {
class CloudExporter {
public:
    CloudExporter() = default;
    bool uploadToS3(const std::string& filePath, const std::string& bucket, const std::string& key);
    void setCredentials(const std::string& accessKey, const std::string& secretKey, const std::string& region = "us-east-1");
    void setProgressCallback(std::function<void(int64_t uploaded, int64_t total)> cb) { progressCb_ = std::move(cb); }
private:
    std::string accessKey_, secretKey_, region_ = "us-east-1";
    std::function<void(int64_t, int64_t)> progressCb_;
};
} // namespace BeatMate::Services::Export
