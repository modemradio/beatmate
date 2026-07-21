#include "EventBusService.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Config {

EventBusService& EventBusService::getInstance() {
    static EventBusService instance;
    return instance;
}

EventBusService::SubscriptionId EventBusService::subscribe(const std::string& eventName, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    SubscriptionId id = nextId_++;
    subscriptions_[eventName].push_back({id, eventName, std::move(callback), false});
    return id;
}

void EventBusService::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, subs] : subscriptions_) {
        subs.erase(std::remove_if(subs.begin(), subs.end(),
            [id](const Subscription& s) { return s.id == id; }), subs.end());
    }
}

void EventBusService::unsubscribeAll(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.erase(eventName);
}

void EventBusService::publish(const std::string& eventName, const std::any& data) {
    std::vector<Subscription> toCall;
    std::vector<SubscriptionId> toRemove;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(eventName);
        if (it == subscriptions_.end()) return;
        toCall = it->second;
    }

    for (auto& sub : toCall) {
        try {
            sub.callback(data);
        } catch (const std::exception& e) {
            spdlog::error("EventBusService: exception in handler for '{}': {}", eventName, e.what());
        }
        if (sub.oneShot) toRemove.push_back(sub.id);
    }

    if (!toRemove.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(eventName);
        if (it != subscriptions_.end()) {
            auto& subs = it->second;
            subs.erase(std::remove_if(subs.begin(), subs.end(),
                [&](const Subscription& s) {
                    return std::find(toRemove.begin(), toRemove.end(), s.id) != toRemove.end();
                }), subs.end());
        }
    }
}

EventBusService::SubscriptionId EventBusService::once(const std::string& eventName, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    SubscriptionId id = nextId_++;
    subscriptions_[eventName].push_back({id, eventName, std::move(callback), true});
    return id;
}

bool EventBusService::hasSubscribers(const std::string& eventName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(eventName);
    return it != subscriptions_.end() && !it->second.empty();
}

int EventBusService::getSubscriberCount(const std::string& eventName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(eventName);
    return it != subscriptions_.end() ? static_cast<int>(it->second.size()) : 0;
}

std::vector<std::string> EventBusService::getActiveEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> events;
    for (auto& [name, subs] : subscriptions_) {
        if (!subs.empty()) events.push_back(name);
    }
    return events;
}

void EventBusService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.clear();
}

} // namespace BeatMate::Services::Config
