/**
 * @file selection_capture_test.cpp
 * @brief Checks the selection entry point: the gesture rule, the exclusion list, the hook
 *        against synthetic input, and one human-driven round trip through the real clipboard.
 *
 * Three of the four groups run unattended. The gesture rule and the exclusion list are pure
 * assertions. The hook case drives the pointer with SendInput, which a low-level hook sees
 * exactly like real input, so it proves the hook and its deferred signal for real rather than
 * through a mock. Its drag lands on a window a child process owns, because the hook ignores any
 * press over a window of its own process (the rule that keeps a drag on one of the app's own
 * surfaces from being read as a selection); the child is this same executable re-run in probe
 * mode, see main().
 *
 * The last one cannot be automated: it needs a drag over another application, so it reports a
 * skip unless LENS_HOOK_SMOKE is set and then waits for a human. What it adds is what only a
 * real application can show — that the injected Ctrl+C fetches the selection, and that the
 * reader's own clipboard content comes back afterwards.
 *
 * Windows only, like the module it covers: it creates real windows and moves the pointer.
 *
 * ClipboardSnapshot.* drives the real clipboard, which this machine shares with everything
 * else running on it. A case has been seen to fail intermittently right after another process
 * wrote the clipboard (three times in about thirty-five runs) and could not be reproduced on
 * demand; an open-clipboard retry did not stop it. So a lone failure there is worth a re-run
 * before it is read as a signal — and the cause is still unknown, which is recorded rather
 * than papered over.
 */

#include <cstring>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QTimer>
#include <gtest/gtest.h>

#include "app/mouse_selection_hook.h"
#include "app/selection_text_grabber.h"
#include "support.h"
#include "util/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace {

using lens::app::Gesture;
using lens::app::GrabStatus;
using lens::app::GrabbedText;
using lens::app::isExcludedProcess;
using lens::app::isSelectionGesture;

/// Passed explicitly rather than read from the system, so the cases do not depend on the
/// mouse settings of whichever machine runs them.
constexpr int kSlop = 4;

/// How long the human gets to make a selection once the hook is listening.
constexpr int kHookWaitMs = 20'000;

/// How long the synthesised drag gets to arrive before the case gives up on it.
constexpr int kDragWaitMs = 2'000;

/// How far the synthesised drag travels. Well past any plausible SM_CXDRAG, so the gesture
/// rule cannot reject it for the wrong reason.
constexpr int kDragPx = 40;

/// The switch that turns this executable into the probe-window child instead of a test run.
constexpr const char* kProbeWindowArg = "--lens-probe-window";

/// How long to wait for the probe child to put its window up, and how often to look.
constexpr int kProbeWindowWaitMs   = 2000;
constexpr int kProbePollIntervalMs = 20;

/// @return A name for the status, so a failure message says something a reader can act on
///         instead of a number.
const char* statusName(GrabStatus status)
{
    switch (status) {
        case GrabStatus::Captured: return "Captured";
        case GrabStatus::ForegroundIsSelf: return "ForegroundIsSelf";
        case GrabStatus::ProcessExcluded: return "ProcessExcluded";
        case GrabStatus::ClipboardBusy: return "ClipboardBusy";
        case GrabStatus::CopyTimedOut: return "CopyTimedOut";
        case GrabStatus::EmptyText: return "EmptyText";
    }
    return "unknown";
}

/// @brief Open the clipboard, retrying briefly.
///
/// Same reason the product retries: another process holding the clipboard open is normal on a
/// live desktop, and a single attempt turns that into a spurious failure. The first version
/// of these cases did not, and failed twice in a full-suite run that then would not reproduce
/// — a flake with a shared resource is a bug in the test even when the cause is outside it.
bool openClipboardRetrying()
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(nullptr) != FALSE) return true;
        Sleep(10);
    }
    return false;
}

/// @brief Put @p text on the clipboard with the raw Win32 API.
///
/// Deliberately its own code rather than the snapshot's: the sentinel has to be set
/// independently for the restore checks to mean anything. Qt's own clipboard needs a
/// QGuiApplication, which this target does not have.
bool putClipboardText(const QString& text)
{
    if (!openClipboardRetrying()) return false;

    EmptyClipboard();
    const std::wstring wide = text.toStdWString();
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        return false;
    }

    if (void* target = GlobalLock(memory)) {
        std::memcpy(target, wide.c_str(), bytes);
        GlobalUnlock(memory);
    }

    // SetClipboardData takes ownership on success and leaves it with us on failure, so the
    // free has to be conditional on the failure.
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

