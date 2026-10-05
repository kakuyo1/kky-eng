/**
 * @file mouse_selection_hook.cpp
 * @brief The gesture rule, and the low-level mouse hook that feeds it.
 */

#include "mouse_selection_hook.h"

#include <cstdlib>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

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
struct GestureTracker {
    // Read from the system once, in install(). A reader who changes Mouse Properties while
    // the app runs keeps the old values until it restarts; ponytail: not worth a
    // WM_SETTINGCHANGE handler for a number nobody retunes mid-session.
    int dragSlopPx = 4; ///< max(SM_CXDRAG, SM_CYDRAG).
    int downX = 0;
    int downY = 0;
    bool overOwnWindow = false; ///< The press landed on a surface of this process.

    void onPress(int x, int y, bool own)
    {
        downX = x;
        downY = y;
        overOwnWindow = own;

        // A press on one of our own surfaces is not the start of a selection: the reader is
        // dragging a panel or working a control, and the text the pipeline would go on to copy
        // is whatever the application behind it has selected.
    }

    /// @return Where the button came up, when the gesture was a selection.
    std::optional<QPoint> onRelease(int x, int y) const
    {
        if (overOwnWindow) return std::nullopt;

        const Gesture gesture{downX, downY, x, y};
        if (!isSelectionGesture(gesture, dragSlopPx)) return std::nullopt;
        return QPoint(x, y);
    }
};

/// @return True when the point is over a window this process owns.
///
/// @note Deliberately the window and not the card: a surface's transparent shadow margin counts
///       as ours too, so a selection started within 26 pixels of a panel is dropped as well. The
///       alternative -- asking the surfaces for their card rectangles -- would put the geometry
///       in two places, and the cost of this is one missed action bar in a place the reader was
///       already reaching past a panel to begin with.
bool overOurWindow(POINT pt)
{
    const HWND under = WindowFromPoint(pt);
    if (under == nullptr) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(under, &pid);
    return pid == GetCurrentProcessId();
}

GestureTracker g_tracker;
MouseSelectionHook* g_owner = nullptr;

// The hook's own thread, and the only thing it does is wait for messages.
//
// Windows calls a WH_MOUSE_LL callback on the thread that installed the hook, and holds the
// mouse until that thread answers -- up to LowLevelHooksTimeout, 300 ms by default. The main
// thread owns the window, the word list, the store and the renderer, so it will always have
// something that blocks for longer than a mouse may wait: measured on the real machine, at
// startup alone, three inputs at 312 ms apiece while it compiled the QML and built the first
// frame. Installing the hook later only moves which work is caught, so it lives here instead.
HHOOK g_handle = nullptr;
DWORD g_hookThreadId = 0;
DWORD g_hookError = 0;
std::thread g_hookThread;
std::mutex g_startMutex;
std::condition_variable g_started;
bool g_startDone = false;

/// @brief Report an event to the loop that owns the surfaces, keeping the callback O(1).
///
/// A queued invocation rather than a direct emit or a zero-delay timer: the callback runs on the
/// hook's thread, and the controller's connections have to be delivered on the thread that owns
/// the surfaces. Queued delivery is what Qt documents for exactly this.
void postToOwner(MouseSelectionHook* owner, std::function<void()> report)
{
    QMetaObject::invokeMethod(owner, std::move(report), Qt::QueuedConnection);
}

void emitPressedLater(const POINT& pt)
{
    MouseSelectionHook* const owner = g_owner;
    const QPoint at(pt.x, pt.y);
    postToOwner(owner, [owner, at] { emit owner->pointerPressed(at); });
}

/**
 * The hook itself. Runs on the hook's own thread, within a budget Windows enforces silently: a
 * callback that overruns LowLevelHooksTimeout gets the hook removed with no error anywhere. So
 * this does O(1) work and nothing else -- the signal is queued rather than emitted, leaving the
 * injection and its wait to the thread that owns the surfaces.
 */
