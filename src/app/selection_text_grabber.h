#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <string_view>
#include <variant>

/**
 * @file selection_text_grabber.h
 * @brief Copies the reader's selection out of whatever application owns it.
 *
 * There is no API that reads another application's selection, so this works the way a person
 * would: put the clipboard aside, press Ctrl+C into the foreground window, take what landed,
 * and put the clipboard back. Two consequences are handled here rather than left to be
 * discovered — the clipboard is borrowed, and an injected Ctrl+C is SIGINT inside a
 * terminal, so terminal processes are excluded before anything is sent.
 *
 * @note Windows only, like the rest of src/app. The clipboard snapshot is OLE's, the
 *       injection is SendInput's.
 */

namespace lens::app {

/// @brief Why one grab attempt ended the way it did.
enum class GrabStatus : std::uint8_t
{
    Captured = 0,     ///< The text came back.
    ForegroundIsSelf, ///< The foreground window belongs to this process; injecting would
                      ///< target our own surfaces, so nothing was sent.
    ProcessExcluded,  ///< The foreground process is one where Ctrl+C means "interrupt".
    ClipboardBusy,    ///< The clipboard could not be taken; without a snapshot to restore,
                      ///< clobbering it is not allowed. Also returned on re-entry.
    CopyTimedOut,     ///< The clipboard did not change before the deadline: nothing was
                      ///< selected, the application ignores synthetic input, or an elevated
                      ///< window dropped it (UIPI).
    EmptyText,        ///< The clipboard changed but carried no text.
};

/**
 * @brief Whether an injected Ctrl+C would do something destructive in this process.
 *
 * A terminal turns Ctrl+C into SIGINT, so a reader who drag-selects a word inside one would
 * lose whatever is running there. The check happens before anything is sent, in the grab
 * step, which is the only place the foreground process matters.
 *
 * @param executableName Executable name, with or without a directory, case-insensitive.
 * @return True for the terminal family listed in the implementation.
 * @note The list is a safety net, not a taxonomy. An editor with an embedded terminal, a
 *       debugger, or a REPL is not covered — their process is the host application, which
 *       cannot be recognised from its name. Extending the list is the fix if one bites.
 */
bool isExcludedProcess(std::string_view executableName);

/**
 * @brief Borrows the clipboard to copy the current selection out of the foreground window.
 *
 * The snapshot is taken with OleGetClipboard and handed back with OleSetClipboard, so every
 * format survives — a text-only save would quietly destroy a copied image or a chunk of
 * formatted HTML. If the snapshot cannot be taken the attempt stops there, because
 * clobbering a clipboard that cannot be restored is worse than doing nothing.
 */
class SelectionTextGrabber : public QObject
{
    Q_OBJECT
public:
    explicit SelectionTextGrabber(QObject* parent = nullptr);

    /// @brief Releases the OLE registration taken in the constructor, if any.
    ~SelectionTextGrabber() override;

    /**
     * @brief Copy the foreground application's selection and read it back.
     *
     * Waits for the injected Ctrl+C to land by polling the clipboard sequence number until
     * it changes or kGrabDeadlineMs passes, driving a nested event loop so Windows messages
     * — and the mouse hook — keep flowing while it waits. Takes tens of milliseconds.
     *
     * @return The copied text trimmed of surrounding whitespace, or the GrabStatus saying
     *         why there is none. The reason is logged too.
     * @note Never call this from the mouse hook callback: the wait would overrun the budget
     *       Windows gives a low-level hook. Re-entering it returns ClipboardBusy.
     */
    std::variant<QString, GrabStatus> grab();

private:
    bool oleReady_ = false;
    bool grabbing_ = false;
};

}
