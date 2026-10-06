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
    QString screenName;
    bool ownProcess = false;
    QPoint anchor;
};

std::optional<ForegroundWindow> foregroundWindow();
std::optional<QPoint> physicalScreenOrigin(QString const& screenName);

}
