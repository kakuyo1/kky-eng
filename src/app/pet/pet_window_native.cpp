/**
 * @file pet_window_native.cpp
 * @brief Win32 implementation of the pet window's capture exclusion and pass-through.
 */

#include "app/pet/pet_window_native.h"

#include <windows.h>

#include <QScreen>
#include <QWindow>
#include <QtGui/qscreen_platform.h>

namespace lens::app::pet {

namespace {

/// @return The native handle of a window Qt has created. Qt creates it on first winId() call.
HWND handleOf(QWindow const& window)
{
    return reinterpret_cast<HWND>(window.winId());
}

} // namespace

QString deviceNameOf(QScreen const& screen)
{
    auto const* native = screen.nativeInterface<QNativeInterface::QWindowsScreen>();
    if (not native) return {};

    auto info   = MONITORINFOEXW{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(native->handle(), &info) == FALSE) return {};
    return QString::fromWCharArray(info.szDevice);
}

bool excludeFromCapture(QWindow const& window)
{
    return SetWindowDisplayAffinity(handleOf(window), WDA_EXCLUDEFROMCAPTURE) != FALSE;
}

void setPassthrough(QWindow const& window, bool on)
{
    auto const hwnd  = handleOf(window);
    auto const style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // Layered stays on either way: transparency only takes effect on a layered window.
    auto const updated = on ? (style | WS_EX_LAYERED | WS_EX_TRANSPARENT)
                            : (style & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, updated);
}

} // namespace lens::app::pet