/// @return Whatever text the clipboard holds now, read the raw way.
std::optional<QString> readClipboardTextNow()
{
    if (!openClipboardRetrying()) return std::nullopt;

    std::optional<QString> text;
    if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        // Owned by the clipboard, so it is locked and unlocked but never freed here.
        if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(handle))) {
            text = QString::fromWCharArray(wide);
            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return text;
}

/// @brief Fill the clipboard with text and a private format, in one session.
/// @return The registered format id, or 0 when the write failed.
/// @note Both formats go on inside a single Open/EmptyClipboard/SetClipboardData session on
///       purpose. SetClipboardData is only defined once the clipboard has been emptied, so
///       adding a second format from a later session leaves it undefined whether the first
///       one survives — which is what made this case fail intermittently before, and why the
///       case now checks the setup before it starts.
unsigned putClipboardTextAndFormat(const QString& text, const char* name, const std::string& bytes)
{
    const UINT format = RegisterClipboardFormatA(name);
    if (format == 0) return 0;
    if (!openClipboardRetrying()) return 0;

    EmptyClipboard();

    // Ownership passes to the clipboard on success and stays here on failure, so each handle
    // is freed only when its own SetClipboardData failed.
    const std::wstring wide = text.toStdWString();
    HGLOBAL textMemory      = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (textMemory != nullptr) {
        if (void* target = GlobalLock(textMemory)) {
            std::memcpy(target, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(textMemory);
        }
        if (SetClipboardData(CF_UNICODETEXT, textMemory) == nullptr) GlobalFree(textMemory);
    }

    HGLOBAL markerMemory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (markerMemory != nullptr) {
        if (void* target = GlobalLock(markerMemory)) {
            std::memcpy(target, bytes.data(), bytes.size());
            GlobalUnlock(markerMemory);
        }
        if (SetClipboardData(format, markerMemory) == nullptr) GlobalFree(markerMemory);
    }

    CloseClipboard();
    return format;
}

/// @brief Put text and a palette on the clipboard, in one session.
///
/// A palette's handle is a GDI object rather than a block of memory, so it is the format the
/// snapshot is built to leave behind. A bitmap would do the same, but Windows also puts its
/// memory siblings (CF_DIB, CF_DIBV5) on the clipboard, and the snapshot copies those; a palette
/// has no such sibling, so its absence after a restore really does mean the skip happened.
///
/// Text rides along so the snapshot still has something it can copy, which is what separates "the
/// handle format was skipped" from "the snapshot copied nothing".
/// @return True when both formats went on.
bool putClipboardTextAndPalette(const QString& text)
{
    const HDC screen = GetDC(nullptr);
    if (screen == nullptr) return false;
    const HPALETTE palette = CreateHalftonePalette(screen);
    ReleaseDC(nullptr, screen);
    if (palette == nullptr) return false;

    if (!openClipboardRetrying()) {
        DeleteObject(palette);
        return false;
    }

    EmptyClipboard();

    const std::wstring wide = text.toStdWString();

    // Each handle's ownership passes to the clipboard on success and stays here on failure, so
    // every failure below has to release what it still holds before it returns.
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (memory == nullptr) {
        DeleteObject(palette);
        CloseClipboard();
        return false;
    }
    if (void* target = GlobalLock(memory)) {
        std::memcpy(target, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
    } else {
        // Handing an unlocked block to the clipboard would put garbage text on it, which is not
        // the setup the case asked for.
        GlobalFree(memory);
        DeleteObject(palette);
        CloseClipboard();
        return false;
    }
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        DeleteObject(palette);
        CloseClipboard();
        return false;
    }

    if (SetClipboardData(CF_PALETTE, palette) == nullptr) {
        DeleteObject(palette);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

/// @return The bytes of a private registered format, or nothing when it is not there.
std::optional<std::string> readClipboardPrivateFormat(unsigned format)
{
    if (!openClipboardRetrying()) return std::nullopt;

    std::optional<std::string> bytes;
    if (const HANDLE handle = GetClipboardData(format)) {
        if (const void* source = GlobalLock(handle)) {
            bytes = std::string(static_cast<const char*>(source), GlobalSize(handle));
            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return bytes;
}

/// @brief Move the pointer to @p start and drag it @p dx pixels to the right.
///
/// A low-level hook is fed synthetic events exactly like real ones, so this exercises the
/// gesture rule through the real event stream rather than calling the rule directly. The
/// callbacks arrive when the event loop next runs, which is why the caller enters one after
/// sending.
void sendDrag(POINT start, int dx)
{
    auto mouseEvent = [](DWORD flag, int moveX) {
        INPUT input{};
        input.type       = INPUT_MOUSE;
        input.mi.dx      = moveX;
        input.mi.dwFlags = flag;
        return input;
    };

    SetCursorPos(start.x, start.y);

    INPUT events[3] = {mouseEvent(MOUSEEVENTF_LEFTDOWN, 0), mouseEvent(MOUSEEVENTF_MOVE, dx), mouseEvent(MOUSEEVENTF_LEFTUP, 0)};
    SendInput(static_cast<UINT>(std::size(events)), events, sizeof(INPUT));
}

/// @brief The title prefix the probe-window child names its window with; the parent appends the
///        child's pid so the two halves can find each other without a pipe.
constexpr wchar_t kProbeTitlePrefix[] = L"Lens probe ";

/// @brief Wait for the probe child to put its window up.
/// @return The window, or nullptr when the child never made one.
HWND waitForProbeWindow(DWORD pid)
{
    const std::wstring title = kProbeTitlePrefix + std::to_wstring(pid);
    for (int waited = 0; waited < kProbeWindowWaitMs; waited += kProbePollIntervalMs) {
        if (HWND found = FindWindowW(nullptr, title.c_str())) return found;
        Sleep(kProbePollIntervalMs);
    }
    return nullptr;
}

/**
 * @brief The probe-window child, owned through a job object.
 *
 * The landing window has to belong to *another process*. The hook ignores any press over a window
 * of its own process — that is the rule that keeps a drag on one of the app's own surfaces from
 * being read as a selection — so a probe owned by the test would be dropped for the right reason
 * and the case could only ever go red. A child of this executable, re-run in probe mode, is the
 * smallest owner that is not the test.
 *
 * The child blocks in its own message loop and has no way out of its own, so the parent has to
 * take it down. A job object with KILL_ON_JOB_CLOSE is what makes that unconditional: closing the
 * handle when the case ends kills the child, and so does this process dying for any reason — a
 * failed assertion, an exception, or a crash — which an explicit TerminateProcess on the way out
 * could not cover.
 */
class ProbeChild {
public:
    ProbeChild() = default;
    ~ProbeChild()
    {
        close();
    }

    ProbeChild(const ProbeChild&)            = delete;
    ProbeChild& operator=(const ProbeChild&) = delete;

    /// @brief Start this executable in probe mode and put it in a kill-on-close job.
    /// @return True when it started; pid() then names it.
    bool start()
    {
        wchar_t executable[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) return false;

        job_ = CreateJobObjectW(nullptr, nullptr);
        if (job_ == nullptr) return false;

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0) {
            close();
            return false;
        }

        const std::wstring argument(kProbeWindowArg, kProbeWindowArg + std::strlen(kProbeWindowArg));
        std::wstring command = L"\"" + std::wstring(executable) + L"\" " + argument;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child) == 0) {
            close();
            return false;
        }
        CloseHandle(child.hThread);

        // A child that did not make it into the job is one the job cannot reap, so it is stopped
        // here rather than left to run.
        if (AssignProcessToJobObject(job_, child.hProcess) == 0) {
            TerminateProcess(child.hProcess, 0);
            CloseHandle(child.hProcess);
            close();
            return false;
        }
        CloseHandle(child.hProcess);

        pid_ = child.dwProcessId;
        return true;
    }

    /// @brief Stop the child now rather than when the case ends.
    void stop()
    {
        close();
    }

    DWORD pid() const
    {
        return pid_;
    }

private:
    void close()
    {
        if (job_ != nullptr) {
            CloseHandle(job_); // the kill-on-close limit takes the child down with the handle
            job_ = nullptr;
        }
        pid_ = 0;
    }

    HANDLE job_ = nullptr;
    DWORD pid_  = 0;
};

/**
 * @brief A window of ours that is allowed to take the foreground.
 *
 * Unlike the drag probe, this one exists to become the foreground window: the grabber has to
 * recognise it as ours and refuse. WS_EX_NOACTIVATE would defeat the purpose.
 */
HWND createFocusProbeWindow()
{
    return CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"Lens focus probe", WS_POPUP | WS_VISIBLE, 80, 80, 220, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

} // namespace

TEST(SelectionGesture, JitterInsideTheSlopIsStillAClick)
{
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100 + kSlop - 1, 200}, kSlop));
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100, 200 + kSlop - 1}, kSlop));
}

