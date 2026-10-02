/**
 * @file selection_text_grabber.cpp
 * @brief The terminal exclusion list, the clipboard snapshot, and the Ctrl+C round trip.
 */

#include "selection_text_grabber.h"

#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>

#include "core/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace lens::app {
namespace {

/// How long to wait for the injected Ctrl+C to land. An application that copies on a worker
/// thread — a browser, say — takes tens of milliseconds. The wait runs inside a nested event
/// loop, so the cost of raising this is latency on the bar, not a frozen UI; this is the
/// first number to raise if a slow application is missed.
constexpr int kGrabDeadlineMs = 250;

/// How often the wait looks at the clipboard. Small enough to catch a fast application,
/// large enough not to spin.
constexpr int kPollIntervalMs = 5;

/// How many times to try opening the clipboard, and how long between tries.
constexpr int kClipboardAttempts = 5;
constexpr int kClipboardRetryMs = 10;

/**
 * @brief Open the clipboard, retrying briefly.
 *
 * OpenClipboard fails whenever any other process holds it open, which on a live desktop
 * happens for milliseconds at a time — the clipboard history service indexing a copy, another
 * application pasting, a clipboard manager looking at what changed. A single attempt turns
 * those into a spurious "the clipboard is busy" and the feature silently does nothing.
 *
 * @return True when the clipboard is open; the caller owns it until CloseClipboard().
 * @note Blocking, up to kClipboardAttempts * kClipboardRetryMs. ponytail: on the calling
 *       thread, because the grab already runs there; revisit if it is ever felt.
 */
bool openClipboardWithRetry()
{
    for (int attempt = 0; attempt < kClipboardAttempts; ++attempt)
    {
        if (OpenClipboard(nullptr) != FALSE) return true;
        Sleep(kClipboardRetryMs);
    }
    return false;
}

/**
 * Processes where Ctrl+C interrupts instead of copying. Compared against the executable's
 * file name, lower-cased, so these have to be the real names: wt.exe is only the launcher,
 * WindowsTerminal.exe is the process holding the console, and some installs put
 * OpenConsole.exe in front of conhost.
 */
constexpr std::array<std::string_view, 7> kExcludedProcesses{
    "windowsterminal.exe",
    "openconsole.exe",
    "conhost.exe",
    "cmd.exe",
    "powershell.exe",
    "pwsh.exe",
    "wt.exe",
};

/**
 * Formats whose clipboard handle is a GDI or display handle rather than memory, so their
 * contents cannot be copied by reading a block out of the handle. Everything else is treated
 * as memory and copied; a format whose handle turns out not to be readable is skipped on the
 * spot and named in the log.
 */
bool isHandleFormat(UINT format)
{
    switch (format)
    {
        case CF_BITMAP:
        case CF_PALETTE:
        case CF_ENHMETAFILE:
        case CF_OWNERDISPLAY:
            return true;
        default:
            return format >= CF_DSPTEXT && format <= CF_DSPENHMETAFILE;
    }
}

/// @return A name for @p format, for the log: the registered name when it has one, the
///         number otherwise.
std::string formatName(UINT format)
{
    wchar_t name[128];
    const int length = GetClipboardFormatNameW(format, name, static_cast<int>(std::size(name)));
    if (length <= 0) return std::to_string(format);

    char narrow[256];
    const int written = WideCharToMultiByte(CP_UTF8, 0, name, length, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    if (written <= 0) return std::to_string(format);
    narrow[written] = '\0';
    return narrow;
}

/// @return @p wide as UTF-8 bytes. A name that is not ASCII simply will not match anything in
///         the exclusion list, which is the safe direction.
std::string narrow(const std::wstring& wide)
{
    if (wide.empty()) return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};

    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
    return out;
}

/// @return The foreground window's executable name, or an empty string when it cannot be
///         read. An elevated process refuses the query; nothing then matches the exclusion
///         list, so the attempt proceeds and the injection is dropped by UIPI instead, which
///         surfaces as a timeout rather than as the wrong kind of failure.
std::string foregroundProcessName(HWND window)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0) return {};

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return {};

    // A generous buffer because long install paths are real; only the tail is used.
    wchar_t imagePath[4096];
    DWORD length = static_cast<DWORD>(std::size(imagePath));
    std::string name;
    if (QueryFullProcessImageNameW(process, 0, imagePath, &length) != 0) name = narrow(imagePath);
    CloseHandle(process);
    return name;
}

