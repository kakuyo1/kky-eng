/** @file foreground_window.cpp
 * @brief Query only the foreground process, without reading titles or other windows.
 */
#include "foreground_window.h"

#include <QFileInfo>

#include <cstdint>
#include <memory>

#include <windows.h>

namespace lens::capture {

quintptr monitorAt(QPoint const physical)
{
    return reinterpret_cast<quintptr>(MonitorFromPoint(POINT{physical.x(), physical.y()}, MONITOR_DEFAULTTONULL));
}

std::optional<QPoint> physicalScreenOrigin(quintptr const monitor)
{
    if (monitor == 0) return std::nullopt;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (not GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor), &info)) return std::nullopt;
    return QPoint{info.rcMonitor.left, info.rcMonitor.top};
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
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (monitor == nullptr) return std::nullopt;
    ForegroundWindow result{.window     = reinterpret_cast<quintptr>(window),
                            .context    = QStringLiteral("%1:%2").arg(pid).arg(reinterpret_cast<quintptr>(window)),
                            .monitor    = reinterpret_cast<quintptr>(monitor),
                            .ownProcess = pid == GetCurrentProcessId(),
                            .anchor     = QPoint{rect.left, rect.top}};
    if (result.ownProcess) return result;
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
