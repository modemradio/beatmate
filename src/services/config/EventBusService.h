#pragma once
#include <any>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <typeindex>

namespace BeatMate::Services::Config {

class EventBusService {
public:
    using EventCallback = std::function<void(const std::any& data)>;
    using SubscriptionId = uint64_t;

    static EventBusService& getInstance();

    SubscriptionId subscribe(const std::string& eventName, EventCallback callback);

    void unsubscribe(SubscriptionId id);
    void unsubscribeAll(const std::string& eventName);

    // Synchronous dispatch.
    void publish(const std::string& eventName, const std::any& data = {});

    template<typename T>
    void emit(const std::string& eventName, const T& data) {
        publish(eventName, std::any(data));
    }

    void emit(const std::string& eventName) {
        publish(eventName, std::any{});
    }

    template<typename T>
    SubscriptionId on(const std::string& eventName, std::function<void(const T&)> callback) {
        return subscribe(eventName, [callback](const std::any& data) {
            try {
                callback(std::any_cast<const T&>(data));
            } catch (const std::bad_any_cast&) {}
        });
    }

    SubscriptionId once(const std::string& eventName, EventCallback callback);

    bool hasSubscribers(const std::string& eventName) const;
    int getSubscriberCount(const std::string& eventName) const;
    std::vector<std::string> getActiveEvents() const;

    void clear();

    static constexpr const char* EVT_TRACK_LOADED = "track.loaded";
    static constexpr const char* EVT_TRACK_UNLOADED = "track.unloaded";
    static constexpr const char* EVT_PLAYBACK_STARTED = "playback.started";
    static constexpr const char* EVT_PLAYBACK_STOPPED = "playback.stopped";
    static constexpr const char* EVT_BPM_CHANGED = "bpm.changed";
    static constexpr const char* EVT_KEY_CHANGED = "key.changed";
    static constexpr const char* EVT_VOLUME_CHANGED = "volume.changed";
    static constexpr const char* EVT_CROSSFADER_CHANGED = "crossfader.changed";
    static constexpr const char* EVT_EQ_CHANGED = "eq.changed";
    static constexpr const char* EVT_EFFECT_CHANGED = "effect.changed";
    static constexpr const char* EVT_CUE_TRIGGERED = "cue.triggered";
    static constexpr const char* EVT_LOOP_CHANGED = "loop.changed";
    static constexpr const char* EVT_SYNC_STATE_CHANGED = "sync.stateChanged";
    static constexpr const char* EVT_RECORDING_STARTED = "recording.started";
    static constexpr const char* EVT_RECORDING_STOPPED = "recording.stopped";
    static constexpr const char* EVT_MIDI_CC = "midi.cc";
    static constexpr const char* EVT_MIDI_NOTE = "midi.note";
    static constexpr const char* EVT_WORKSPACE_CHANGED = "workspace.changed";
    static constexpr const char* EVT_THEME_CHANGED = "theme.changed";
    static constexpr const char* EVT_LIBRARY_UPDATED = "library.updated";
    static constexpr const char* EVT_ANALYSIS_COMPLETE = "analysis.complete";
    static constexpr const char* EVT_ERROR = "error";

private:
    EventBusService() = default;
    ~EventBusService() = default;
    EventBusService(const EventBusService&) = delete;
    EventBusService& operator=(const EventBusService&) = delete;

    struct Subscription {
        SubscriptionId id;
        std::string eventName;
        EventCallback callback;
        bool oneShot = false;
    };

    std::map<std::string, std::vector<Subscription>> subscriptions_;
    mutable std::mutex mutex_;
    SubscriptionId nextId_ = 1;
};

} // namespace BeatMate::Services::Config
