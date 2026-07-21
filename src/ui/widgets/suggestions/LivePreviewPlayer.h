#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace BeatMate::UI::Widgets {

// Shared 30-second preview player for the Live view.
class LivePreviewPlayer {
public:
    enum class State { Idle, Loading, Playing };

    static LivePreviewPlayer& getInstance();

    void playPreview(const std::string& rowId,
                     const std::string& artist,
                     const std::string& title,
                     const std::string& directUrl = {});

    void stop();

    std::string currentRowId() const;
    State       currentState() const;

    // Callback fires on the message thread.
    struct ListenerToken {
        ListenerToken() = default;
        explicit ListenerToken(int id);
        ~ListenerToken();
        ListenerToken(const ListenerToken&) = delete;
        ListenerToken& operator=(const ListenerToken&) = delete;
        ListenerToken(ListenerToken&& o) noexcept;
        ListenerToken& operator=(ListenerToken&& o) noexcept;
        int id = 0;
    };
    ListenerToken addListener(std::function<void()> cb);

    ~LivePreviewPlayer();

private:
    LivePreviewPlayer();
    LivePreviewPlayer(const LivePreviewPlayer&) = delete;
    LivePreviewPlayer& operator=(const LivePreviewPlayer&) = delete;

    void ensureDevice();
    void notifyListeners();
    void attachReaderAndPlay(const std::string& rowId,
                             std::unique_ptr<juce::AudioFormatReader> reader);
    std::unique_ptr<juce::AudioFormatReader> createReaderForUrl(const std::string& url);
    void failLoad(const std::string& rowId);
    static std::string lookupPreviewUrl(const std::string& artist,
                                        const std::string& title);

    mutable std::mutex lock_;
    std::unique_ptr<juce::AudioDeviceManager>   device_;
    std::unique_ptr<juce::AudioFormatManager>   formats_;
    std::unique_ptr<juce::AudioTransportSource> transport_;
    std::unique_ptr<juce::AudioSourcePlayer>    player_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;

    std::atomic<State> state_ { State::Idle };
    std::string currentRowId_;

    int nextListenerId_ = 1;
    std::vector<std::pair<int, std::function<void()>>> listeners_;

    std::atomic<uint64_t> loadGeneration_ { 0 };

    void removeListener(int id);
    friend struct ListenerToken;
};

} // namespace BeatMate::UI::Widgets
