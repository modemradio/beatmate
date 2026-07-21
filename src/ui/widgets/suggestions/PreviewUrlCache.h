#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace BeatMate::UI::Widgets {

// Memoizes "title|artist" -> iTunes Search previewUrl resolution.
class PreviewUrlCache {
public:
    static constexpr int kCapacity = 500;

    static PreviewUrlCache& getInstance();

    // Resolve the iTunes preview URL for a (title, artist) pair. Empty
    void getAsync(std::string title,
                  std::string artist,
                  std::function<void(std::string)> cb);

    // Synchronous cache peek (no network). Returns std::nullopt-style empty
    bool tryGet(const std::string& title,
                const std::string& artist,
                std::string& outUrl) const;

    // Manual put for callers that already have the URL (e.g. Apple RSS sometimes
    void put(const std::string& title,
             const std::string& artist,
             const std::string& url);

    int cachedCount() const;
    void clear();

private:
    PreviewUrlCache() = default;
    PreviewUrlCache(const PreviewUrlCache&) = delete;
    PreviewUrlCache& operator=(const PreviewUrlCache&) = delete;

    static std::string makeKey(const std::string& title,
                               const std::string& artist);

    bool lookupLocked(const std::string& key, std::string& out);
    void insertLocked(const std::string& key, std::string url);

    mutable std::mutex mu_;
    std::list<std::pair<std::string, std::string>> lru_;
    std::unordered_map<std::string, decltype(lru_)::iterator> index_;
    std::unordered_map<std::string,
                       std::vector<std::function<void(std::string)>>> pending_;
};

} // namespace BeatMate::UI::Widgets
