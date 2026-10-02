#include "core/log.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <utility>

namespace lens::log {

void init(spdlog::level::level_enum level, const std::string& logFile) {
    if (const char* fromEnv = std::getenv("LENS_LOG_LEVEL")) level = spdlog::level::from_str(fromEnv);

    std::shared_ptr<spdlog::sinks::sink> sink;
    if (logFile.empty()) {
        sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    } else {
        sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile, 1024 * 1024, 3);
    }

    auto logger = std::make_shared<spdlog::logger>("lens", std::move(sink));
    logger->set_level(level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(std::move(logger));
}

}
