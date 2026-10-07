/**
 * @file global_hotkey.cpp
 * @brief The Win32 registration behind GlobalHotkey.
 */

#include "global_hotkey.h"

#include <QCoreApplication>

#include "core/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among
// them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace lens::app {
namespace {

/// The one hotkey this program takes. The id comes back in WM_HOTKEY, and nothing else here
/// registers one; 'LS' in ASCII, so a report that is not ours reads as odd rather than as noise.
constexpr int kHotkeyId = 0x4C53;

/// @return The Win32 virtual key for a Qt key, or 0 for a key this program will not grab.
///
/// The set is what a trigger is made of: letters, digits, the function row and the navigation
/// keys. Everything else -- a punctuation key whose position is not the same on every layout, a
/// dead key, an IME key -- is refused rather than guessed at, and `isUsable` is what reports it.
UINT virtualKeyFor(Qt::Key const key)
{
    if (key >= Qt::Key_A and key <= Qt::Key_Z) return static_cast<UINT>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 and key <= Qt::Key_9) return static_cast<UINT>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 and key <= Qt::Key_F24) return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;
        default: return 0;
    }
}

UINT modifiersFor(Qt::KeyboardModifiers const modifiers)
{
    UINT out = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) out |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier)) out |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier)) out |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier)) out |= MOD_WIN;
    return out;
}

}

bool GlobalHotkey::isUsable(QKeySequence const& keys)
{
    if (keys.isEmpty()) return false;
    const QKeyCombination combination = keys[0];
    return virtualKeyFor(combination.key()) != 0 and (modifiersFor(combination.keyboardModifiers()) & (MOD_CONTROL | MOD_ALT)) != 0;
}

QKeySequence GlobalHotkey::defaultKeys()
{
    return QKeySequence{QStringLiteral("Ctrl+Alt+S")};
}

QKeySequence GlobalHotkey::keys() const
{
    return keys_;
}

bool GlobalHotkey::conflicted() const
{
    return installed_ and not keys_.isEmpty() and registered_.isEmpty();
}

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent), keys_(defaultKeys())
{}

GlobalHotkey::~GlobalHotkey()
{
    if (not installed_) return;
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::instance()->removeNativeEventFilter(this);
    if (not registered_.isEmpty())
        UnregisterHotKey(nullptr, kHotkeyId);
    registered_ = {};
    installed_  = false;
}

bool GlobalHotkey::install()
{
    if (installed_) return not conflicted();
    installed_ = true;
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::instance()->installNativeEventFilter(this);
    return apply();
}

bool GlobalHotkey::setKeys(QKeySequence const& keys)
{
    const QKeySequence previous = keys_;
    keys_                       = keys;
    if (apply()) return true;
    keys_ = previous;
    apply();
    return false;
}

bool GlobalHotkey::apply()
{
    if (not registered_.isEmpty()) {
        UnregisterHotKey(nullptr, kHotkeyId);
        registered_ = {};
    }
    if (not installed_ or keys_.isEmpty()) return true;
    if (not isUsable(keys_)) {
        LENS_WARN("refused the trigger '{}': it needs a letter, digit, function or navigation key beside Ctrl or Alt",
                  keys_.toString(QKeySequence::PortableText).toStdString());
        return false;
    }
    const QKeyCombination combination = keys_[0];
    const UINT modifiers              = modifiersFor(combination.keyboardModifiers());
    // MOD_NOREPEAT: a held key is one capture, not a stream of them.
    if (RegisterHotKey(nullptr, kHotkeyId, modifiers | MOD_NOREPEAT, virtualKeyFor(combination.key())) == FALSE) {
        LENS_WARN("the trigger '{}' is held by another program (error {})", keys_.toString(QKeySequence::PortableText).toStdString(), GetLastError());
        return false;
    }
    registered_ = keys_;
    LENS_INFO("OCR trigger key registered: '{}'", keys_.toString(QKeySequence::PortableText).toStdString());
    return true;
}

bool GlobalHotkey::nativeEventFilter(QByteArray const& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result)
    if (eventType != QByteArrayLiteral("windows_generic_MSG")) return false;
    const auto* const msg = static_cast<MSG*>(message);
    // A hotkey posted to this thread's queue carries no window, which is exactly why the filter
    // sees it: it is not a message any window of this process would otherwise be given.
    if (msg->message != WM_HOTKEY or msg->wParam != kHotkeyId) return false;
    emit pressed();
    return true;
}

}
