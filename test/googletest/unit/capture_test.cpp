/** @file capture_test.cpp
 * @brief Offline capture policy, whitelist, stability and candidate boundaries.
 */
#include <gtest/gtest.h>

#include <climits>
#include <chrono>
#include <memory>

#include "app/mouse_selection_hook.h"
#include "capture/capture_policy.h"
#include "capture/scan_pipeline.h"
#include "support.h"

namespace {

using namespace std::chrono_literals;
using lens::capture::FilterPolicy;
using lens::capture::ScanFrame;
using lens::capture::ScanPipeline;
using lens::capture::ScanTime;
using lens::test::CoreTest;

ScanTime at(int seconds)
{
    return ScanTime{} + std::chrono::seconds{seconds};
}

ScanFrame frame(std::uint64_t pixels = 1, std::string context = "window")
{
    return {.context = std::move(context), .processName = "chrome.exe", .pixels = pixels};
}

std::unique_ptr<ScanPipeline> scanner()
{
    auto scan = lens::capture::makeScanPipeline();
    scan->setWhitelist({"chrome.exe"});
    return scan;
}

void observe(ScanPipeline& scan, int seconds, std::string_view text, std::uint64_t pixels = 1, FilterPolicy policy = {})
{
    if (scan.needsOcr(frame(pixels), at(seconds))) scan.acceptText(text, policy, at(seconds));
}

}

TEST(CapturePolicy, DragBoundaryHandlesBothDirections)
{
    EXPECT_FALSE(lens::capture::exceedsDragThreshold(-1, 0, 2));
    EXPECT_TRUE(lens::capture::exceedsDragThreshold(-2, 0, 2));
    EXPECT_TRUE(lens::capture::exceedsDragThreshold(0, 8, 8));
    EXPECT_TRUE(lens::app::isSelectionGesture({INT_MIN, 0, INT_MAX, 0}, 8));
}

TEST(CapturePolicy, ExplicitSelectionLengthCountsUnicodeLettersOnly)
{
    EXPECT_EQ(lens::capture::selectionLetterCount(QStringLiteral("ab3!")), 2);
    EXPECT_EQ(lens::capture::selectionLetterCount(QStringLiteral("word")), 4);
    EXPECT_EQ(lens::capture::selectionLetterCount(QString::fromUtf8("é中")), 2);
}

TEST_F(CoreTest, ScanRequiresWhitelistStableFramesAndTwoSecondRateLimit)
{
    auto scan = scanner();
    observe(*scan, 0, "resilience ubiquitous");
    observe(*scan, 1, "resilience ubiquitous");
    EXPECT_EQ(scan->takeBatch(at(1)).size(), 0u);
    observe(*scan, 2, "resilience ubiquitous");
    EXPECT_EQ(scan->takeBatch(at(2)).size(), 2u);
    observe(*scan, 3, "resilience ambiguous", 2);
    observe(*scan, 4, "resilience ambiguous", 2);
    observe(*scan, 5, "resilience ambiguous", 2);
    EXPECT_EQ(scan->takeBatch(at(5)).size(), 2u);
}

TEST_F(CoreTest, ContinuousMinimumLengthDoesNotLeakExplicitTokenExemption)
{
    EXPECT_EQ(lens::capture::filterProse("am cat book", {.minimumLength = 2}).size(), 3u);
    EXPECT_EQ(lens::capture::filterProse("am cat book", {.minimumLength = 5}).size(), 0u);
    EXPECT_TRUE(lens::capture::filterProse("QML Kubernetes MP3", {.minimumLength = 2}).empty());
    EXPECT_EQ(lens::capture::unsupportedEntityCount("I visited New York and QML API"), 2u);
}

TEST(CapturePolicy, ProcessGateIsExactAndFailClosed)
{
    EXPECT_TRUE(lens::capture::allowedProcess("Apps/CHROME.EXE", {"chrome.exe"}, false));
    EXPECT_FALSE(lens::capture::allowedProcess("not-chrome.exe", {"chrome.exe"}, false));
    EXPECT_FALSE(lens::capture::allowedProcess("chrome.exe", {"chrome.exe"}, true));
    EXPECT_FALSE(lens::capture::allowedProcess("", {"chrome.exe"}, false));
}
