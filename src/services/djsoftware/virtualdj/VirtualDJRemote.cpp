#include "VirtualDJRemote.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::VirtualDJ {

VirtualDJRemote::VirtualDJRemote() {
}

VirtualDJRemote::~VirtualDJRemote() {
    disconnect();
}

bool VirtualDJRemote::connect(const std::string& ip, int port) {
    baseUrl_ = "http://" + ip + ":" + std::to_string(port);

    // VirtualDJ 2023+ HTTP API uses /query endpoint. Ping with a harmless
    juce::URL url{juce::String(baseUrl_ + "/query?script=get_time")};
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(1500);
    auto stream = url.createInputStream(options);

    if (stream != nullptr) {
        connected_ = true;
        if (connectedCallback_) connectedCallback_();
        spdlog::info("VirtualDJRemote: Connected to {}:{}", ip, port);
    } else {
        spdlog::warn("VirtualDJRemote: ping failed {}:{} (VirtualDJ not running or HTTP disabled)", ip, port);
        connected_ = false;
    }

    return connected_;
}

int VirtualDJRemote::readConfiguredPort() {
    auto settings = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("VirtualDJ").getChildFile("settings.xml");
    if (!settings.existsAsFile()) return 0;
    if (auto xml = juce::XmlDocument::parse(settings)) {
        for (auto* child : xml->getChildIterator()) {
            const auto tag  = child->getTagName().toLowerCase();
            const auto name = child->getStringAttribute("name").toLowerCase();
            if (tag.contains("networkcontrol") || name.contains("networkcontrol")) {
                const int v = child->getStringAttribute("value", child->getAllSubText()).getIntValue();
                if (v > 0 && v <= 65535) return v;
            }
        }
    }
    const auto raw = settings.loadFileAsString().toLowerCase();
    const int idx = raw.indexOf("networkcontrolport");
    if (idx >= 0) {
        const auto tail = raw.substring(idx, idx + 80);
        const int v = tail.retainCharacters("0123456789").substring(0, 5).getIntValue();
        if (v > 0 && v <= 65535) return v;
    }
    return 0;
}

bool VirtualDJRemote::connectAuto(const std::string& ip) {
    if (connect(ip, 80)) return true;
    if (connect(ip, 8080)) return true;
    const int custom = readConfiguredPort();
    if (custom > 0 && custom != 80 && custom != 8080 && connect(ip, custom)) return true;
    return false;
}

void VirtualDJRemote::disconnect() {
    if (connected_) {
        connected_ = false;
        if (disconnectedCallback_) disconnectedCallback_();
        spdlog::info("VirtualDJRemote: Disconnected");
    }
}

bool VirtualDJRemote::isConnected() const {
    return connected_;
}

static std::string vdjScriptQuery(const std::string& baseUrl, const std::string& script)
{
    juce::String encoded = juce::URL::addEscapeChars(juce::String(script), true);
    juce::URL url(juce::String(baseUrl) + "/query?script=" + encoded);
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(1000);
    auto stream = url.createInputStream(opts);
    if (!stream) return {};
    return stream->readEntireStreamAsString().trim().toStdString();
}

DeckInfo VirtualDJRemote::getCurrentTrack(int deck) {
    DeckInfo info;
    info.deckNumber = deck;
    if (!connected_) return info;

    // Reference: http://www.virtualdj.com/wiki/NetworkControlPlugin.html
    auto deckPrefix = "deck " + std::to_string(deck) + " ";
    info.title  = vdjScriptQuery(baseUrl_, deckPrefix + "get_title");
    info.artist = vdjScriptQuery(baseUrl_, deckPrefix + "get_artist");
    auto bpmStr = vdjScriptQuery(baseUrl_, deckPrefix + "get_bpm");
    info.bpm    = bpmStr.empty() ? 0.0 : std::stod(bpmStr);
    auto playingStr = vdjScriptQuery(baseUrl_, deckPrefix + "get_play");
    info.isPlaying  = (playingStr == "1" || playingStr == "on");
    info.filePath   = vdjScriptQuery(baseUrl_, deckPrefix + "get_filepath");
    return info;
}

std::vector<DeckInfo> VirtualDJRemote::getDecks() {
    std::vector<DeckInfo> decks;
    for (int i = 1; i <= 4; ++i) {
        decks.push_back(getCurrentTrack(i));
    }
    return decks;
}

bool VirtualDJRemote::play(int deck) {
    if (!connected_) return false;
    spdlog::debug("VirtualDJRemote: Play deck {}", deck);
    return true;
}

bool VirtualDJRemote::pause(int deck) {
    if (!connected_) return false;
    spdlog::debug("VirtualDJRemote: Pause deck {}", deck);
    return true;
}

bool VirtualDJRemote::sync(int deck) {
    if (!connected_) return false;
    spdlog::debug("VirtualDJRemote: Sync deck {}", deck);
    return true;
}

bool VirtualDJRemote::loadTrack(int deck, const std::string& filePath) {
    if (!connected_) {
        spdlog::warn("VirtualDJRemote: Cannot load track - not connected");
        return false;
    }
    if (filePath.empty()) {
        spdlog::warn("VirtualDJRemote: Cannot load track - empty file path");
        return false;
    }

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(2500);

    juce::String pathJS(filePath);
    pathJS = pathJS.replace("\"", "\\\"");

    const juce::String scripts[] = {
        juce::String("deck ") + juce::String(deck) + " loadtrack \"" + pathJS + "\"",
        juce::String("deck ") + juce::String(deck) + " load \""      + pathJS + "\"",
        juce::String("deck ") + juce::String(deck) + " open \""      + pathJS + "\"",
    };

    for (const auto& script : scripts) {
        juce::String encoded = juce::URL::addEscapeChars(script, true);
        for (const char* endpoint : { "/execute?script=", "/query?script=" }) {
            juce::URL getUrl(juce::String(baseUrl_) + endpoint + encoded);
            if (auto s = getUrl.createInputStream(options)) {
                auto body = s->readEntireStreamAsString().trim().toLowerCase();
                const bool isErr = body.startsWithIgnoreCase("error")
                                || body.startsWithIgnoreCase("false")
                                || body.contains("unknown")
                                || body.contains("invalid");
                if (!isErr) {
                    spdlog::info("VirtualDJRemote: loaded deck {} via {}'{}'",
                                 deck, endpoint, script.toStdString());
                    return true;
                }
            }
        }

        juce::URL postUrl(juce::String(baseUrl_) + "/send/");
        auto postReady = postUrl.withPOSTData("script=" + script);
        if (auto ps = postReady.createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                    .withConnectionTimeoutMs(2500))) {
            (void)ps->readEntireStreamAsString();
            spdlog::info("VirtualDJRemote: loaded deck {} via POST /send/", deck);
            return true;
        }
    }

    juce::String encodedPath = juce::URL::addEscapeChars(juce::String(filePath), true);
    juce::URL legacy(juce::String(baseUrl_) + "/api/decks/" + juce::String(deck) +
                     "/load?file=" + encodedPath);
    if (auto legacyStream = legacy.createInputStream(options)) {
        (void)legacyStream->readEntireStreamAsString();
        spdlog::info("VirtualDJRemote: (legacy API) track loaded on deck {}", deck);
        return true;
    }

    spdlog::error("VirtualDJRemote: Failed to load track on deck {}", deck);
    return false;
}

} // namespace BeatMate::Services::VirtualDJ
