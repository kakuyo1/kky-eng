/**
 * @file selection_capture_test.cpp
 * @brief Checks the selection entry point: the gesture rule, the exclusion list, the hook
 *        against synthetic input, and one human-driven round trip through the real clipboard.
 *
 * Three of the four groups run unattended. The gesture rule and the exclusion list are pure
 * assertions. The hook case drives the pointer with SendInput, which a low-level hook sees
 * exactly like real input, so it proves the hook and its deferred signal for real rather than
 * through a mock.
 *
 * The last one cannot be automated: it needs a drag over another application, so it reports a
 * skip unless LENS_HOOK_SMOKE is set and then waits for a human. What it adds is what only a
 * real application can show — that the injected Ctrl+C fetches the selection, and that the
 * reader's own clipboard content comes back afterwards.
 *
 * Windows only, like the module it covers: it creates real windows and moves the pointer.
 */

#include <cstring>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QTimer>
#include <gtest/gtest.h>

#include "app/mouse_selection_hook.h"
#include "app/selection_text_grabber.h"
#include "core/log.h"
#include "support.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace {

using lens::app::Gesture;
using lens::app::GrabStatus;
using lens::app::isExcludedProcess;
using lens::app::isSelectionGesture;

/// Passed explicitly rather than read from the system, so the cases do not depend on the
/// mouse settings of whichever machine runs them.
constexpr int kSlop = 4;

/// How long the human gets to make a selection once the hook is listening.
constexpr int kHookWaitMs = 20'000;

/// How far the synthesised drag travels. Well past any plausible SM_CXDRAG, so the gesture
/// rule cannot reject it for the wrong reason.
constexpr int kDragPx = 40;

/// @return A name for the status, so a failure message says something a reader can act on
///         instead of a number.
const char* statusName(GrabStatus status)
{
    switch (status)
    {
        case GrabStatus::Captured: return "Captured";
        case GrabStatus::ForegroundIsSelf: return "ForegroundIsSelf";
        case GrabStatus::ProcessExcluded: return "ProcessExcluded";
        case GrabStatus::ClipboardBusy: return "ClipboardBusy";
        case GrabStatus::CopyTimedOut: return "CopyTimedOut";
        case GrabStatus::EmptyText: return "EmptyText";
    }
    return "unknown";
}

/// @brief Put @p text on the clipboard with the raw Win32 API.
///
/// Deliberately not the grabber's own OLE path: the sentinel has to be set independently for
/// the restore check to mean anything. Qt's own clipboard needs a QGuiApplication, which
/// this target does not have.
bool putClipboardText(const QString& text)
{
    if (OpenClipboard(nullptr) == FALSE) return false;

    EmptyClipboard();
    const std::wstring wide = text.toStdWString();
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr)
    {
        CloseClipboard();
        return false;
    }

    if (void* target = GlobalLock(memory))
    {
        std::memcpy(target, wide.c_str(), bytes);
        GlobalUnlock(memory);
    }

    // SetClipboardData takes ownership on success and leaves it with us on failure, so the
    // free has to be conditional on the failure.
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
    {
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
        input.type = INPUT_MOUSE;
        input.mi.dx = moveX;
        input.mi.dwFlags = flag;
        return input;
    };

    SetCursorPos(start.x, start.y);

    INPUT events[3] = {mouseEvent(MOUSEEVENTF_LEFTDOWN, 0), mouseEvent(MOUSEEVENTF_MOVE, dx), mouseEvent(MOUSEEVENTF_LEFTUP, 0)};
    SendInput(static_cast<UINT>(std::size(events)), events, sizeof(INPUT));
}

/**
 * @brief A small topmost window for the synthesised drag to land on.
 *
 * A synthetic click has to go somewhere, and letting it find the reader's own window is how
 * a test deletes somebody's file. This one belongs to the test, so hitting it does nothing,
 * and WS_EX_NOACTIVATE keeps it from taking focus from whatever the reader was doing.
 *
 * The system's own STATIC class is used so no window class has to be registered, and the
 * caller destroys it.
 */
