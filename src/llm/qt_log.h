#pragma once

/**
 * @file qt_log.h
 * @brief Routes Qt's own diagnostics into spdlog, so one file holds the whole story.
 *
 * @note This is Qt plumbing with no LLM behaviour in it. lens_llm is where it compiles
 *       because that is the lowest target that links Qt at all; every Qt-linked target above
 *       it depends on lens_llm, so moving the file would buy nothing.
 */

namespace lens::log {

/**
 * @brief Forward qDebug / qInfo / qWarning / qCritical / qFatal into spdlog.
 *
 * Levels map one to one: debug, info, warn, error, critical. The Qt call site travels
 * along as spdlog's source location, so a Qt warning points at the Qt file that raised it
 * rather than at this bridge. A fatal message is logged at critical before Qt aborts.
 *
 * @note Call after init(), or the messages land on whatever logger is installed by then.
 *       Safe to call repeatedly; Qt keeps only the newest handler.
 */
void installQtMessageHandler();

}
