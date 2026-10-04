/**
 * @file app_seams_test.cpp
 * @brief The pure halves of the app seams: the notice payload's shape, and the Run entry's
 *        key and command.
 *
 * Both live in headers that hold no more than Qt Core or the standard library, so this suite
 * can reach them without linking lens_app -- which is the difference between an assertion that
 * runs on every change and one a human has to remember. The registry write itself is machine
 * state and stays a human-run case in lens_gtest_integration (TEST.md section 2).
 */

#include <string>

#include <QString>
#include <QVariantMap>
#include <gtest/gtest.h>

#include "app/autostart.h"
#include "app/notice.h"

namespace {

using lens::app::autostartCommand;
using lens::app::kAutostartRunKey;
using lens::app::kAutostartValueName;
using lens::app::kNoticeError;
using lens::app::kNoticeInfo;
using lens::app::noticePayload;

} // namespace

/// The shape is the contract the notice surface will be built against, so it is pinned here
/// rather than left to whatever the controller happens to put in the map.
TEST(NoticePayload, CarriesTitleBodyAndKindAndNothingElse)
{
    const QVariantMap notice = noticePayload(QStringLiteral("ubiquitous"),
                                             QString::fromUtf8("这张卡片没有可解释的单词"),
                                             QString::fromUtf8(kNoticeInfo));

    EXPECT_EQ(notice.size(), 3) << "a fourth key would be one no surface reads";
    EXPECT_EQ(notice.value("title").toString(), QStringLiteral("ubiquitous"));
    EXPECT_EQ(notice.value("body").toString(), QString::fromUtf8("这张卡片没有可解释的单词"));
    EXPECT_EQ(notice.value("kind").toString(), QStringLiteral("info"));
}

TEST(NoticePayload, TheTwoKindsAreDistinctAndATitleMayBeAbsent)
{
    EXPECT_STRNE(kNoticeInfo, kNoticeError);

    // No title is the contention case: nothing was selected, so there is nothing to name.
    const QVariantMap notice = noticePayload(QString(), QStringLiteral("x"), QString::fromUtf8(kNoticeError));
    EXPECT_TRUE(notice.value("title").toString().isEmpty());
    EXPECT_EQ(notice.value("kind").toString(), QString::fromUtf8(kNoticeError));
}

/// The one key the product writes outside its own document, so the path is worth a case: it is
/// read by nothing in this process and a typo in it fails silently.
TEST(Autostart, TheEntrySitsUnderTheUsersRunKey)
{
    EXPECT_STREQ(kAutostartRunKey, R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)");
    EXPECT_STREQ(kAutostartValueName, "Lens");
}

TEST(Autostart, TheCommandQuotesTheExecutablePath)
{
    // Quoted even without a space, so the form is one thing rather than two cases, and a path
    // that has spaces is never the branch nobody tried. The fixtures carry no drive letter:
    // what is being pinned is the quoting, and a drive path in a source file is what the
    // pre-commit hook's check exists to reject (config/README.md).
    EXPECT_EQ(autostartCommand(R"(Lens\lens.exe)"), R"("Lens\lens.exe")");

    // What the quoting is for: unquoted, Windows would run the first word and hand the rest
    // over as arguments.
    EXPECT_EQ(autostartCommand(R"(Program Files\Lens\lens.exe)"), R"("Program Files\Lens\lens.exe")");
}
