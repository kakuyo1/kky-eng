#include "util/log.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lens::log {
namespace {

/// @return The level named by LENS_LOG_LEVEL, or @p fallback when it is unset or the name
///         is not one spdlog knows. A typo must not silence logging by accident.
spdlog::level::level_enum levelFromEnvironment(spdlog::level::level_enum fallback)
{
    const char* name = std::getenv("LENS_LOG_LEVEL");
    if (name == nullptr) return fallback;

    const auto parsed = spdlog::level::from_str(name);
    const bool known  = parsed != spdlog::level::off || std::string_view{name} == "off";
    return known ? parsed : fallback;
}

} // namespace

void init(spdlog::level::level_enum level, const std::filesystem::path& logDir)
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    // Collect the file-sink problem instead of throwing: a logging setup failure must not
    // stop the program from starting, and the logger to report it through does not exist
    // until the end of this function.
    std::string fileError;
    if (!logDir.empty()) {
        const std::filesystem::path file = logDir / kLogFileName;
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);

        if (ec) {
            fileError = "cannot create directory: " + ec.message();
        } else {
            try {
                sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    file.string(), kLogFileBytes, kLogFileCount));
            } catch (const std::exception& e) {
                fileError = e.what();
            }
        }
        if (!fileError.empty()) fileError += " (" + file.string() + ")";
    }

    auto logger = std::make_shared<spdlog::logger>("lens", sinks.begin(), sinks.end());
    logger->set_level(levelFromEnvironment(level));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] [%s:%#] %v");
    // Every line, not just from a warning up: the file is meant to be watched while the app
    // runs (`tail -f logs/lens.log`), and a reader who has to wait for the next warning to see
    // what just happened is not watching a log. The volume is a tray app's, so the write costs
    // nothing worth measuring.
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(logger));

    if (!fileError.empty()) LENS_WARN("file logging disabled: {}", fileError);
}

}