HWND createProbeWindow()
{
    return CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Lens probe",
                           WS_POPUP | WS_VISIBLE, 80, 80, 220, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/// @brief Move the pointer back and get rid of the probe window.
void dismissProbe(HWND probe, POINT original)
{
    SetCursorPos(original.x, original.y);
    if (probe != nullptr) DestroyWindow(probe);
}

/**
 * @brief A window of ours that is allowed to take the foreground.
 *
 * Unlike the drag probe, this one exists to become the foreground window: the grabber has to
 * recognise it as ours and refuse. WS_EX_NOACTIVATE would defeat the purpose.
 */
HWND createFocusProbeWindow()
{
    return CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"Lens focus probe",
                           WS_POPUP | WS_VISIBLE, 80, 80, 220, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

}   // namespace

TEST(SelectionGesture, AStillClickIsNotASelection)
{
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100, 200, 1}, kSlop));
}

TEST(SelectionGesture, JitterInsideTheSlopIsStillAClick)
{
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100 + kSlop - 1, 200, 1}, kSlop));
    EXPECT_FALSE(isSelectionGesture(Gesture{100, 200, 100, 200 + kSlop - 1, 1}, kSlop));
}

TEST(SelectionGesture, MovementAtTheSlopCounts)
{
    // The boundary is inclusive: exactly the slop is already a drag, which is the reading
    // that keeps a two-character selection from being swallowed.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100 + kSlop, 200, 1}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200 + kSlop, 1}, kSlop));

    // Dragging up or to the left selects just as much as dragging down or right.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100 - kSlop, 200, 1}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200 - kSlop, 1}, kSlop));
}

TEST(SelectionGesture, LaterClicksOfARunSelectWithoutMoving)
{
    // This is the common case for a single word, and the pointer never moves, so the
    // distance test alone would never see it.
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200, 2}, kSlop));
    EXPECT_TRUE(isSelectionGesture(Gesture{100, 200, 100, 200, 3}, kSlop));
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
    EXPECT_TRUE(isExcludedProcess(R"(C:\Program Files\PowerShell\7\pwsh.exe)"));
    EXPECT_FALSE(isExcludedProcess(R"(C:\Program Files\Google\Chrome\Application\chrome.exe)"));
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

/// Everything about the hook that can be checked without a hand on the mouse. Synthetic input
/// reaches a low-level hook the same way real input does, so driving the pointer ourselves
/// tests the gesture rule and the deferred signal for real rather than through a mock.
TEST(SelectionHook, ReportsASynthesisedDragAtItsReleasePoint)
{
    const HWND probe = createProbeWindow();
    ASSERT_TRUE(probe != nullptr) << "could not create the probe window, error " << GetLastError();

    RECT bounds{};
    const bool measured = GetWindowRect(probe, &bounds) != 0;

    POINT original{};
    GetCursorPos(&original);

    lens::app::MouseSelectionHook hook;
    const bool installed = hook.install();

    QEventLoop loop;
    std::optional<QPoint> anchor;
    if (installed)
    {
        QObject::connect(&hook, &lens::app::MouseSelectionHook::selectionReleased, [&](QPoint point) {
            anchor = point;
            loop.quit();
        });
        QTimer::singleShot(2'000, &loop, &QEventLoop::quit);

        // Sent before the loop runs: the callbacks land when the thread next pumps, which is
        // what loop.exec() does.
        if (measured) sendDrag(POINT{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2}, kDragPx);
        loop.exec();
    }

    POINT landed{};
    GetCursorPos(&landed);
    dismissProbe(probe, original);

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

    const QString* text = std::get_if<QString>(&result);
    ASSERT_TRUE(text != nullptr) << "the grab failed with status "
                                 << statusName(*std::get_if<GrabStatus>(&result));

    std::cout << "  captured: \"" << text->toStdString() << "\"\n";

    EXPECT_EQ(*text, expected) << "the captured text is not what was selected";
    EXPECT_EQ(readClipboardTextNow().value_or(QString()), sentinel)
        << "the reader's clipboard did not come back";
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // The hook hands back the screen coordinates Windows reports to it, which are physical
    // pixels. A DPI-unaware process has its own coordinates virtualised instead, and the two
    // then disagree by the monitor's scale factor — at 125%, a drag ending at logical x=220
    // arrives as x=275. The real application is per-monitor aware (Qt arranges that for a
    // QGuiApplication), so the test declares the same thing rather than papering over the
    // difference. Has to happen before any window exists.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    SetConsoleOutputCP(CP_UTF8);
    QCoreApplication app(argc, argv);   // the hook needs a message loop, not a window

    lens::log::init(spdlog::level::trace, lens::test::sourceDir() / "logs");

    return RUN_ALL_TESTS();
}
