#include "llm/qt_log.h"

#include "core/log.h"

#include <QtCore/QString>
#include <QtCore/qlogging.h>

#include <string>

namespace lens::log {
namespace {

spdlog::level::level_enum levelFor(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg: return spdlog::level::debug;
        case QtInfoMsg: return spdlog::level::info;
        case QtWarningMsg: return spdlog::level::warn;
        case QtCriticalMsg: return spdlog::level::err; // spdlog spells it 'err'
        case QtFatalMsg: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

/// @note Runs on whatever thread Qt logged from; the sinks are the thread-safe variants.
void forwardToSpdlog(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // Qt compiles these out unless QT_MESSAGELOGCONTEXT is on, and omits the function in
    // release builds, so both are allowed to be null.
    const spdlog::source_loc location{context.file != nullptr ? context.file : "", context.line, context.function != nullptr ? context.function : ""};

    // The message is an argument, not the format string, so braces inside it stay literal.
    spdlog::default_logger()->log(location, levelFor(type), "qt: {}", message.toStdString());
}

} // namespace

void installQtMessageHandler()
{
    qInstallMessageHandler(&forwardToSpdlog);
}

}
