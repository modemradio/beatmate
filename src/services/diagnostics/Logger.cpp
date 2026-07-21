#include "Logger.h"
#include <filesystem>
#include <spdlog/sinks/daily_file_sink.h>
namespace BeatMate::Services::Diagnostics {
std::shared_ptr<spdlog::logger> Logger::logger_;
void Logger::initialize(const std::string& logDir, const std::string& appName) {
    std::filesystem::create_directories(logDir);
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::info);
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logDir + "/" + appName + ".log", 5 * 1024 * 1024, 5);
    fileSink->set_level(spdlog::level::debug);
    logger_ = std::make_shared<spdlog::logger>(appName, spdlog::sinks_init_list{consoleSink, fileSink});
    logger_->set_level(spdlog::level::debug);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    spdlog::set_default_logger(logger_);
    spdlog::info("Logger: Initialized (log dir: {})", logDir);
}
void Logger::setLevel(spdlog::level::level_enum level) { if (logger_) logger_->set_level(level); }
std::shared_ptr<spdlog::logger> Logger::get() { return logger_ ? logger_ : spdlog::default_logger(); }
} // namespace BeatMate::Services::Diagnostics