/// @brief Press Ctrl+C into whatever window has focus.
/// @return False when the system refused the injection outright.
/// @note The Ctrl pair is skipped when the reader is already holding Ctrl. Injecting a
///       key-up for a modifier someone is holding physically is how a stuck key happens, and
///       a physically held Ctrl already makes the C press mean Ctrl+C.
bool sendCopyKeystroke()
{
    const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    INPUT inputs[4] = {};
    std::size_t count = 0;
    if (!ctrlHeld)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        ++count;
    }
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = 'C';
    ++count;
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = 'C';
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    ++count;
    if (!ctrlHeld)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }

    return SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT)) == count;
}

/// @brief Poll the clipboard's sequence number until it moves or the deadline passes.
/// @note Runs a nested event loop, so Windows messages — the mouse hook among them — keep
///       flowing while it waits. Windows offers no clipboard-changed notification without a
///       window of your own, and the sequence number is what the clipboard itself provides;
///       ponytail: a window with AddClipboardFormatListener if the poll ever shows up.
bool waitForClipboardChange(DWORD sequenceBefore)
{
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(kPollIntervalMs);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, sequenceBefore] {
        if (GetClipboardSequenceNumber() != sequenceBefore) loop.quit();
    });
    QTimer::singleShot(kGrabDeadlineMs, &loop, &QEventLoop::quit);

    poll.start();
    loop.exec();
    return GetClipboardSequenceNumber() != sequenceBefore;
}

/// @return The text the clipboard holds now, or nothing when it holds none.
std::optional<QString> readClipboardText()
{
    if (OpenClipboard(nullptr) == FALSE) return std::nullopt;

    std::optional<QString> text;
    if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT))
    {
        // Owned by the clipboard, so it is locked and unlocked but never freed here.
        if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(handle)))
        {
            text = QString::fromWCharArray(wide);
            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return text;
}

}   // namespace

bool isExcludedProcess(std::string_view executableName)
{
    // Callers pass whatever the process query returned, and a full image path is the normal
    // case, so match on the last component rather than insisting on a bare name.
    const std::size_t separator = executableName.find_last_of("\\/");
    const std::string_view base = separator == std::string_view::npos ? executableName : executableName.substr(separator + 1);

    std::string lowered(base);
    const auto toLower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), toLower);

    // Exact match, never a substring: a process that merely has "conhost" inside its name is
    // not a terminal, and excluding it would silently kill the feature there.
    for (const std::string_view candidate : kExcludedProcesses)
    {
        if (candidate == lowered) return true;
    }
    return false;
}

ClipboardSnapshot ClipboardSnapshot::take()
{
    ClipboardSnapshot snapshot;

    // Failure here is the one case where the caller must not go on: emptying a clipboard that
    // could not be read destroys it with nothing to put back.
    if (!openClipboardWithRetry())
    {
        LENS_WARN("ClipboardSnapshot::take: OpenClipboard failed with error {}", GetLastError());
        return snapshot;
    }

    for (UINT format = 0; (format = EnumClipboardFormats(format)) != 0;)
    {
        if (isHandleFormat(format))
        {
            LENS_TRACE("ClipboardSnapshot::take: '{}' is a handle, not memory; leaving it behind", formatName(format));
            continue;
        }

        const HANDLE handle = GetClipboardData(format);
        if (handle == nullptr) continue;

        // Read it out here rather than holding the handle: a handle on the clipboard belongs
        // to the clipboard, and is freed the moment anything replaces its contents.
        const SIZE_T size = GlobalSize(handle);

        // GetClipboardData renders on demand for a delayed format, so this also forces the
        // source application to hand its data over before it loses the chance.
        const void* source = GlobalLock(handle);
        if (source == nullptr || size == 0)
        {
            if (source != nullptr) GlobalUnlock(handle);
            LENS_WARN("ClipboardSnapshot::take: cannot read '{}'; leaving it behind", formatName(format));
            continue;
        }

        Entry entry;
        entry.format = format;
        entry.bytes.resize(size);
        std::memcpy(entry.bytes.data(), source, size);
        GlobalUnlock(handle);

        snapshot.entries_.push_back(std::move(entry));
    }

    CloseClipboard();
    snapshot.taken_ = true;
    LENS_TRACE("ClipboardSnapshot::take: {} formats copied", snapshot.entries_.size());
    return snapshot;
}