LRESULT CALLBACK lowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_owner != nullptr) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        switch (wParam) {
            case WM_LBUTTONDOWN:
                // WindowFromPoint is a single lookup, on the one message where the answer is
                // needed; the callback's budget is the reason the rest of the work is deferred.
                g_tracker.onPress(info->pt.x, info->pt.y, overOurWindow(info->pt));
                emitPressedLater(info->pt);
                break;

            case WM_RBUTTONDOWN:
                emitPressedLater(info->pt);
                break;

            case WM_LBUTTONUP:
                if (const auto anchor = g_tracker.onRelease(info->pt.x, info->pt.y)) {
                    // Emitted from file scope because the callback must be a free function.
                    // Qt's signals are public, so this is legal; the class's own code would
                    // read the same way.
                    MouseSelectionHook* const owner = g_owner;
                    postToOwner(owner, [owner, point = *anchor] { emit owner->selectionReleased(point); });
                }
                break;

            default:
                break;
        }
    }

    // Always chain on: something else may legitimately have installed a hook too.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

/// @brief Runs on the hook's own thread: install, then pump until asked to stop.
void hookThreadMain()
{
    // A thread gets its message queue on the first peek, and PostThreadMessage cannot reach this
    // thread until it has one -- which is how the destructor asks it to leave.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    g_handle = SetWindowsHookExW(WH_MOUSE_LL, &lowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    {
        const std::lock_guard<std::mutex> lock(g_startMutex);
        g_hookThreadId = GetCurrentThreadId();
        g_hookError = g_handle == nullptr ? GetLastError() : 0;
        g_startDone = true;
    }
    g_started.notify_all();

    if (g_handle == nullptr)
        return;

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Unhooked here rather than from the destructor: this is the thread the callback runs on, so
    // it is the only one that can know no callback is in flight.
    UnhookWindowsHookEx(g_handle);
    g_handle = nullptr;
    g_owner = nullptr;
}

} // namespace

bool isSelectionGesture(const Gesture& gesture, int dragSlopPx)
{
    const int dx = std::abs(gesture.upX - gesture.downX);
    const int dy = std::abs(gesture.upY - gesture.downY);
    return dx >= dragSlopPx || dy >= dragSlopPx;
}

MouseSelectionHook::MouseSelectionHook(QObject* parent)
    : QObject(parent)
{}

MouseSelectionHook::~MouseSelectionHook()
{
    if (!installed_ || g_owner != this)
        return;

    // Ask the hook's thread to leave and wait for it: it unhooks on the way out, from the thread
    // the callback runs on, which is the only place that can know none is in flight.
    if (g_hookThreadId != 0)
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
    if (g_hookThread.joinable())
        g_hookThread.join();
    installed_ = false;
}

bool MouseSelectionHook::install()
{
    if (installed_) return true;

    // Read on this thread, before the hook starts: the tracker is touched only by the callback
    // afterwards, and starting the thread is what publishes these values to it.
    const int dragX = GetSystemMetrics(SM_CXDRAG);
    const int dragY = GetSystemMetrics(SM_CYDRAG);
    g_tracker.dragSlopPx = dragX > dragY ? dragX : dragY;
    g_owner = this;
    {
        std::unique_lock<std::mutex> lock(g_startMutex);
        g_startDone = false;
        g_hookThread = std::thread(hookThreadMain);
        g_started.wait(lock, [] { return g_startDone; });
    }

    if (g_handle == nullptr) {
        LENS_CRITICAL("MouseSelectionHook::install: SetWindowsHookEx failed with error {}", g_hookError);
        g_owner = nullptr;
        g_hookThread.join();
        return false;
    }

    installed_ = true;
    LENS_INFO("MouseSelectionHook::install: listening on its own thread (drag slop {} px; click-to-select disabled)", g_tracker.dragSlopPx);
    return true;
}

}
