#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>

/**
 * @file global_hotkey.h
 * @brief One key combination the shell watches for, on every application and not only this one.
 *
 * A window-scoped shortcut is consulted only while this program already has the keyboard, which is
 * the one moment a capture trigger is not needed: the text to read is in whatever application the
 * reader is looking at. `RegisterHotKey` is the shell's own registry for the other case, and it
 * delivers `WM_HOTKEY` to the thread that registered -- the main thread here, which is the thread
 * that pumps messages (as `QEventDispatcherWin32::processEvents` peeks the whole queue, thread
 * messages included, and hands each one to the installed native event filters).
 *
 * @note Windows only, like the rest of src/app. This is where the Win32 keyboard calls live.
 */

namespace lens::app {

class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);

    /// @brief Drop the registration, if one is in place.
    ~GlobalHotkey() override;

    /// @brief Register the stored combination.
    /// @return True when the shell holds it. A combination another program already has is the
    ///         ordinary failure and is logged rather than thrown: the trigger is missing, the
    ///         program is not.
    /// @note Call it from the thread that pumps messages, which is the one `WM_HOTKEY` arrives on.
    bool install();

    /// @brief Put a combination in place, replacing whatever is registered.
    /// @param keys The combination, as the settings field recorded it.
    /// @return True once it is registered -- or when there is nothing to register with yet, which
    ///         is the test and offscreen case. False leaves the previous combination working, so a
    ///         refused choice costs the reader nothing.
    bool setKeys(QKeySequence const& keys);

    /// @return The combination that is set, whether or not the shell accepted it.
    QKeySequence keys() const;

    /// @return Whether a combination is set and the shell would not take it, which is the one
    ///         state a surface has something to say about: the trigger will not fire.
    /// @note Before install() there is nothing to report, and a run that never installs one -- the
    ///       test tree -- is not a reader looking at a broken setting.
    bool conflicted() const;

    /// @return Whether this program will grab @p keys at all.
    /// @note Ctrl or Alt is required. A bare letter, or Shift plus one, would take that key away
    ///       from every other application on the machine for as long as the program runs.
    static bool isUsable(QKeySequence const& keys);

    /// @return The trigger a reader who has never chosen one gets.
    static QKeySequence defaultKeys();

    /// @brief The shell's WM_HOTKEY, on the thread that registered it.
    bool nativeEventFilter(QByteArray const& eventType, void* message, qintptr* result) override;

signals:
    /// @brief The reader pressed the combination anywhere on the desktop.
    void pressed();

private:
    /// @brief Register what is stored, releasing any registration already held.
    bool apply();

    QKeySequence keys_;
    /// What the shell actually took, so that a failed replacement does not leave the field and
    /// the registration disagreeing about what is in force.
    QKeySequence registered_;
    bool installed_ = false;
};

}
