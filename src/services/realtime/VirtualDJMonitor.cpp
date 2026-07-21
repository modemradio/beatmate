#include "VirtualDJMonitor.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <thread>
#include <atomic>

namespace BeatMate::Services::Realtime {

VirtualDJMonitor::VirtualDJMonitor() {}
VirtualDJMonitor::~VirtualDJMonitor() { stop(); }

void VirtualDJMonitor::start(const std::string& ip, int port) {
    ip_ = ip;
    port_ = port;
    startTimer(3000); // Poll every 3 seconds
    spdlog::info("VirtualDJMonitor: Started on {}:{}", ip, port);
}

void VirtualDJMonitor::stop() { stopTimer(); }

void VirtualDJMonitor::timerCallback() {
    // Launch HTTP request on a background thread to avoid blocking the UI
    auto ip = ip_;
    auto port = port_;
    auto callback = trackChangedCallback_;

    std::thread([ip, port, callback]() {
        try {
            juce::URL url("http://" + ip + ":" + std::to_string(port) + "/api/status");

            auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(300)
                .withResponseHeaders(nullptr);

            auto stream = url.createInputStream(options);
            if (!stream) return; // VDJ not running - normal

            auto response = stream->readEntireStreamAsString();
            if (response.isEmpty()) return;

            auto parsed = juce::JSON::parse(response);
            if (!parsed.isObject()) return;

            auto* root = parsed.getDynamicObject();
            if (!root) return;

            auto decksVar = root->getProperty("decks");
            auto* decks = decksVar.getArray();
            if (!decks) return;

            for (int i = 0; i < decks->size(); ++i) {
                auto* deck = (*decks)[i].getDynamicObject();
                if (!deck) continue;

                auto title  = deck->getProperty("title").toString().toStdString();
                auto artist = deck->getProperty("artist").toString().toStdString();

                if (!title.empty() && callback) {
                    juce::MessageManager::callAsync([callback, i, title, artist]() {
                        callback(i, title, artist);
                    });
                }
            }
        } catch (...) {
            // Silently ignore connection errors
        }
    }).detach();
}

} // namespace BeatMate::Services::Realtime
