#pragma once
#include <string>
namespace BeatMate::Core { class AudioBuffer; }
namespace BeatMate::Services::Plugins {
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual std::string getName() const = 0;
    virtual std::string getVersion() const = 0;
    virtual std::string getAuthor() const = 0;
    virtual std::string getDescription() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void process(Core::AudioBuffer* buffer) = 0;
};
} // namespace BeatMate::Services::Plugins
