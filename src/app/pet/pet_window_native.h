#pragma once

class QWindow;

/**
 * @file pet_window_native.h
 * @brief The two window behaviours Qt has no switch for: capture exclusion and mouse pass-through (PHASE3 3.5).
 */

namespace lens::app::pet {

/**
 * @brief Keeps the window out of screen capture, so screenshots and the OCR scan's pixel diff never see the pet.
 * @param window A window that has been shown, or is about to be.
 * @return False when the OS refuses (before Windows 10 2004). The pet then appears in captures and the caller says so.
 */
bool excludeFromCapture(QWindow const& window);

/**
 * @brief Lets every mouse event fall through the window, by toggling WS_EX_TRANSPARENT over WS_EX_LAYERED.
 * @param window A window that has been shown.
 * @param on True to pass input through, false to take it back. Only the transparency bit changes; the window is not rebuilt.
 */
void setPassthrough(QWindow const& window, bool on);

} // namespace lens::app::pet
