#pragma once

/**
 * @file log.h
 * @brief The project logging interface.
 *
 * Modules log through the LENS_* macros rather than calling spdlog directly, so the
 * destination, the line format, and the compile-time cut-off all live in one place.
 * init() installs the process-wide logger; the macros keep working before it runs by
 * falling back to spdlog's default logger.
 */

#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>

// SPDLOG_ACTIVE_LEVEL is set by CMake on lens_core (trace in Debug, info in Release), not
// here. spdlog's own common.h defaults it to info the moment it is included, so defining
// it in this header would silently lose to that default whatever the include order.



/// @name Logging macros
/// Thin wrappers over spdlog's own macros, so every line carries the source file, line,
/// and function, and so a call below SPDLOG_ACTIVE_LEVEL compiles away entirely.
/// @{
#define LENS_TRACE(...) SPDLOG_LOGGER_TRACE(::spdlog::default_logger(), __VA_ARGS__)
#define LENS_DEBUG(...) SPDLOG_LOGGER_DEBUG(::spdlog::default_logger(), __VA_ARGS__)
#define LENS_INFO(...) SPDLOG_LOGGER_INFO(::spdlog::default_logger(), __VA_ARGS__)
#define LENS_WARN(...) SPDLOG_LOGGER_WARN(::spdlog::default_logger(), __VA_ARGS__)
#define LENS_ERROR(...) SPDLOG_LOGGER_ERROR(::spdlog::default_logger(), __VA_ARGS__)
#define LENS_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::spdlog::default_logger(), __VA_ARGS__)
/// @}

namespace lens::log {

/// @brief Level used when neither the caller nor the environment overrides it.
#if defined(NDEBUG)
inline constexpr spdlog::level::level_enum kDefaultLevel = spdlog::level::info;
#else
inline constexpr spdlog::level::level_enum kDefaultLevel = spdlog::level::trace;
#endif

/// Size at which the log file rotates.
inline constexpr std::size_t kLogFileBytes = 10 * 1024 * 1024;

/// Rotated backups kept. spdlog counts backups only, so the live lens.log is extra:
/// three rotations leave lens.log plus lens.1.log through lens.3.log on disk.
inline constexpr std::size_t kLogFileCount = 3;

/// Name of the rotating file inside the log directory.
inline constexpr const char* kLogFileName = "lens.log";

/**
 * @brief Install the process-wide logger.
 *
 * Logs to `<logDir>/lens.log`, rotating at kLogFileBytes and keeping kLogFileCount rotated
 * backups alongside it, and mirrors every line to stderr in colour. The directory is
 * created when missing.
 *
 * @param level  Verbosity. The `LENS_LOG_LEVEL` environment variable ("trace", "debug",
 *               "info", "warn", "error", "critical", "off") overrides it when set; an
 *               unrecognised name leaves @p level in force.
 * @param logDir Directory holding the rotating file. Relative paths resolve against the
 *               process working directory. An empty path drops the file sink and keeps
 *               stderr only.
 * @note Safe to call more than once; the newest call wins.
 */
void init(spdlog::level::level_enum level = kDefaultLevel,
          const std::filesystem::path& logDir = std::filesystem::path{"logs"});

}
