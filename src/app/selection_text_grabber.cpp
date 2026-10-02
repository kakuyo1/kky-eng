/**
 * @file selection_text_grabber.cpp
 * @brief The terminal exclusion list, and the clipboard round trip that gets the text out.
 */

#include "selection_text_grabber.h"

#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>

#include "core/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>
#include <ole2.h>

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

/// @return @p wide as UTF-8 bytes. The comparison wants a bare name, and a name that is not
///         ASCII simply will not match anything in the list, which is the safe direction.
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

/// @return Whatever text the clipboard holds now.
std::optional<QString> readClipboardText()
{
    IDataObject* current = nullptr;
    if (OleGetClipboard(&current) != S_OK || current == nullptr) return std::nullopt;

    std::optional<QString> text;
    FORMATETC format{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (current->GetData(&format, &medium) == S_OK)
    {
        const auto* wide = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal));
        if (wide != nullptr)
        {
            text = QString::fromWCharArray(wide);
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }

    current->Release();
    return text;
}

/// @brief Hand the snapshot back, releasing the reference either way.
void restoreClipboard(IDataObject* saved)
{
    if (OleSetClipboard(saved) != S_OK)
        LENS_WARN("SelectionTextGrabber::grab: OleSetClipboard failed; the reader's clipboard was not put back");
    saved->Release();
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

SelectionTextGrabber::SelectionTextGrabber(QObject* parent) : QObject(parent)
{
    // The clipboard snapshot is OLE's. Qt's Windows platform plugin already does this for
    // the GUI thread once a QGuiApplication exists, in which case this returns S_FALSE —
    // which still demands a matching OleUninitialize, so oleReady_ balances both the same
    // way. RPC_E_CHANGED_MODE (the thread was already set up with another apartment model)
    // leaves OLE unusable here, and grab() then declines rather than clobbering anything.
    const HRESULT result = OleInitialize(nullptr);
    oleReady_ = SUCCEEDED(result);
    if (!oleReady_) LENS_WARN("SelectionTextGrabber: OleInitialize failed with 0x{:08X}", static_cast<unsigned>(result));
}

SelectionTextGrabber::~SelectionTextGrabber()
{
    if (oleReady_) OleUninitialize();
}

std::variant<QString, GrabStatus> SelectionTextGrabber::grab()
{
    if (!oleReady_)
    {
        LENS_WARN("SelectionTextGrabber::grab: OLE is unavailable on this thread");
        return GrabStatus::ClipboardBusy;
    }

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

    // Put the clipboard aside before touching it. Every format survives because the snapshot
    // is a live OLE data object rather than a copy of the text: a text-only save would
    // quietly destroy a copied image or a chunk of HTML. Failing to take it means stopping,
    // since clobbering a clipboard that cannot be restored is worse than doing nothing.
    IDataObject* saved = nullptr;
    if (OleGetClipboard(&saved) != S_OK || saved == nullptr)
    {
        LENS_WARN("SelectionTextGrabber::grab: OleGetClipboard failed; leaving the clipboard alone");
        grabbing_ = false;
        return GrabStatus::ClipboardBusy;
    }

    const DWORD sequenceBefore = GetClipboardSequenceNumber();

    if (!sendCopyKeystroke())
    {
        LENS_WARN("SelectionTextGrabber::grab: SendInput was refused, which is what an elevated foreground window does");
        restoreClipboard(saved);
        grabbing_ = false;
        return GrabStatus::CopyTimedOut;
    }

    const bool landed = waitForClipboardChange(sequenceBefore);

    // Read before restoring: OleSetClipboard puts the snapshot back over the top.
    const std::optional<QString> copied = landed ? readClipboardText() : std::nullopt;
    restoreClipboard(saved);
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