bool ClipboardSnapshot::restore() const
{
    if (!taken_) return true;
    if (entries_.empty()) return true;

    if (!openClipboardWithRetry())
    {
        LENS_WARN("ClipboardSnapshot::restore: OpenClipboard failed with error {}; the reader's clipboard is gone", GetLastError());
        return false;
    }

    EmptyClipboard();

    bool complete = true;
    for (const Entry& entry : entries_)
    {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, entry.bytes.size());
        if (memory == nullptr)
        {
            LENS_WARN("ClipboardSnapshot::restore: cannot allocate {} bytes for '{}'", entry.bytes.size(), formatName(entry.format));
            complete = false;
            continue;
        }

        if (void* target = GlobalLock(memory))
        {
            std::memcpy(target, entry.bytes.data(), entry.bytes.size());
            GlobalUnlock(memory);
        }

        // SetClipboardData takes ownership on success and leaves it with us on failure.
        if (SetClipboardData(entry.format, memory) == nullptr)
        {
            GlobalFree(memory);
            LENS_WARN("ClipboardSnapshot::restore: SetClipboardData failed for '{}'", formatName(entry.format));
            complete = false;
        }
    }

    CloseClipboard();
    return complete;
}

SelectionTextGrabber::SelectionTextGrabber(QObject* parent) : QObject(parent) {}

std::variant<QString, GrabStatus> SelectionTextGrabber::grab()
{
    if (grabbing_)
    {
        // The nested event loop in waitForClipboardChange pumps messages, so the mouse hook
        // can fire from inside a grab. Refusing the re-entry is what keeps the snapshot of
        // the outer call intact.
        LENS_WARN("SelectionTextGrabber::grab: already grabbing; ignoring the re-entry");
        return GrabStatus::ClipboardBusy;
    }

    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
    {
        LENS_TRACE("SelectionTextGrabber::grab: no foreground window to copy from");
        return GrabStatus::CopyTimedOut;
    }

    DWORD ownerPid = 0;
    GetWindowThreadProcessId(foreground, &ownerPid);
    if (ownerPid == GetCurrentProcessId())
    {
        // The overlay surfaces never take focus (PHASE1.md section 4.4), so this should not
        // happen; if it ever does, injecting would press Ctrl+C into our own UI.
        LENS_TRACE("SelectionTextGrabber::grab: the foreground window is ours");
        return GrabStatus::ForegroundIsSelf;
    }

    const std::string processName = foregroundProcessName(foreground);
    if (isExcludedProcess(processName))
    {
        LENS_INFO("SelectionTextGrabber::grab: '{}' is excluded; Ctrl+C there is an interrupt", processName);
        return GrabStatus::ProcessExcluded;
    }

    grabbing_ = true;

    // Put the clipboard aside before touching it. Failing to take it means stopping, since
    // clobbering a clipboard that cannot be restored is worse than doing nothing.
    const ClipboardSnapshot snapshot = ClipboardSnapshot::take();
    if (!snapshot.taken())
    {
        LENS_WARN("SelectionTextGrabber::grab: no snapshot of the clipboard; leaving it alone");
        grabbing_ = false;
        return GrabStatus::ClipboardBusy;
    }

    const DWORD sequenceBefore = GetClipboardSequenceNumber();

    if (!sendCopyKeystroke())
    {
        LENS_WARN("SelectionTextGrabber::grab: SendInput was refused, which is what an elevated foreground window does");
        snapshot.restore();
        grabbing_ = false;
        return GrabStatus::CopyTimedOut;
    }

    const bool landed = waitForClipboardChange(sequenceBefore);

    // Read before restoring: the restore empties the clipboard and fills it again.
    const std::optional<QString> copied = landed ? readClipboardText() : std::nullopt;
    if (!snapshot.restore())
        LENS_WARN("SelectionTextGrabber::grab: the reader's clipboard could not be put back in full");
    grabbing_ = false;

    if (!landed)
    {
        LENS_TRACE("SelectionTextGrabber::grab: the clipboard did not change within {} ms", kGrabDeadlineMs);
        return GrabStatus::CopyTimedOut;
    }
    if (!copied)
    {
        LENS_WARN("SelectionTextGrabber::grab: the clipboard changed but carried no text");
        return GrabStatus::EmptyText;
    }

    const QString text = copied->trimmed();
    if (text.isEmpty()) return GrabStatus::EmptyText;

    LENS_INFO("SelectionTextGrabber::grab: captured {} characters from '{}'", text.size(), processName);
    return text;
}

}