TEST(SelectionGesture, MovementAtTheSlopCounts)
{
    // The boundary is inclusive: exactly the slop is already a drag, which is the reading
    // that keeps a two-character selection from being swallowed.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100 + kSlop, 200}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200 + kSlop}, kSlop));

    // Dragging up or to the left selects just as much as dragging down or right.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100 - kSlop, 200}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200 - kSlop}, kSlop));
}

TEST(SelectionGesture, AStationaryClickNeverSelects)
{
    // Double-clicking and triple-clicking have the same stationary endpoints here. Only a drag
    // should open Lens's selection action bar, so click runs remain outside the selection path.
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100, 200}, kSlop));
}

TEST(ExcludedProcess, TerminalsAreExcluded)
{
    EXPECT_TRUE(isExcludedProcess("WindowsTerminal.exe"));
    EXPECT_TRUE(isExcludedProcess("OpenConsole.exe"));
    EXPECT_TRUE(isExcludedProcess("conhost.exe"));
    EXPECT_TRUE(isExcludedProcess("cmd.exe"));
    EXPECT_TRUE(isExcludedProcess("powershell.exe"));
    EXPECT_TRUE(isExcludedProcess("pwsh.exe"));
    EXPECT_TRUE(isExcludedProcess("wt.exe"));
}

