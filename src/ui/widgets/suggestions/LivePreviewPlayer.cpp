#include "LivePreviewPlayer.h"

#include "PreviewUrlCache.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace BeatMate::UI::Widgets {

LivePreviewPlayer::ListenerToken::ListenerToken(int i) : id(i) {}

LivePreviewPlayer::ListenerToken::~ListenerToken() {
    if (id != 0) LivePreviewPlayer::getInstance().removeListener(id);
}

LivePreviewPlayer::ListenerToken::ListenerToken(ListenerToken&& o) noexcept : id(o.id) {
    o.id = 0;
}

LivePreviewPlayer::ListenerToken&
LivePreviewPlayer::ListenerToken::operator=(ListenerToken&& o) noexcept {
    if (this != &o) {
        if (id != 0) LivePreviewPlayer::getInstance().removeListener(id);
        id = o.id;
        o.id = 0;
    }
    return *this;
}

LivePreviewPlayer& LivePreviewPlayer::getInstance() {
    static LivePreviewPlayer inst;
    return inst;
}

LivePreviewPlayer::LivePreviewPlayer() {
    formats_ = std::make_unique<juce::AudioFormatManager>();
    formats_->registerBasicFormats();
    transport_ = std::make_unique<juce::AudioTransportSource>();
    player_    = std::make_unique<juce::AudioSourcePlayer>();
    player_->setSource(transport_.get());
}

LivePreviewPlayer::~LivePreviewPlayer() {
    stop();
    if (device_ && player_) device_->removeAudioCallback(player_.get());
    if (player_)   player_->setSource(nullptr);
    if (transport_) transport_->setSource(nullptr);
}

void LivePreviewPlayer::ensureDevice() {
    if (device_) return;
    device_ = std::make_unique<juce::AudioDeviceManager>();
    device_->initialiseWithDefaultDevices(0, 2);
    if (player_) device_->addAudioCallback(player_.get());
}

LivePreviewPlayer::ListenerToken
LivePreviewPlayer::addListener(std::function<void()> cb) {
    std::lock_guard<std::mutex> g(lock_);
    int id = nextListenerId_++;
    listeners_.push_back({ id, std::move(cb) });
    return ListenerToken(id);
}

void LivePreviewPlayer::removeListener(int id) {
    std::lock_guard<std::mutex> g(lock_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [id](const auto& p) { return p.first == id; }),
        listeners_.end());
}

void LivePreviewPlayer::notifyListeners() {
    std::vector<std::function<void()>> snap;
    {
        std::lock_guard<std::mutex> g(lock_);
        snap.reserve(listeners_.size());
        for (auto& [_, fn] : listeners_) snap.push_back(fn);
    }
    for (auto& fn : snap) if (fn) fn();
}

std::string LivePreviewPlayer::currentRowId() const {
    std::lock_guard<std::mutex> g(lock_);
    return currentRowId_;
}

LivePreviewPlayer::State LivePreviewPlayer::currentState() const {
    return state_.load();
}

void LivePreviewPlayer::stop() {
    // Invalidate any in-flight load so a stale completion can't restart us.
    loadGeneration_.fetch_add(1, std::memory_order_acq_rel);

    {
        std::lock_guard<std::mutex> g(lock_);
        if (transport_) {
            transport_->stop();
            transport_->setSource(nullptr);
        }
        readerSource_.reset();
        currentRowId_.clear();
    }
    state_.store(State::Idle);
    juce::MessageManager::callAsync([this]{ notifyListeners(); });
}

