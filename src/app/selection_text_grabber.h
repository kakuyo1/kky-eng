#pragma once

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

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
 * @note Windows only, like the rest of src/app. The clipboard work uses the raw Win32
 *       clipboard API rather than OLE's, for the reason recorded in ClipboardSnapshot.
 */

namespace lens::app {

/// @brief Why one grab attempt ended the way it did.
enum class GrabStatus : std::uint8_t {
    Captured = 0,     ///< The text came back.
    ForegroundIsSelf, ///< The foreground window belongs to this process; injecting would
                      ///< target our own surfaces, so nothing was sent.
    ProcessExcluded,  ///< The foreground process is one where Ctrl+C means "interrupt".
    ClipboardBusy,    ///< The clipboard could not be taken; without a snapshot to restore,
                      ///< clobbering it is not allowed. Also returned on re-entry.
    CopyTimedOut,     ///< Nothing came back: there was no window to copy from, the clipboard
                      ///< did not change before the deadline, the application ignored the
                      ///< synthetic input, or an elevated window dropped it (UIPI).
    EmptyText,        ///< The clipboard changed but carried no text.
};

/// @brief A grab that produced text, and what the clipboard did while it was read.
struct GrabbedText {
    QString text;                   ///< The selection, trimmed of surrounding whitespace.
    bool clipboardReplaced = false; ///< Another process wrote the clipboard after our copy
                                    ///< landed -- a clipboard manager looking at the change, or
                                    ///< the reader copying something themselves. The text is
                                    ///< still the selection, but the snapshot was left in place
                                    ///< rather than put back over the newer content, so what is
                                    ///< on the clipboard now is theirs.
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
 * @brief A copy of everything the clipboard held, kept as bytes this process owns.
 *
 * OLE looked like the right tool for this and is not: `OleSetClipboard` on an object that
 * came from `OleGetClipboard` fails with `CLIPBRD_E_CANT_CLOSE` in this process, whether or
 * not the clipboard changed in between and whether the content was put there by this process
 * or another one — measured, with `OleSetClipboard(nullptr)` succeeding in the same run, so
 * it is the object round trip that fails, not the setter. The raw API has none of that: every
 * format is read out and copied here, and written back as the clipboard is emptied and filled
 * again. A reader's copied image or formatted block therefore survives, which a text-only save
 * would quietly destroy.
 *
 * @note Some formats are handles rather than memory — bitmaps, palettes, enhanced metafiles,
 *       and the owner-display family — and cannot be copied this way. Those are skipped and
 *       logged by name, so what was dropped is never a mystery.
 */
class ClipboardSnapshot {
public:
    ClipboardSnapshot() = default;

    /**
     * @brief Copy everything the clipboard currently holds.
     * @return The snapshot. Check taken(): a snapshot that could not be read must never be
     *         restored, because restoring it would empty a clipboard it cannot refill.
     */
    static ClipboardSnapshot take();

    /// @return Whether the clipboard was read successfully.
    bool taken() const
    {
        return taken_;
    }

    /**
     * @brief Replace the clipboard with this copy.
     * @return True when every entry landed. A false return means the reader's clipboard was
     *         left short, so it is logged as a warning rather than swallowed.
     * @note Does nothing when taken() is false.
     */
    bool restore() const;

private:
    /// @brief One format and the bytes of its handle.
    struct Entry {
        unsigned format = 0;          ///< A CF_* value or a registered format id.
        std::vector<std::byte> bytes; ///< The handle's contents, copied out.
    };

    bool taken_ = false;
    std::vector<Entry> entries_;
};

/**
 * @brief Borrows the clipboard to copy the current selection out of the foreground window.
 */
class SelectionTextGrabber : public QObject {
    Q_OBJECT
public:
    explicit SelectionTextGrabber(QObject* parent = nullptr);

    /**
     * @brief Copy the foreground application's selection and read it back.
     *
     * Waits for the injected Ctrl+C to land by polling the clipboard sequence number until
     * it changes or kGrabDeadlineMs passes, driving a nested event loop so Windows messages
     * — and the mouse hook — keep flowing while it waits. Takes tens of milliseconds.
     *
     * @return The selection, or the GrabStatus saying why there is none. The reason is logged
     *         too. A successful grab also says whether the clipboard was written by another
     *         process while the selection was being read (see GrabbedText).
     * @note Never call this from the mouse hook callback: the wait would overrun the budget
     *       Windows gives a low-level hook. Re-entering it returns ClipboardBusy.
     */
    std::variant<GrabbedText, GrabStatus> grab();

private:
    bool grabbing_ = false;
};

}
