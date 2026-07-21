#include "PreviewUrlCache.h"

#include <algorithm>
#include <cctype>
#include <thread>
#include <utility>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BeatMate::UI::Widgets {

PreviewUrlCache& PreviewUrlCache::getInstance() {
    static PreviewUrlCache inst;
    return inst;
}

int PreviewUrlCache::cachedCount() const {
    std::lock_guard<std::mutex> g(mu_);
    return static_cast<int>(lru_.size());
}

void PreviewUrlCache::clear() {
    std::lock_guard<std::mutex> g(mu_);
    lru_.clear();
    index_.clear();
    pending_.clear();
}

std::string PreviewUrlCache::makeKey(const std::string& title,
                                     const std::string& artist) {
    std::string k;
    k.reserve(title.size() + artist.size() + 1);
    auto append = [&](const std::string& s) {
        for (char c : s) k.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(c))));
    };
    append(title);
    k.push_back('|');
    append(artist);
    return k;
}

bool PreviewUrlCache::lookupLocked(const std::string& key, std::string& out) {
    auto it = index_.find(key);
    if (it == index_.end()) return false;
    lru_.splice(lru_.begin(), lru_, it->second);
    it->second = lru_.begin();
    out = it->second->second;
    return true;
}

void PreviewUrlCache::insertLocked(const std::string& key, std::string url) {
    auto it = index_.find(key);
    if (it != index_.end()) {
        it->second->second = std::move(url);
        lru_.splice(lru_.begin(), lru_, it->second);
        it->second = lru_.begin();
        return;
    }
    lru_.emplace_front(key, std::move(url));
    index_[key] = lru_.begin();
    while ((int)lru_.size() > kCapacity) {
        index_.erase(lru_.back().first);
        lru_.pop_back();
    }
}

bool PreviewUrlCache::tryGet(const std::string& title,
                             const std::string& artist,
                             std::string& outUrl) const {
    auto key = makeKey(title, artist);
    std::lock_guard<std::mutex> g(mu_);
    auto it = index_.find(key);
    if (it == index_.end()) return false;
    outUrl = it->second->second;
    return true;
}

void PreviewUrlCache::put(const std::string& title,
                          const std::string& artist,
                          const std::string& url) {
    auto key = makeKey(title, artist);
    std::lock_guard<std::mutex> g(mu_);
    insertLocked(key, url);
}

void PreviewUrlCache::getAsync(std::string title,
                               std::string artist,
                               std::function<void(std::string)> cb) {
    if (!cb) return;
    auto key = makeKey(title, artist);

    {
        std::lock_guard<std::mutex> g(mu_);
        std::string hit;
        if (lookupLocked(key, hit)) {
            cb(std::move(hit));
            return;
        }
        auto pit = pending_.find(key);
        if (pit != pending_.end()) {
            pit->second.push_back(std::move(cb));
            return;
        }
        pending_[key] = { std::move(cb) };
    }

    std::thread([key, title, artist]() {
        std::string url;
        try {
            const juce::String term = juce::String(artist) + " " + juce::String(title);
            const juce::String encoded = juce::URL::addEscapeChars(term, false);
            const juce::String full =
                "https://itunes.apple.com/search?term=" + encoded
                + "&limit=1&media=music";

            juce::URL u(full);
            int statusCode = 0;
            auto stream = u.createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(4000)
                    .withExtraHeaders("User-Agent: BeatMate/11.0 (PreviewUrlCache)\r\n")
                    .withStatusCode(&statusCode));
            if (stream) {
                auto body = stream->readEntireStreamAsString();
                if (body.isNotEmpty()
                    && (statusCode == 0 || (statusCode >= 200 && statusCode < 300))) {
                    auto j = nlohmann::json::parse(body.toStdString(),
                                                   nullptr, false);
                    if (!j.is_discarded()
                        && j.contains("results") && !j["results"].empty()) {
                        url = j["results"][0].value("previewUrl", std::string{});
                    }
                }
            }
        } catch (const std::exception& ex) {
            spdlog::warn("[PreviewUrlCache] lookup failed for '{}' / '{}': {}",
                         title, artist, ex.what());
        }

        std::vector<std::function<void(std::string)>> waiters;
        {
            auto& self = PreviewUrlCache::getInstance();
            std::lock_guard<std::mutex> g(self.mu_);
            self.insertLocked(key, url);
            auto it = self.pending_.find(key);
            if (it != self.pending_.end()) {
                waiters = std::move(it->second);
                self.pending_.erase(it);
            }
        }

        juce::MessageManager::callAsync(
            [waiters = std::move(waiters), url]() mutable {
                for (auto& w : waiters) if (w) w(url);
            });
    }).detach();
}

}
