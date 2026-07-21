#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

class StreamingManager {
public:
    StreamingManager();
    ~StreamingManager() = default;

    void registerService(std::shared_ptr<StreamingServiceBase> service);
    std::vector<std::shared_ptr<StreamingServiceBase>> getConnectedServices();
    std::shared_ptr<StreamingServiceBase> getService(Models::StreamingServiceType type);

    // Unified search across all connected services
    std::map<Models::StreamingServiceType, StreamingSearchResult> search(const std::string& query, int limit = 20);

private:
    std::map<Models::StreamingServiceType, std::shared_ptr<StreamingServiceBase>> services_;
};

} // namespace BeatMate::Services::Streaming
