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

std::intptr_t passthroughStyle(std::intptr_t style, bool on)
{
    // Layered stays on either way: transparency only takes effect on a layered window.
    if (on) return style | static_cast<std::intptr_t>(WS_EX_LAYERED | WS_EX_TRANSPARENT);
    return style & ~static_cast<std::intptr_t>(WS_EX_TRANSPARENT);
}

void setPassthrough(QWindow const& window, bool on)
{
    auto const hwnd  = handleOf(window);
    auto const style = static_cast<std::intptr_t>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(passthroughStyle(style, on)));
}

} // namespace lens::app::pet