void LivePreviewPlayer::playPreview(const std::string& rowId,
                                    const std::string& artist,
                                    const std::string& title,
                                    const std::string& directUrl) {
    {
        std::lock_guard<std::mutex> g(lock_);
        if (!currentRowId_.empty() && currentRowId_ == rowId
            && state_.load() != State::Idle) {
            // fall through to stop() (releases lock below)
        } else if (state_.load() != State::Idle) {
        }
    }
    if (currentRowId() == rowId && state_.load() != State::Idle) {
        stop();
        return;
    }

    if (state_.load() != State::Idle) stop();

    {
        std::lock_guard<std::mutex> g(lock_);
        currentRowId_ = rowId;
    }
    state_.store(State::Loading);

    ensureDevice();
    juce::MessageManager::callAsync([this]{ notifyListeners(); });

    const uint64_t myGen = loadGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;

    std::string a = artist;
    std::string t = title;
    std::string d = directUrl;
    std::thread([this, myGen, rowId, a, t, d]() {
        const auto url = d.empty() ? lookupPreviewUrl(a, t) : d;
        if (myGen != loadGeneration_.load(std::memory_order_acquire)) return;
        if (url.empty()) {
            spdlog::warn("[LivePreview] no iTunes preview found for row {}", rowId);
            failLoad(rowId);
            return;
        }

        auto reader = createReaderForUrl(url);
        if (myGen != loadGeneration_.load(std::memory_order_acquire)) return;
        if (!reader) {
            failLoad(rowId);
            return;
        }

        auto shared = std::make_shared<std::unique_ptr<juce::AudioFormatReader>>(std::move(reader));
        juce::MessageManager::callAsync([this, myGen, rowId, shared]() {
            if (myGen != loadGeneration_.load(std::memory_order_acquire)) return;
            attachReaderAndPlay(rowId, std::move(*shared));
        });
    }).detach();
}

std::unique_ptr<juce::AudioFormatReader>
LivePreviewPlayer::createReaderForUrl(const std::string& url) {
    {
        const juce::String asString(url);
        if (!asString.startsWithIgnoreCase("http")
            && juce::File::isAbsolutePath(asString)) {
            juce::File localFile(asString);
            if (localFile.existsAsFile()) {
                std::unique_ptr<juce::AudioFormatReader> reader(
                    formats_->createReaderFor(localFile));
                if (reader) return reader;
                spdlog::warn("[LivePreview] no decoder for local file {}", url);
                return nullptr;
            }
        }
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

        juce::URL u { juce::String(url) };
        auto stream = u.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(5000));
        if (!stream) {
            spdlog::warn("[LivePreview] cannot open stream (attempt {}) {}", attempt + 1, url);
            continue;
        }

        std::unique_ptr<juce::AudioFormatReader> reader(
            formats_->createReaderFor(std::move(stream)));
        if (reader) return reader;
        spdlog::warn("[LivePreview] no suitable decoder for {}", url);
        break;
    }
    return nullptr;
}

void LivePreviewPlayer::failLoad(const std::string& rowId) {
    {
        std::lock_guard<std::mutex> g(lock_);
        if (currentRowId_ != rowId) return;
        currentRowId_.clear();
    }
    state_.store(State::Idle);
    juce::MessageManager::callAsync([this]{ notifyListeners(); });
}

void LivePreviewPlayer::attachReaderAndPlay(const std::string& rowId,
                                            std::unique_ptr<juce::AudioFormatReader> reader) {
    if (!reader) {
        failLoad(rowId);
        return;
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(
            reader.release(), true);
        if (transport_) {
            transport_->setSource(readerSource_.get(),
                                  0, nullptr,
                                  readerSource_->getAudioFormatReader()->sampleRate);
            transport_->setPosition(0.0);
            transport_->start();
        }
        currentRowId_ = rowId;
    }
    state_.store(State::Playing);
    notifyListeners();
}

std::string LivePreviewPlayer::lookupPreviewUrl(const std::string& artist,
                                                const std::string& title) {
    // Memoize via the shared PreviewUrlCache so repeated plays of the same
    {
        std::string cached;
        if (PreviewUrlCache::getInstance().tryGet(title, artist, cached))
            return cached;
    }
    // Build: https://itunes.apple.com/search?term=<url-encoded>&limit=1&media=music
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
            .withExtraHeaders("User-Agent: BeatMate/11.0 (LivePreview)\r\n")
            .withStatusCode(&statusCode));
    if (!stream) return {};
    auto body = stream->readEntireStreamAsString();
    if (body.isEmpty()) return {};
    if (statusCode != 0 && (statusCode < 200 || statusCode >= 300)) return {};

    try {
        auto j = nlohmann::json::parse(body.toStdString());
        if (!j.contains("results") || j["results"].empty()) {
            PreviewUrlCache::getInstance().put(title, artist, {});
            return {};
        }
        const auto& first = j["results"][0];
        auto url = first.value("previewUrl", std::string{});
        PreviewUrlCache::getInstance().put(title, artist, url);
        return url;
    } catch (const std::exception& ex) {
        spdlog::warn("[LivePreview] iTunes JSON parse error: {}", ex.what());
        return {};
    }
}

} // namespace BeatMate::UI::Widgets
