#pragma once

#include <QObject>
#include <QPoint>

/**
 * @file mouse_selection_hook.h
 * @brief Watches the mouse for the moment the reader finishes selecting text.
 *
 * Windows offers no API that hands over the selected text of another application, so the
 * trigger cannot be "the selection changed" — it has to be the gesture. A drag that ended is
 * the point at which a selection exists. The hook reports only that moment; getting the text out
 * is a separate step, see
 * selection_text_grabber.h. Injecting Ctrl+C from inside a low-level hook callback would
 * overrun the budget Windows allows it and get the hook removed without a word, so the
 * callback stays O(1) and defers.
 *
 * @note Windows only, like the rest of src/app. This is where the Win32 mouse calls live.
 */

namespace lens::app {

/**
 * @brief What the hook saw between one left-button press and its release.
 *
 * Positions are virtual-screen pixels, so a multi-monitor desktop with negative origins
 * needs no special case.
 */
struct Gesture {
    int downX = 0; ///< Where the button went down.
    int downY = 0;
    int upX = 0; ///< Where it came back up.
    int upY = 0;
};

/**
 * @brief Whether a finished gesture was the reader selecting text.
 *
 * A drag that travelled at least @p dragSlopPx counts. A click or click run that stayed inside
 * the slop is not treated as a selection, so ordinary double-clicks do not open the selection
 * action bar unexpectedly.
 *
 * @param gesture    Endpoints the hook accumulated.
 * @param dragSlopPx Movement below this counts as jitter rather than a drag. The caller
 *                   seeds it from the system's own SM_CXDRAG / SM_CYDRAG so the rule
 *                   follows the reader's mouse settings.
 * @return True when the gesture should be treated as a selection.
 */
bool isSelectionGesture(const Gesture& gesture, int dragSlopPx);

/**
 * @brief Installs the low-level mouse hook and reports completed selections.
 *
 * The hook is process-wide and its callback runs on the thread that installed it, which
 * must be the thread pumping messages. Everything expensive happens after the callback has
 * returned.
 */
class MouseSelectionHook : public QObject {
    Q_OBJECT
public:
    explicit MouseSelectionHook(QObject* parent = nullptr);

    /// @brief Uninstalls the hook if it is still installed.
    ~MouseSelectionHook() override;

    /**
     * @brief Install the hook.
     * @return True on success. On failure the reason is logged at CRITICAL and false comes
     *         back; nothing throws, because this runs on a startup path.
     * @note Calling it twice is harmless: the second call reports success without
     *       reinstalling.
     */
    bool install();

signals:
    /// @brief A left-button gesture was judged a completed selection.
    /// @param anchor Release position in the coordinates Windows reports to the hook: real
    ///               screen pixels, never virtualised. The windows of the QML surfaces are
    ///               placed in device-independent pixels instead, so the view divides this by
    ///               the screen's devicePixelRatio before it anchors anything — measured at
    ///               125%: a release at physical x=275 is a window coordinate of 220.
    void selectionReleased(QPoint anchor);

    /// @brief A mouse button went down anywhere on the desktop, left or right.
    /// @param at Press position, in the same physical pixels selectionReleased() reports.
    /// @note This fires for every press on the machine while the app runs. The surfaces are
    ///       windows of their own, so a press outside one is never delivered to this process,
    ///       and the hook is the only place that can see it -- UI.md's "click outside closes"
    ///       has nothing else to stand on. A view with nothing open should ignore it cheaply.
    void pointerPressed(QPoint at);

private:
    bool installed_ = false;

    // The system thresholds and the gesture bookkeeping live in the .cpp, not here: the
    // callback Windows calls is a free function that receives no user data and cannot reach
    // a private member. Keeping them there is also what keeps windows.h out of this header.
};

}