TEST(ExcludedProcess, TheComparisonIgnoresCase)
{
    EXPECT_TRUE(isExcludedProcess("CONHOST.EXE"));
    EXPECT_TRUE(isExcludedProcess("PowerShell.EXE"));
}

TEST(ExcludedProcess, AFullImagePathIsMatchedOnItsFileName)
{
    // The fixtures carry no drive letter on purpose: what the matching rule is about is the
    // directory prefix, and a drive path written into a source file is what the pre-commit
    // hook's check exists to reject (config/README.md).
    EXPECT_TRUE(isExcludedProcess(R"(Program Files\PowerShell\7\pwsh.exe)"));
    EXPECT_FALSE(isExcludedProcess(R"(Program Files\Google\Chrome\Application\chrome.exe)"));
}

TEST(ExcludedProcess, OrdinaryApplicationsAreNotExcluded)
{
    EXPECT_FALSE(isExcludedProcess("chrome.exe"));
    EXPECT_FALSE(isExcludedProcess("notepad.exe"));
    EXPECT_FALSE(isExcludedProcess("WINWORD.EXE"));
    EXPECT_FALSE(isExcludedProcess(""));
}

TEST(ExcludedProcess, TheMatchIsExactRatherThanASubstring)
{
    // A substring rule would quietly disable the feature in any process that happens to
    // carry one of these names, so the guard is worth a case of its own.
    EXPECT_FALSE(isExcludedProcess("myconhost.exe"));
    EXPECT_FALSE(isExcludedProcess("not-pwsh.exe"));
    // The names are file names: without the extension it is a different process.
    EXPECT_FALSE(isExcludedProcess("powershell"));
}

/// The guarantee the snapshot exists for. Borrowing a reader's clipboard is only acceptable
/// because this puts it back, and the interactive case above cannot check it without a hand
/// on the mouse — which is how the first version shipped broken.
TEST(ClipboardSnapshot, PutsBackWhatItTookAfterTheClipboardChanges)
{
    ASSERT_TRUE(putClipboardText(QStringLiteral("lens-keep-me")));

    const lens::app::ClipboardSnapshot snapshot = lens::app::ClipboardSnapshot::take();
    ASSERT_TRUE(snapshot.taken()) << "the clipboard could not be read";

    // Stands in for the injected copy replacing the clipboard underneath.
    ASSERT_TRUE(putClipboardText(QStringLiteral("lens-intruder")));
    ASSERT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-intruder"));

    EXPECT_TRUE(snapshot.restore());
    EXPECT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-keep-me"))
        << "the reader's clipboard did not come back";
}

