/**
 * @file mouse_selection_hook.cpp
 * @brief The gesture rule, and the low-level mouse hook that feeds it.
 */

#include "mouse_selection_hook.h"

#include <QTimer>

#include <cstdint>
#include <cstdlib>
#include <optional>

#include "core/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace lens::app {
namespace {

/**
 * The gesture bookkeeping the callback accumulates, and the system thresholds it judges
 * against. File scope rather than members, because LowLevelMouseProc is a free function:
 * Windows hands it no user data, so it has nothing to reach an instance through.
 */
struct GestureTracker
{
    // Read from the system once, in install(). A reader who changes Mouse Properties while
    // the app runs keeps the old values until it restarts; ponytail: not worth a
    // WM_SETTINGCHANGE handler for a number nobody retunes mid-session.
    int dragSlopPx = 4;        ///< max(SM_CXDRAG, SM_CYDRAG).
    int doubleClickMs = 500;   ///< GetDoubleClickTime().
    int doubleClickSlopPx = 4; ///< SM_CXDOUBLECLK.

    int downX = 0;
    int downY = 0;
    int clickRun = 0;
    std::uint32_t lastPressTick = 0;
    int lastPressX = 0;
    int lastPressY = 0;

    void onPress(int x, int y, std::uint32_t tick)
    {
        // Unsigned subtraction, so the wrap of GetTickCount every 49 days compares correctly
        // instead of reporting a 49-day gap.
        const std::uint32_t sinceLast = tick - lastPressTick;
        const bool inTime = lastPressTick != 0 && sinceLast <= static_cast<std::uint32_t>(doubleClickMs);
        const bool inPlace = std::abs(x - lastPressX) <= doubleClickSlopPx && std::abs(y - lastPressY) <= doubleClickSlopPx;

        // A low-level hook never receives WM_LBUTTONDBLCLK, so the run has to be rebuilt from
        // the same two numbers Windows itself compares: how long since the last press, and
        // how far it moved. Anything else starts a fresh run of one.
        clickRun = (inTime && inPlace) ? clickRun + 1 : 1;

        lastPressTick = tick;
        lastPressX = x;
        lastPressY = y;
        downX = x;
        downY = y;
    }

    /// @return Where the button came up, when the gesture was a selection.
    std::optional<QPoint> onRelease(int x, int y) const
    {
        const Gesture gesture{downX, downY, x, y, clickRun};
        if (!isSelectionGesture(gesture, dragSlopPx)) return std::nullopt;
        return QPoint(x, y);
    }
};

GestureTracker g_tracker;
MouseSelectionHook* g_owner = nullptr;
HHOOK g_handle = nullptr;

/**
 * The hook itself. Windows calls it on the thread that installed the hook, within a budget
 * it enforces silently: a callback that overruns LowLevelHooksTimeout gets the hook removed
 * with no error anywhere. So this does O(1) work and nothing else — the signal goes out
 * through a zero-delay timer, leaving the injection and its wait to the event loop.
 */
LRESULT CALLBACK lowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_owner != nullptr)
    {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        switch (wParam)
        {
            case WM_LBUTTONDOWN:
                g_tracker.onPress(info->pt.x, info->pt.y, GetTickCount());
                break;

            case WM_LBUTTONUP:
                if (const auto anchor = g_tracker.onRelease(info->pt.x, info->pt.y))
                {
                    // Emitted from file scope because the callback must be a free function.
                    // Qt's signals are public, so this is legal; the class's own code would
                    // read the same way.
                    MouseSelectionHook* const owner = g_owner;
                    QTimer::singleShot(0, owner, [owner, point = *anchor] { emit owner->selectionReleased(point); });
                }
                break;

            default:
                break;
        }
    }

    // Always chain on: something else may legitimately have installed a hook too.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}   // namespace

bool isSelectionGesture(const Gesture& gesture, int dragSlopPx)
{
    // Checked before the distance, because it is the more common way to pick a single word:
    // the second click selects it and the third selects the paragraph, neither with any
    // pointer movement for the distance test to see.
    if (gesture.clickRun >= 2) return true;

    const int dx = std::abs(gesture.upX - gesture.downX);
    const int dy = std::abs(gesture.upY - gesture.downY);
    return dx >= dragSlopPx || dy >= dragSlopPx;
}

MouseSelectionHook::MouseSelectionHook(QObject* parent) : QObject(parent) {}

MouseSelectionHook::~MouseSelectionHook()
{
    if (installed_ && g_owner == this && g_handle != nullptr)
    {
        UnhookWindowsHookEx(g_handle);
        g_handle = nullptr;
        g_owner = nullptr;
    }
}

bool MouseSelectionHook::install()
{
    if (installed_) return true;

    const int dragX = GetSystemMetrics(SM_CXDRAG);
    const int dragY = GetSystemMetrics(SM_CYDRAG);
    g_tracker.dragSlopPx = dragX > dragY ? dragX : dragY;
    g_tracker.doubleClickMs = static_cast<int>(GetDoubleClickTime());
    g_tracker.doubleClickSlopPx = GetSystemMetrics(SM_CXDOUBLECLK);

    g_owner = this;
    g_handle = SetWindowsHookExW(WH_MOUSE_LL, &lowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    if (g_handle == nullptr)
    {
        LENS_CRITICAL("MouseSelectionHook::install: SetWindowsHookEx failed with error {}", GetLastError());
        g_owner = nullptr;
        return false;
    }

    installed_ = true;
    LENS_INFO("MouseSelectionHook::install: listening (drag slop {} px, double click {} ms, click slop {} px)", g_tracker.dragSlopPx, g_tracker.doubleClickMs, g_tracker.doubleClickSlopPx);
    return true;
}

}
