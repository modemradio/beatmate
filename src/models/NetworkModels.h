#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class ConnectionState : int {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Reconnecting = 3,
    Error = 4
};

NLOHMANN_JSON_SERIALIZE_ENUM(ConnectionState, {
    { ConnectionState::Disconnected, "Disconnected" },
    { ConnectionState::Connecting, "Connecting" },
    { ConnectionState::Connected, "Connected" },
    { ConnectionState::Reconnecting, "Reconnecting" },
    { ConnectionState::Error, "Error" }
})

struct ConnectionStatus {
    ConnectionState state = ConnectionState::Disconnected;
    std::string serverUrl;
    std::string errorMessage;
    int64_t connectedSince = 0;     // unix timestamp
    int64_t lastPingAt = 0;
    int latencyMs = 0;              // round-trip latency
    int reconnectAttempts = 0;
    int maxReconnectAttempts = 10;

    ConnectionStatus() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ConnectionStatus,
        state, serverUrl, errorMessage, connectedSince,
        lastPingAt, latencyMs, reconnectAttempts, maxReconnectAttempts
    )
};

enum class SyncDirection : int {
    Upload = 0,
    Download = 1,
    Bidirectional = 2
};

NLOHMANN_JSON_SERIALIZE_ENUM(SyncDirection, {
    { SyncDirection::Upload, "Upload" },
    { SyncDirection::Download, "Download" },
    { SyncDirection::Bidirectional, "Bidirectional" }
})

enum class SyncStatus : int {
    Idle = 0,
    Syncing = 1,
    Completed = 2,
    Failed = 3,
    Conflict = 4,
    Cancelled = 5
};

NLOHMANN_JSON_SERIALIZE_ENUM(SyncStatus, {
    { SyncStatus::Idle, "Idle" },
    { SyncStatus::Syncing, "Syncing" },
    { SyncStatus::Completed, "Completed" },
    { SyncStatus::Failed, "Failed" },
    { SyncStatus::Conflict, "Conflict" },
    { SyncStatus::Cancelled, "Cancelled" }
})

struct SyncState {
    SyncStatus status = SyncStatus::Idle;
    SyncDirection direction = SyncDirection::Bidirectional;
    double progress = 0.0;          // 0-1
    int totalItems = 0;
    int syncedItems = 0;
    int failedItems = 0;
    int conflictItems = 0;
    int64_t lastSyncAt = 0;         // unix timestamp
    int64_t startedAt = 0;
    std::string currentItem;        // description of item being synced
    std::string errorMessage;
    std::vector<std::string> errors;
    std::vector<std::string> conflicts;

    SyncState() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SyncState,
        status, direction, progress, totalItems, syncedItems,
        failedItems, conflictItems, lastSyncAt, startedAt,
        currentItem, errorMessage, errors, conflicts
    )
};

enum class WebSocketMessageType : int {
    Ping = 0,
    Pong = 1,
    Subscribe = 2,
    Unsubscribe = 3,
    Publish = 4,
    Request = 5,
    Response = 6,
    Error = 7,
    SyncUpdate = 8,
    TrackUpdate = 9,
    PlaylistUpdate = 10,
    SettingsUpdate = 11,
    Notification = 12
};

NLOHMANN_JSON_SERIALIZE_ENUM(WebSocketMessageType, {
    { WebSocketMessageType::Ping, "Ping" },
    { WebSocketMessageType::Pong, "Pong" },
    { WebSocketMessageType::Subscribe, "Subscribe" },
    { WebSocketMessageType::Unsubscribe, "Unsubscribe" },
    { WebSocketMessageType::Publish, "Publish" },
    { WebSocketMessageType::Request, "Request" },
    { WebSocketMessageType::Response, "Response" },
    { WebSocketMessageType::Error, "Error" },
    { WebSocketMessageType::SyncUpdate, "SyncUpdate" },
    { WebSocketMessageType::TrackUpdate, "TrackUpdate" },
    { WebSocketMessageType::PlaylistUpdate, "PlaylistUpdate" },
    { WebSocketMessageType::SettingsUpdate, "SettingsUpdate" },
    { WebSocketMessageType::Notification, "Notification" }
})

struct WebSocketMessage {
    WebSocketMessageType type = WebSocketMessageType::Ping;
    std::string id;                 // message ID for request/response correlation
    std::string channel;            // pub/sub channel
    std::string topic;              // message topic
    nlohmann::json payload;         // message payload
    int64_t timestamp = 0;
    std::string senderId;
    std::string senderName;

    WebSocketMessage() = default;

    WebSocketMessage(WebSocketMessageType type, const std::string& topic)
        : type(type), topic(topic) {}

    WebSocketMessage(WebSocketMessageType type, const std::string& topic, const nlohmann::json& payload)
        : type(type), topic(topic), payload(payload) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WebSocketMessage,
        type, id, channel, topic, payload, timestamp, senderId, senderName
    )
};

} // namespace BeatMate::Models