/// The reason the snapshot copies formats rather than text: a reader who had copied an image
/// or a block of formatted text must not lose it to a word lookup.
TEST(ClipboardSnapshot, PutsBackEveryFormatRatherThanJustTheText)
{
    const unsigned marker = putClipboardTextAndFormat(QStringLiteral("lens-keep-me"), "LensIntegrationMarker", "second-format");
    ASSERT_NE(marker, 0u) << "could not register or set the marker format";

    // Check the setup before blaming the snapshot: if the two formats were not on the
    // clipboard to begin with, a failure afterwards says nothing about the snapshot.
    ASSERT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-keep-me")) << "setup: the text is not there";
    ASSERT_EQ(readClipboardPrivateFormat(marker).value_or(std::string()), "second-format") << "setup: the marker is not there";

    const lens::app::ClipboardSnapshot snapshot = lens::app::ClipboardSnapshot::take();
    ASSERT_TRUE(snapshot.taken());

    ASSERT_TRUE(putClipboardText(QStringLiteral("lens-intruder")));
    EXPECT_TRUE(snapshot.restore());

    EXPECT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-keep-me"));
    EXPECT_EQ(readClipboardPrivateFormat(marker).value_or(std::string()), "second-format")
        << "a format other than the text was dropped, which is what a text-only save would do";
}

/// A format whose handle is a GDI object is skipped on purpose: copying its bytes and writing
/// them back would put a broken bitmap on the clipboard. The text beside it still comes back,
/// which is how the skip and the copy are told apart.
TEST(ClipboardSnapshot, LeavesAHandleFormatBehindRatherThanCopyingItsBytes)
{
    ASSERT_TRUE(putClipboardTextAndPalette(QStringLiteral("lens-keep-me")));
    ASSERT_TRUE(IsClipboardFormatAvailable(CF_PALETTE)) << "setup: the palette is not on the clipboard";

    const lens::app::ClipboardSnapshot snapshot = lens::app::ClipboardSnapshot::take();
    ASSERT_TRUE(snapshot.taken());

    ASSERT_TRUE(putClipboardText(QStringLiteral("lens-intruder")));
    EXPECT_TRUE(snapshot.restore());

    EXPECT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-keep-me"))
        << "the text was dropped along with the format that cannot be copied";
    EXPECT_FALSE(IsClipboardFormatAvailable(CF_PALETTE))
        << "a GDI handle was copied and written back as a byte blob";
}

/// A clipboard with nothing on it is still read successfully, and restoring that empty snapshot
/// is a no-op rather than a wipe. Content put on *after* the take is what makes the no-op
/// visible: a restore that emptied the clipboard and refilled it from nothing would remove it.
/// This is the entries-empty branch, which the never-taken case above does not reach.
TEST(ClipboardSnapshot, AnEmptySnapshotRestoresToANoOp)
{
    ASSERT_TRUE(openClipboardRetrying());
    EmptyClipboard();
    CloseClipboard();

    const lens::app::ClipboardSnapshot snapshot = lens::app::ClipboardSnapshot::take();
    ASSERT_TRUE(snapshot.taken());

    ASSERT_TRUE(putClipboardText(QStringLiteral("lens-keep-me")));
    EXPECT_TRUE(snapshot.restore());
    EXPECT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-keep-me"))
        << "restoring an empty snapshot emptied the clipboard";
}

/// Restoring a snapshot that was never taken must do nothing: it has no entries to fill an
/// emptied clipboard with, so the guard is the difference between an untouched clipboard and
/// a wiped one.
TEST(ClipboardSnapshot, ARestoreWithoutATakeLeavesTheClipboardAlone)
{
    ASSERT_TRUE(putClipboardText(QStringLiteral("lens-untouched")));

    const lens::app::ClipboardSnapshot neverTaken;
    EXPECT_TRUE(neverTaken.restore());
    EXPECT_EQ(readClipboardTextNow().value_or(QString()), QStringLiteral("lens-untouched"))
        << "restoring a snapshot that was never taken emptied the clipboard";
}

