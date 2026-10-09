#pragma once

/**
 * @file tray_state.h
 * @brief What the tray icon is saying, as a priority rather than a sum.
 *
 * Tray used to pick this inline, and the ordering is the whole content of the rule: an exhausted
 * budget is not a request in flight, and a reader who has switched capture off still has to see
 * that the day's money ran out. The state lives here rather than in Tray because nothing behind
 * a private member of it can be asserted, and PHASE2 section 4.4's acceptance names this state.
 */

namespace lens::app {

/// @brief Enough of the tray state to be worth naming; the icon art is picked from it.
enum class TrayState {
    Auto,   ///< Selection capture is on and nothing is in flight.
    Busy,   ///< An explanation is being fetched.
    Off,    ///< Selection capture is off.
    Budget, ///< Today's budget has been reached.
};

/**
 * @brief The state the controller's own values imply.
 * @param budgetExhausted Today's spend reached the cap: outranks the other two.
 * @param busy An explanation is in flight.
 * @param captureOn The reader has selection capture switched on.
 * @return The state, most urgent first.
 */
inline TrayState trayState(bool budgetExhausted, bool busy, bool captureOn)
{
    if (budgetExhausted)
        return TrayState::Budget;
    if (busy)
        return TrayState::Busy;
    return captureOn ? TrayState::Auto : TrayState::Off;
}

/**
 * @brief The icon art's name, without the taskbar- half the file names carry.
 * @param state The state to draw.
 * @return One of "auto", "busy", "off", "budget"; every one of them has an icon shipped.
 */
inline const char* trayIconName(TrayState state)
{
    switch (state) {
        case TrayState::Busy:
            return "busy";
        case TrayState::Off:
            return "off";
        case TrayState::Budget:
            return "budget";
        case TrayState::Auto:
            break;
    }
    return "auto";
}

}
