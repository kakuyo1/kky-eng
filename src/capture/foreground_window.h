/** @file foreground_window.h
 * @brief Fail-closed Win32 foreground identity for the scanner.
 */
#pragma once

#include <QPoint>
#include <QString>

#include <optional>

namespace lens::capture {

struct ForegroundWindow {
    quintptr window = 0;
    QString context;
    QString processName;
    /// The monitor the window sits on, as Win32 hands it over.
    ///
    /// A handle rather than the monitor's name, because the two names in play are different
    /// strings: Qt reports a screen by the monitor's *friendly* name ("B156HAN15.H", padded with a
    /// space) and `MONITORINFOEXW` by its device name (`\.\DISPLAY1`). Comparing them is always
    /// false, so the scanner never found the screen to read -- silently, since a lookup that
    /// matches nothing looks exactly like a screen that is not there.
    quintptr monitor = 0;
    bool ownProcess  = false;
    QPoint anchor;
};

std::optional<ForegroundWindow> foregroundWindow();

/// @return The monitor a physical pixel falls on, or zero when it is on none of them.
quintptr monitorAt(QPoint physical);

/// @return The monitor's origin in physical pixels, or nothing when @p monitor is not one.
std::optional<QPoint> physicalScreenOrigin(quintptr monitor);

}