/// Everything about the hook that can be checked without a hand on the mouse. Synthetic input
/// reaches a low-level hook the same way real input does, so driving the pointer ourselves
/// tests the gesture rule and the deferred signal for real rather than through a mock.
///
/// The drag lands on a window owned by a child process. It has to: the hook drops any press over
/// a window of its own process — the rule that keeps a drag on one of the app's own surfaces from
/// being read as a selection — so a landing window the test owns would be refused for the right
/// reason and the case could only ever report "the hook reported nothing".
TEST(SelectionHook, ReportsASynthesisedDragAtItsReleasePoint)
{
    ProbeChild probe;
    ASSERT_TRUE(probe.start()) << "could not start the probe window child, error " << GetLastError();
    const HWND window = waitForProbeWindow(probe.pid());
    ASSERT_TRUE(window != nullptr) << "the probe window child never put its window up";

    RECT bounds{};
    const bool measured = GetWindowRect(window, &bounds) != 0;

    POINT original{};
    GetCursorPos(&original);

    lens::app::MouseSelectionHook hook;
    const bool installed = hook.install();

    QEventLoop loop;
    std::optional<QPoint> anchor;
    if (installed) {
        QObject::connect(&hook, &lens::app::MouseSelectionHook::selectionReleased, [&](QPoint point) {
            anchor = point;
            loop.quit();
        });
        QTimer::singleShot(kDragWaitMs, &loop, &QEventLoop::quit);

        // Sent before the loop runs: the callbacks land when the thread next pumps, which is
        // what loop.exec() does.
        if (measured) sendDrag(POINT{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2}, kDragPx);
        loop.exec();
    }

    POINT landed{};
    GetCursorPos(&landed);
    SetCursorPos(original.x, original.y);
    probe.stop();

    ASSERT_TRUE(measured) << "could not measure the probe window";
    ASSERT_TRUE(installed) << "the low-level mouse hook could not be installed";
    ASSERT_TRUE(anchor.has_value()) << "the hook reported nothing, so the gesture rule rejected a drag of " << kDragPx << " px";

    // The anchor is defined as the release position, so it is compared against where the
    // pointer actually ended up rather than against a coordinate computed here, which would
    // only be testing the test.
    EXPECT_NEAR(anchor->x(), landed.x, 2) << "the anchor should be the release position";
    EXPECT_NEAR(anchor->y(), landed.y, 2);
}

/// The one branch of the grabber that needs no second application. It has to refuse when the
/// window in front belongs to this process: injecting there would press Ctrl+C into our own
/// surfaces, and the overlay windows never take focus precisely so this cannot happen.
TEST(SelectionGrab, DeclinesWhenTheForegroundWindowIsOurs)
{
    lens::app::SelectionTextGrabber grabber;

    const HWND probe = createFocusProbeWindow();
    ASSERT_TRUE(probe != nullptr) << "could not create the probe window, error " << GetLastError();

    const HWND previous = GetForegroundWindow();
    SetForegroundWindow(probe);
    const bool oursIsInFront = GetForegroundWindow() == probe;

    const auto result = grabber.grab();

    DestroyWindow(probe);
    if (previous != nullptr) SetForegroundWindow(previous);

    if (!oursIsInFront)
        GTEST_SKIP() << "could not put a window of ours in front, so the self-check had nothing to refuse";

    ASSERT_TRUE(std::holds_alternative<GrabStatus>(result))
        << "a grab with our own window in front should have copied nothing";
    EXPECT_EQ(std::get<GrabStatus>(result), GrabStatus::ForegroundIsSelf);
}

