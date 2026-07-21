#pragma once
#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
namespace BeatMate::Services::Diagnostics {
class Logger {
public:
    static void initialize(const std::string& logDir = "logs", const std::string& appName = "BeatMate");
    static void setLevel(spdlog::level::level_enum level);
    static std::shared_ptr<spdlog::logger> get();
private:
    static std::shared_ptr<spdlog::logger> logger_;
};
} // namespace BeatMate::Services::Diagnostics
