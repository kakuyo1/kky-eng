/**
 * @file app_seams_test.cpp
 * @brief The pure halves of the app seams: the notice payload's shape, the Run entry's key and
 *        command, the tray state's priority, the annual grid's window, and the settings
 *        document's path.
 *
 * Each is the side of a seam that holds no more than Qt Core or the standard library -- which
 * is the difference between an assertion that runs on every change and one a human has to
 * remember. The sides that touch the machine stay human-run cases in lens_gtest_integration
 * (TEST.md section 2): the registry write, and what the live tray icon actually draws.
 */

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include <QDate>
#include <QStandardPaths>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <gtest/gtest.h>

#include "app/autostart.h"
#include "app/notice.h"
#include "app/paths.h"
#include "app/tray_state.h"
#include "app/year_grid.h"
#include "core/stats_store.h"
#include "support.h"

namespace {

using lens::app::autostartCommand;
using lens::app::kAutostartRunKey;
using lens::app::kAutostartValueName;
using lens::app::kNoticeError;
using lens::app::kNoticeInfo;
using lens::app::noticePayload;
using lens::app::settingsPath;
using lens::app::toPath;
using lens::app::TrayState;
using lens::app::trayIconName;
using lens::app::trayState;
using lens::app::yearGrid;
using lens::core::DailyUsage;

} // namespace

/// The shape is the contract the notice surface is built against, so it is pinned here
/// rather than left to whatever the controller happens to put in the map.
TEST(NoticePayload, CarriesAReasonAndAtMostOneAction)
{
    const QVariantMap notice = noticePayload(QStringLiteral("ubiquitous"),
                                             QString::fromUtf8("这张卡片没有可解释的单词"),
                                             QString::fromUtf8(kNoticeInfo));

    EXPECT_EQ(notice.size(), 5) << "a sixth key would be one no surface reads";
    EXPECT_EQ(notice.value("title").toString(), QStringLiteral("ubiquitous"));
    EXPECT_EQ(notice.value("body").toString(), QString::fromUtf8("这张卡片没有可解释的单词"));
    EXPECT_EQ(notice.value("kind").toString(), QStringLiteral("info"));
    // Most notices are the end of the road: nothing to press, and no word to press it about.
    EXPECT_TRUE(notice.value("action").toString().isEmpty());
    EXPECT_TRUE(notice.value("lemma").toString().isEmpty());
}

