#pragma once

#include <spdlog/common.h>

#include <string>

/**
 * @file log.h
 * @brief Single entry point for logging setup, shared by every module.
 *
 * Modules log through spdlog directly (`spdlog::info(...)`, `spdlog::trace(...)`);
 * they only need this header to configure the destination and verbosity.
 */

namespace lens::log {

/// @brief Level used when neither the caller nor the environment says otherwise.
#if defined(NDEBUG)
inline constexpr spdlog::level::level_enum kDefaultLevel = spdlog::level::info;
#else
inline constexpr spdlog::level::level_enum kDefaultLevel = spdlog::level::trace;
#endif

/**
 * @brief Install the process-wide default logger.
 *
 * @param level   Verbosity for the logger. The `LENS_LOG_LEVEL` environment variable
 *                ("trace", "debug", "info", "warn", "error", "critical", "off")
 *                overrides this argument when set.
 * @param logFile Destination file, appended to across runs (1 MiB, 3 files, rotating).
 *                Empty means stderr with colour, which suits console targets; a windowless
 *                tray app must pass a path or its output goes nowhere.
 *
 * @note Safe to call more than once; the newest call wins.
 */
void init(spdlog::level::level_enum level = kDefaultLevel, const std::string& logFile = {});

}
