/**
 * @file autostart.cpp
 * @brief The Run entry that starts Lens with the reader's session.
 */

#include "app/autostart.h"

#include <QCoreApplication>
#include <QSettings>

#include "core/log.h"

namespace lens::app {

bool writeAutostart(bool on)
{
    QSettings run(kAutostartRunKey, QSettings::NativeFormat);
    if (on) {
        const std::string path = QCoreApplication::applicationFilePath().toStdString();
        run.setValue(kAutostartValueName, QString::fromStdString(autostartCommand(path)));
    } else {
        run.remove(kAutostartValueName);
    }
    run.sync();

    // What the return value says is whether the registry took the change, so a policy that
    // forbids the key is reported rather than quietly lost.
    const bool stored = run.status() == QSettings::NoError;
    if (!stored)
        LENS_WARN("autostart could not be turned {} (registry status {})", on ? "on" : "off", static_cast<int>(run.status()));
    else
        LENS_INFO("autostart {}", on ? "on" : "off");
    return stored;
}

bool autostartEnabled()
{
    const QSettings run(kAutostartRunKey, QSettings::NativeFormat);
    return !run.value(kAutostartValueName).toString().isEmpty();
}

}
