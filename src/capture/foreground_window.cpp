/** @file foreground_window.cpp
 * @brief Query only the foreground process, without reading titles or other windows.
 */
#include "foreground_window.h"

#include <QFileInfo>

#include <cstdint>
#include <memory>

#include <windows.h>

namespace lens::capture {

std::optional<QPoint> physicalScreenOrigin(QString const& screenName)
{
    auto const device = screenName.toStdWString();
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (not EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &mode)) return std::nullopt;
    return QPoint{mode.dmPosition.x, mode.dmPosition.y};
}

std::optional<ForegroundWindow> foregroundWindow()
{
    auto const window = GetForegroundWindow();
    if (window == nullptr or IsIconic(window)) return std::nullopt;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0) return std::nullopt;
    RECT rect{};
    if (not GetWindowRect(window, &rect)) return std::nullopt;
    auto const width  = std::int64_t{rect.right} - rect.left;
    auto const height = std::int64_t{rect.bottom} - rect.top;
    if (width <= 0 or height <= 0 or width > 8192 or height > 8192 or width * height > 32 * 1024 * 1024)
        return std::nullopt;
    ForegroundWindow result{.window     = reinterpret_cast<quintptr>(window),
                            .context    = QStringLiteral("%1:%2").arg(pid).arg(reinterpret_cast<quintptr>(window)),
                            .ownProcess = pid == GetCurrentProcessId(),
                            .anchor     = QPoint{rect.left, rect.top}};
    if (result.ownProcess) return result;
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (not GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return std::nullopt;
    result.screenName  = QString::fromWCharArray(monitor.szDevice);
    auto const process = std::unique_ptr<void, decltype(&CloseHandle)>{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), &CloseHandle};
    if (not process) return std::nullopt;
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    if (not QueryFullProcessImageNameW(process.get(), 0, path, &size)) return std::nullopt;
    result.processName = QFileInfo{QString::fromWCharArray(path, static_cast<int>(size))}.fileName();
    return result;
}

}
