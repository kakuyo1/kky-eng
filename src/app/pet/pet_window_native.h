#pragma once

#include <cstdint>

#include <QString>

class QScreen;
class QWindow;

/**
 * @file pet_window_native.h
 * @brief The window behaviours Qt has no switch for: capture exclusion, mouse pass-through, and the monitor's
 *        device name that the pet records its position against (PHASE3 3.5).
 */

namespace lens::app::pet {

/**
 * @brief The device name of the monitor behind a screen, such as `\\.\DISPLAY1`.
 *
 * This is the identity the saved position uses. QScreen::name() is not: on Windows it is the monitor's
 * friendly name from its EDID, which PHASE3 3.5 rules out.
 * @return The name from MONITORINFOEXW::szDevice, or empty when Windows cannot read the monitor.
 */
QString deviceNameOf(QScreen const& screen);

/**
 * @brief Keeps the window out of screen capture, so screenshots and the OCR scan's pixel diff never see the pet.
 * @param window A window that has been shown, or is about to be.
 * @return False when the OS refuses (before Windows 10 2004). The pet then appears in captures and the caller says so.
 */
bool excludeFromCapture(QWindow const& window);

/**
 * @brief The extended window style after pass-through is switched: WS_EX_TRANSPARENT added or removed, with
 *        WS_EX_LAYERED kept on and every other bit untouched.
 * @param style The window's current extended style.
 * @param on True to pass input through.
 * @return The style to set.
 */
std::intptr_t passthroughStyle(std::intptr_t style, bool on);

/**
 * @brief Lets every mouse event fall through the window, by toggling WS_EX_TRANSPARENT over WS_EX_LAYERED.
 * @param window A window that has been shown.
 * @param on True to pass input through, false to take it back. Only the transparency bit changes; the window is not rebuilt.
 */
void setPassthrough(QWindow const& window, bool on);

} // namespace lens::app::pet