/// A notice that does offer one names the word it would act on, because the surface that draws the
/// button does not know what the button means -- the side that raised the notice does.
TEST(NoticePayload, AnOfferedActionNamesWhatItActsOn)
{
    const QVariantMap notice = noticePayload(QStringLiteral("panel"),
                                             QStringLiteral("No explanation is stored for this word."),
                                             QString::fromUtf8(kNoticeInfo),
                                             QStringLiteral("Explain now"),
                                             QStringLiteral("panel"));

    EXPECT_EQ(notice.value("action").toString(), QStringLiteral("Explain now"));
    EXPECT_EQ(notice.value("lemma").toString(), QStringLiteral("panel"));
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

/// The tray state is a priority, not a sum: an exhausted budget is the one thing the reader has
/// to act on, so it outranks a request in flight and the capture switch both. Nothing behind
/// Tray's private half could assert it, which is why the rule lives in tray_state.h.
TEST(TrayState, TheExhaustedBudgetOutranksTheRestAndEveryStateNamesItsArt)
{
    EXPECT_EQ(trayState(true, true, true), TrayState::Budget);
    EXPECT_EQ(trayState(true, false, false), TrayState::Budget);
    EXPECT_EQ(trayState(false, true, true), TrayState::Busy);
    EXPECT_EQ(trayState(false, false, true), TrayState::Auto);
    EXPECT_EQ(trayState(false, false, false), TrayState::Off);

    // The name is the middle of a file name under :/icons, so a state that answers with a word
    // no art carries leaves the shell drawing a blank slot rather than saying anything.
    EXPECT_STREQ(trayIconName(TrayState::Budget), "budget");
    EXPECT_STREQ(trayIconName(TrayState::Busy), "busy");
    EXPECT_STREQ(trayIconName(TrayState::Auto), "auto");
    EXPECT_STREQ(trayIconName(TrayState::Off), "off");
}

/// @return The grid's entry for @p date, or an empty map when the window does not carry it.
QVariantMap gridDay(const QVariantList& grid, const QString& date)
{
    for (const QVariant& day : grid) {
        if (day.toMap().value("date").toString() == date)
            return day.toMap();
    }
    return {};
}

/// The window is fixed at 365 days ending on the day it is handed, which is the reason the
/// projection left AppController: there the date came from the clock, so every assertion was
/// about whichever day the suite happened to run on.
TEST(YearGrid, IsThreeHundredAndSixtyFiveDaysEndingOnTheDayItIsGiven)
{
    const QVariantList grid = yearGrid(QDate(2026, 3, 1), {});
    ASSERT_EQ(grid.size(), 365);

    // Read back through QDate rather than written out: the first day is 364 days before the
    // last, and that arithmetic is the assertion.
    const QString first = grid.first().toMap().value("date").toString();
    EXPECT_EQ(QDate::fromString(first, QStringLiteral("yyyy-MM-dd")).addDays(364), QDate(2026, 3, 1));
    EXPECT_EQ(grid.last().toMap().value("date").toString(), QStringLiteral("2026-03-01"));

    // A year with nothing in it is 365 squares, not an empty surface.
    EXPECT_EQ(grid.first().toMap().value("pops").toInt(), 0);
    EXPECT_EQ(grid.last().toMap().value("pops").toInt(), 0);
}

TEST(YearGrid, CarriesTheLeapDayWhenTheWindowReachesOne)
{
    // A year back from March 2024 crosses 2024-02-29. The date is what the surface keys the
    // square on, so a day the window skipped is a day the reader cannot be shown.
    const QVariantList grid = yearGrid(QDate(2024, 3, 1), {});
    EXPECT_FALSE(gridDay(grid, QStringLiteral("2024-02-29")).isEmpty());
    EXPECT_EQ(gridDay(grid, QStringLiteral("2024-02-29")).value("pops").toInt(), 0);
}

TEST(YearGrid, SpendsTheTalliesOfTheOneDayItHasAndLeavesTheRestAtZero)
{
    std::map<std::string, DailyUsage> daily;
    daily["2026-02-14"] = DailyUsage{42, 3, 5};
    daily["2026-02-20"] = DailyUsage{1, 0, 1};

    const QVariantList grid = yearGrid(QDate(2026, 3, 1), daily);

    const QVariantMap heaviest = gridDay(grid, QStringLiteral("2026-02-14"));
    EXPECT_EQ(heaviest.value("pops").toInt(), 42);
    EXPECT_EQ(heaviest.value("learned").toInt(), 3);
    EXPECT_EQ(heaviest.value("fresh").toInt(), 5);

    // The days around it are squares of their own, and a day with no record is zero rather than
    // missing: the surface draws the whole window from this list and from nothing else.
    EXPECT_EQ(gridDay(grid, QStringLiteral("2026-02-20")).value("pops").toInt(), 1);
    EXPECT_EQ(gridDay(grid, QStringLiteral("2026-02-15")).value("pops").toInt(), 0);

    // A tally the window does not reach is not in it.
    EXPECT_TRUE(gridDay(grid, QStringLiteral("2026-03-02")).isEmpty());
}

/// Test mode is process-wide, so it is put back: this binary has other suites, and none of them
/// should inherit a redirected profile.
struct ProfileScope {
    ProfileScope()
    {
        QStandardPaths::setTestModeEnabled(true);
    }
    ~ProfileScope()
    {
        QStandardPaths::setTestModeEnabled(false);
    }
};

/// @return @p path's contents, for the carry-over case below.
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The boundary: the document carries the reader's API key and word marks, so it belongs to the
/// profile -- one per account -- and never to the install tree, which every account shares and
/// which may be read-only under Program Files.
TEST(SettingsPath, LivesInTheProfileAndNeverInTheInstallTree)
{
    const ProfileScope scope;
    const std::filesystem::path target = settingsPath({});

    EXPECT_EQ(target.filename(), "settings.json");
    // Pinned to the platform's profile location, which is the assertion the section is about:
    // the install tree is never the profile location, so a resolver that reached for the
    // executable's own directory fails here.
    EXPECT_EQ(target.parent_path(), toPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));

    // Said the other way round as well, against the tree this binary was built from: there is no
    // QCoreApplication here, so QCoreApplication::applicationDirPath() would answer with an empty
    // string and the comparison would hold for the wrong reason.
    const std::filesystem::path under = target.parent_path().lexically_relative(lens::test::sourceDir());
    EXPECT_TRUE(under.empty() || under.string().starts_with(".."))
        << "the document resolved under the tree it was built from: " << target.string();

    // Documented, and what the caller relies on: the directory is there when it returns.
    EXPECT_TRUE(std::filesystem::is_directory(target.parent_path()));

    std::filesystem::remove_all(target.parent_path());
}

/// The other half of the same rule: settings.local.json is a development source, carried into
/// the profile once so a key already configured keeps working -- and then never again, or a
/// later launch would put the repository's copy back over the reader's own.
TEST(SettingsPath, CarriesTheLegacyDocumentOverOnceAndOnlyOnce)
{
    const ProfileScope scope;
    const std::filesystem::path profile = settingsPath({});
    std::filesystem::remove(profile);

    const std::filesystem::path legacy = std::filesystem::temp_directory_path() / "lens-legacy-settings.json";
    {
        std::ofstream out(legacy);
        out << R"({"API-KEY":"from-the-repo"})";
    }

    EXPECT_EQ(settingsPath(legacy), profile);
    EXPECT_NE(readFile(profile).find("from-the-repo"), std::string::npos);

    {
        std::ofstream out(profile);
        out << R"({"API-KEY":"the-readers"})";
    }
    settingsPath(legacy);

    const std::string carried = readFile(profile);
    EXPECT_NE(carried.find("the-readers"), std::string::npos);
    EXPECT_EQ(carried.find("from-the-repo"), std::string::npos)
        << "the carry-over ran again over a document the reader has since written";

    std::filesystem::remove(legacy);
    std::filesystem::remove_all(profile.parent_path());
}
