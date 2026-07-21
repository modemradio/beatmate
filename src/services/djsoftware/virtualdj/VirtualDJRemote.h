#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace BeatMate::Services::VirtualDJ {

struct DeckInfo {
    int deckNumber = 0;
    std::string filePath;
    std::string title;
    std::string artist;
    double bpm = 0.0;
    double position = 0.0;
    bool isPlaying = false;
    float volume = 1.0f;
};

using ConnectedCallback = std::function<void()>;
using DisconnectedCallback = std::function<void()>;
using RemoteTrackChangedCallback = std::function<void(int deck, const std::string& title, const std::string& artist)>;

class VirtualDJRemote {

public:
    VirtualDJRemote();
    ~VirtualDJRemote();

    bool connect(const std::string& ip, int port = 80);
    bool connectAuto(const std::string& ip = "127.0.0.1");
    void disconnect();
    bool isConnected() const;

    static int readConfiguredPort();

    DeckInfo getCurrentTrack(int deck = 1);
    std::vector<DeckInfo> getDecks();

    bool play(int deck = 1);
    bool pause(int deck = 1);
    bool sync(int deck = 1);

    bool loadTrack(int deck, const std::string& filePath);

    void setConnectedCallback(ConnectedCallback cb) { connectedCallback_ = std::move(cb); }
    void setDisconnectedCallback(DisconnectedCallback cb) { disconnectedCallback_ = std::move(cb); }
    void setTrackChangedCallback(RemoteTrackChangedCallback cb) { trackChangedCallback_ = std::move(cb); }

private:
    std::string baseUrl_;
    bool connected_ = false;
    ConnectedCallback connectedCallback_;
    DisconnectedCallback disconnectedCallback_;
    RemoteTrackChangedCallback trackChangedCallback_;
};

}
