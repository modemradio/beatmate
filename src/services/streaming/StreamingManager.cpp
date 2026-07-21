#include "StreamingManager.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <future>

namespace BeatMate::Services::Streaming {

StreamingManager::StreamingManager() {
}

void StreamingManager::registerService(std::shared_ptr<StreamingServiceBase> service) {
    services_[service->getServiceType()] = std::move(service);
    spdlog::info("StreamingManager: Registered {}", services_.rbegin()->second->getServiceName());
}

std::vector<std::shared_ptr<StreamingServiceBase>> StreamingManager::getConnectedServices() {
    std::vector<std::shared_ptr<StreamingServiceBase>> connected;
    for (auto& [type, service] : services_) {
        if (service->isAuthenticated()) {
            connected.push_back(service);
        }
    }
    return connected;
}

std::shared_ptr<StreamingServiceBase> StreamingManager::getService(Models::StreamingServiceType type) {
    auto it = services_.find(type);
    return (it != services_.end()) ? it->second : nullptr;
}

std::map<Models::StreamingServiceType, StreamingSearchResult> StreamingManager::search(
    const std::string& query, int limit) {

    std::map<Models::StreamingServiceType, StreamingSearchResult> results;

    std::vector<std::pair<Models::StreamingServiceType,
                          std::future<StreamingSearchResult>>> futures;
    for (auto& [type, service] : services_) {
        if (!service->isAuthenticated()) continue;
        auto svc = service;
        futures.emplace_back(type, std::async(std::launch::async,
            [svc, query, limit]() -> StreamingSearchResult {
                try {
                    return svc->search(query, limit);
                } catch (const std::exception& e) {
                    spdlog::error("StreamingManager: Error searching {}: {}",
                                  svc->getServiceName(), e.what());
                } catch (...) {
                    spdlog::error("StreamingManager: Unknown error searching {}",
                                  svc->getServiceName());
                }
                return StreamingSearchResult{};
            }));
    }

    for (auto& [type, fut] : futures) {
        auto r = fut.get();
        spdlog::debug("StreamingManager: service {} returned {} results",
                      static_cast<int>(type), r.tracks.size());
        results[type] = std::move(r);
    }

    return results;
}

}