TEST(SelectionGrab, CapturesTheSelectionAndPutsTheClipboardBack)
{
    if (qEnvironmentVariableIsEmpty("LENS_HOOK_SMOKE"))
        GTEST_SKIP() << "set LENS_HOOK_SMOKE=1 to run this one by hand, since it needs a real "
                        "drag over another window. LENS_HOOK_SMOKE_TEXT names the word to "
                        "select (default 'ubiquitous'). Pick an ordinary window: an elevated "
                        "one (Task Manager, an administrator console) drops synthetic input, "
                        "and a terminal is excluded on purpose.";

    const QString expected = qEnvironmentVariable("LENS_HOOK_SMOKE_TEXT", QStringLiteral("ubiquitous"));

    // Seed the clipboard first. The grab borrows it, so the strongest thing this case can
    // prove without a second pair of eyes is that the reader's own content came back.
    const QString sentinel = QStringLiteral("lens-sentinel-") + QString::number(QCoreApplication::applicationPid());
    ASSERT_TRUE(putClipboardText(sentinel)) << "could not seed the clipboard; another application may be holding it";

    lens::app::MouseSelectionHook hook;
    ASSERT_TRUE(hook.install()) << "the low-level mouse hook could not be installed";

    std::cout << "\n  Drag-select the word \"" << expected.toStdString()
              << "\" in another window (a browser or an editor, not this console)\n"
              << "  and release the button. Waiting up to " << kHookWaitMs / 1000 << " seconds...\n"
              << std::flush;

    QEventLoop loop;
    std::optional<QPoint> anchor;
    QObject::connect(&hook, &lens::app::MouseSelectionHook::selectionReleased, [&](QPoint point) {
        anchor = point;
        loop.quit();
    });
    QTimer::singleShot(kHookWaitMs, &loop, &QEventLoop::quit);
    loop.exec();

    // Reaching here at all is the half that no offline case can check: that a low-level hook
    // installed this way actually sees a drag that happened in someone else's window.
    ASSERT_TRUE(anchor.has_value()) << "nothing arrived within " << kHookWaitMs
                                    << " ms, so the hook never saw the gesture";
    std::cout << "  the hook saw a release at " << anchor->x() << "," << anchor->y() << "\n";

    lens::app::SelectionTextGrabber grabber;
    const auto result = grabber.grab();

    const GrabbedText* captured = std::get_if<GrabbedText>(&result);
    ASSERT_TRUE(captured != nullptr) << "the grab failed with status "
                                     << statusName(*std::get_if<GrabStatus>(&result));

    std::cout << "  captured: \"" << captured->text.toStdString() << "\"\n";

    EXPECT_EQ(captured->text, expected) << "the captured text is not what was selected";

    // Distinguish "the clipboard came back empty" from "it could not be read at all": the
    // first is the data-loss bug, the second is a lock, and they need different fixes.
    const std::optional<QString> afterwards = readClipboardTextNow();
    ASSERT_TRUE(afterwards.has_value()) << "the clipboard could not be read back at all";
    EXPECT_EQ(*afterwards, sentinel) << "the reader's clipboard did not come back";
}

/// @brief Probe-window mode: put up the window a synthesised drag lands on, then pump until the
///        parent kills this process.
///
/// Runs as a child of the test (see ReportsASynthesisedDragAtItsReleasePoint). The window is a
/// plain STATIC one, so hitting it does nothing, and WS_EX_NOACTIVATE keeps the press from
/// pulling focus off whatever the reader was doing. The title carries this pid so the parent can
/// find the window without a pipe.
int runProbeWindowHost()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const std::wstring title = kProbeTitlePrefix + std::to_wstring(GetCurrentProcessId());
    const HWND window        = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", title.c_str(), WS_POPUP | WS_VISIBLE, 80, 80, 220, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    // Nothing to find and no way to be told to quit: exit rather than block forever, so a broken
    // child does not outlive the parent that is waiting on its window.
    if (window == nullptr) return 1;

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == kProbeWindowArg) return runProbeWindowHost();

    ::testing::InitGoogleTest(&argc, argv);

    // The hook hands back the screen coordinates Windows reports to it, which are physical
    // pixels. A DPI-unaware process has its own coordinates virtualised instead, and the two
    // then disagree by the monitor's scale factor — at 125%, a drag ending at logical x=220
    // arrives as x=275. The real application is per-monitor aware (Qt arranges that for a
    // QGuiApplication), so the test declares the same thing rather than papering over the
    // difference. Has to happen before any window exists.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    SetConsoleOutputCP(CP_UTF8);
    QCoreApplication app(argc, argv); // the hook needs a message loop, not a window

    lens::log::init(spdlog::level::trace, lens::test::sourceDir() / "logs");

    return RUN_ALL_TESTS();
}
