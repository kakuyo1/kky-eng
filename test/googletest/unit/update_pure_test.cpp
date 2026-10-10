/**
 * @file update_pure_test.cpp
 * @brief Offline tests for the update module: the release answer, the date and skip rules, the
 *        two guards on the request, and what the duty writes.
 *
 * The dates here are arguments rather than the clock, which is the whole reason the rules live
 * in update_pure.h: a once-a-day rule that can only be exercised today can only be exercised
 * once.
 *
 * The duty's cases drive it through a scripted network manager -- the seam LlmClient takes --
 * so nothing reaches a socket and a request count is a plain integer.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <memory>

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include "app/storage_duty.h"
#include "app/update_duty.h"
#include "core/known_store.h"
#include "update/update_client.h"
#include "update/update_pure.h"

using lens::update::parseRelease;
using lens::update::shouldCheckAutomatically;
using lens::update::shouldPrompt;
using lens::update::StoredUpdate;

namespace {

struct QtApplication {
    int argc      = 1;
    char name[24] = "update_pure_test";
    char* argv[2] = {name, nullptr};
    QCoreApplication application{argc, argv};
};

/// The page every usable answer names, as one string so a raw string delimiter never has to
/// survive a JSON fragment that ends in a quote.
const char* const kPageUrl = "https://github.com/kakuyo1/lens/releases/tag/v1.2.0";

/// @return A release answer with @p fields appended to the two required ones.
QByteArray release(const char* fields)
{
    return QByteArray("{\"tag_name\":\"v1.2.0\",\"html_url\":\"") + kPageUrl + "\"" + fields + "}";
}

/// @return A release answer whose tag is @p tag.
QByteArray releaseWithTag(const char* tag)
{
    const QByteArray head = "{\"tag_name\":\"";
    const QByteArray tail = "\",\"html_url\":\"" + QByteArray(kPageUrl) + "\"}";
    return head + QByteArray(tag) + tail;
}

/// A reply that finishes on the next turn of the loop, so the client's own connections exist
/// before `finished` arrives. `abort()` is what an answer that outgrew its ceiling does: a
/// cancelled reply, which the client has to read as offline.
///
/// It carries no body, and cannot: QNetworkReply reads from an internal buffer that only the
/// networking stack fills, so a stub cannot put bytes in a reader's hands. What it does carry
/// is the request count and the abort, which are what the two guards here are about; a release
/// answer is read through parseRelease() above, and reaches the duty on the signal the client
/// emits when it has parsed one.
class ScriptedReply final : public QNetworkReply {
public:
    ScriptedReply(QObject* parent, int status)
        : QNetworkReply(parent), status_(status)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status_);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { deliver(); });
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("the answer outgrew its ceiling"));
        deliver();
    }

    qint64 readData(char*, qint64) override
    {
        return -1;
    }

    /// Finish once, whichever of the two paths arrives first: a reply that finishes twice is
    /// not a shape Qt produces, and the counts below are only meaningful if it does not.
    void deliver()
    {
        if (delivered_)
            return;
        delivered_ = true;
        setFinished(true);
        emit finished();
    }

private:
    int status_;
    bool delivered_ = false;
};

/// The seam LlmClient takes, counting what it was asked for.
class ScriptedManager final : public QNetworkAccessManager {
public:
    explicit ScriptedManager(int status = 200)
        : status_(status)
    {
    }

    int requests = 0;

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest&, QIODevice*) override
    {
        ++requests;
        return new ScriptedReply(this, status_);
    }

private:
    int status_;
};

/// One turn of the loop, which is when a scripted answer arrives.
void settle()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

const QDate pinnedDate()
{
    return QDate::fromString(QStringLiteral("2026-10-06"), Qt::ISODate);
}

/// One duty over a temporary document, plus the client it asks through.
///
/// The client's own request goes to the scripted manager, which counts it and answers with a
/// bodyless reply: a stub cannot hand a reader bytes, so what a case needs the client's answer
/// for, it delivers on the signal the client emits once it has parsed one. The parse itself is
/// parseRelease()'s own cases above.
struct DutyFixture {
    DutyFixture(int status = 200)
        : path(std::filesystem::temp_directory_path() / "lens_update_duty_test.json")
    {
        std::filesystem::remove(path);
        store   = std::make_unique<lens::core::KnownStore>(lens::core::KnownStore::load(path));
        storage = std::make_unique<lens::app::StorageDuty>(*store);
        lens::update::Settings settings;
        settings.source        = QUrl{QStringLiteral("https://api.github.com/repos/kakuyo1/lens/releases/latest")};
        settings.maxNotesChars = 200;
        settings.maxBytes      = 2 * 1024 * 1024;
        client                 = std::make_unique<lens::update::UpdateClient>(settings, &manager);
        duty                   = std::make_unique<lens::app::UpdateDuty>(*storage, *client, QStringLiteral("1.1.0"), [] { return pinnedDate(); });
    }

    ~DutyFixture()
    {
        duty.reset();
        client.reset();
        storage.reset();
        store.reset();
        std::filesystem::remove(path);
    }

    /// @brief Answer the check in flight with a release the client would have parsed.
    void answerWith(const char* fields)
    {
        const auto parsed = parseRelease(release(fields), 200);
        ASSERT_TRUE(parsed.has_value());
        emit client->finished(*parsed);
    }

    /// @brief The same, for an answer that names @p tag.
    void answerWithTag(const char* tag)
    {
        const auto parsed = parseRelease(releaseWithTag(tag), 200);
        ASSERT_TRUE(parsed.has_value());
        emit client->finished(*parsed);
    }

    std::filesystem::path path;
    ScriptedManager manager;
    std::unique_ptr<lens::core::KnownStore> store;
    std::unique_ptr<lens::app::StorageDuty> storage;
    std::unique_ptr<lens::update::UpdateClient> client;
    std::unique_ptr<lens::app::UpdateDuty> duty;

    /// @return The document as it stands on disk, which is what the reader would get next run.
    nlohmann::json reloaded() const
    {
        auto again = lens::core::KnownStore::load(path);
        return again.document();
    }
};

} // namespace

TEST(UpdateParse, ReadsTheVersionThePageAndTheNotes)
{
    QtApplication qt;
    const auto parsed = parseRelease(release(R"(,"body":"Fixed two things.\n\nAnd a third thing nobody reads." )"), 200);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->versionText, QStringLiteral("1.2.0"));
    EXPECT_EQ(parsed->pageUrl, QStringLiteral("https://github.com/kakuyo1/lens/releases/tag/v1.2.0"));
    // Only the first paragraph: the rest is the change log, and the card has no room for it.
    EXPECT_EQ(parsed->notes, QStringLiteral("Fixed two things."));
}

TEST(UpdateParse, AcceptsANullBodyAsAReleaseWithNothingWritten)
{
    QtApplication qt;
    const auto parsed = parseRelease(release(R"(,"body":null)"), 200);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->notes.isEmpty());
}

TEST(UpdateParse, RejectsAMissingOrWrongTypedRequiredField)
{
    QtApplication qt;
    EXPECT_FALSE(parseRelease(R"({"html_url":"https://example.com/r"})", 200).has_value());
    EXPECT_FALSE(parseRelease(R"({"tag_name":"v1.2.0"})", 200).has_value());
    EXPECT_FALSE(parseRelease(R"({"tag_name":1,"html_url":"https://example.com/r"})", 200).has_value());
    EXPECT_FALSE(parseRelease(R"({"tag_name":"v1.2.0","html_url":7})", 200).has_value());
}

TEST(UpdateParse, RejectsANonHttpsPageAndANonSemverTag)
{
    QtApplication qt;
    EXPECT_FALSE(parseRelease(R"({"tag_name":"v1.2.0","html_url":"http://example.com/r"})", 200).has_value());
    EXPECT_FALSE(parseRelease(R"({"tag_name":"v1.2.0","html_url":"https:///r"})", 200).has_value());
    EXPECT_FALSE(parseRelease(releaseWithTag("v1.2.0-rc1"), 200).has_value());
    EXPECT_FALSE(parseRelease(releaseWithTag("nightly"), 200).has_value());
}

TEST(UpdateParse, RejectsAnAnswerThatIsNotJSONAtAll)
{
    QtApplication qt;
    EXPECT_FALSE(parseRelease("not json", 200).has_value());
    EXPECT_FALSE(parseRelease("[]", 200).has_value());
}

TEST(UpdateParse, TakesTheFirstParagraphFromABodyThatCameBackWithWindowsLineEnds)
{
    QtApplication qt;
    const auto parsed = parseRelease(release(",\"body\":\"Fixed two things.\r\n\r\nA list.\r\n\""), 200);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->notes, QStringLiteral("Fixed two things."));
}

TEST(UpdateParse, CutsTheNotesToTheCeilingAndSaysSo)
{
    QtApplication qt;
    const auto parsed = parseRelease(release(R"(,"body":"abcdefghij")"), 4);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->notes, QStringLiteral("abcd..."));
}

TEST(UpdateAvailability, OnlyAStrictlyNewerVersionIsAvailable)
{
    using lens::update::isUpdateAvailable;
    using lens::util::SemVer;
    EXPECT_TRUE(isUpdateAvailable(SemVer{1, 2, 0}, SemVer{1, 1, 0}));
    EXPECT_TRUE(isUpdateAvailable(SemVer{1, 10, 0}, SemVer{1, 9, 0}));
    EXPECT_FALSE(isUpdateAvailable(SemVer{1, 1, 0}, SemVer{1, 1, 0}));
    EXPECT_FALSE(isUpdateAvailable(SemVer{1, 0, 0}, SemVer{1, 1, 0}));
}

TEST(UpdateDateRule, ChecksOnceWhenTheStoredDateIsNotToday)
{
    StoredUpdate stored;
    stored.lastCheckDate = QStringLiteral("2026-10-05");
    EXPECT_TRUE(shouldCheckAutomatically(stored, QStringLiteral("2026-10-06")));
    stored.lastCheckDate = QStringLiteral("2026-10-06");
    EXPECT_FALSE(shouldCheckAutomatically(stored, QStringLiteral("2026-10-06")));
    stored.lastCheckDate.clear();
    EXPECT_TRUE(shouldCheckAutomatically(stored, QStringLiteral("2026-10-06")));
}

TEST(UpdateDateRule, ChecksNothingWhenTheReaderSwitchedItOff)
{
    StoredUpdate stored;
    stored.autoCheck     = false;
    stored.lastCheckDate = QStringLiteral("2026-10-05");
    EXPECT_FALSE(shouldCheckAutomatically(stored, QStringLiteral("2026-10-06")));
}

TEST(UpdateSkipRule, TheSkippedVersionSuppressesThePromptAndANewerOneDoesNot)
{
    StoredUpdate stored;
    stored.skipped = QStringLiteral("1.2.0");
    EXPECT_FALSE(shouldPrompt(stored, QStringLiteral("1.2.0"), true));
    EXPECT_TRUE(shouldPrompt(stored, QStringLiteral("1.3.0"), true));
    stored.skipped.clear();
    EXPECT_TRUE(shouldPrompt(stored, QStringLiteral("1.2.0"), true));
}

TEST(UpdateSkipRule, NothingToShowWhenThereIsNothingNew)
{
    StoredUpdate stored;
    EXPECT_FALSE(shouldPrompt(stored, QStringLiteral("1.2.0"), false));
}

TEST(UpdateSettings, ReadsTheSourceAndTheLimitsOutOfTheData)
{
    QtApplication qt;
    const auto settings = lens::update::Settings::load(std::filesystem::path(LENS_SOURCE_DIR) / "data" / "update.json");
    EXPECT_EQ(settings.source.scheme(), QStringLiteral("https"));
    EXPECT_FALSE(settings.source.host().isEmpty());
    EXPECT_EQ(settings.timeoutMs, 10000);
    EXPECT_EQ(settings.maxNotesChars, 200);
    EXPECT_EQ(settings.maxBytes, 2097152);
}

TEST(UpdateSettings, AFileThatIsNotThereCostsTheCheckAndNothingElse)
{
    QtApplication qt;
    const auto settings = lens::update::Settings::load(std::filesystem::path(LENS_SOURCE_DIR) / "data" / "no-such-file.json");
    EXPECT_FALSE(settings.source.isValid());
}

TEST(UpdateClient, RefusesANonHttpsSourceWithoutSendingAnything)
{
    QtApplication qt;
    ScriptedManager manager;
    lens::update::Settings settings;
    settings.source = QUrl{QStringLiteral("http://api.github.com/repos/kakuyo1/lens/releases/latest")};

    lens::update::UpdateClient client(settings, &manager);
    int offline = 0;
    QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });

    client.fetch();
    settle();

    EXPECT_EQ(manager.requests, 0);
    EXPECT_EQ(offline, 1);
}

TEST(UpdateClient, AnAnswerLargerThanTheCeilingIsAbortedAndReadsAsOffline)
{
    QtApplication qt;
    ScriptedManager manager;
    lens::update::Settings settings;
    settings.source        = QUrl{QStringLiteral("https://api.github.com/repos/kakuyo1/lens/releases/latest")};
    settings.maxBytes      = 16;
    settings.maxNotesChars = 200;

    lens::update::UpdateClient client(settings, &manager);
    int offline  = 0;
    int releases = 0;
    QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });
    QObject::connect(&client, &lens::update::UpdateClient::finished, [&releases](lens::update::Release) { ++releases; });

    client.fetch();
    ASSERT_EQ(manager.requests, 1);

    // What the reply reports once more than the ceiling has arrived. The handler the client
    // connected is what calls abort(), and an aborted reply is an error, so this reaches the
    // same offline branch a dead network does -- before the answer is delivered, which is when
    // an oversized body is noticed in the first place.
    const auto reply = manager.findChild<QNetworkReply*>();
    ASSERT_NE(reply, nullptr);
    emit reply->downloadProgress(17, 17);
    settle();

    EXPECT_EQ(offline, 1);
    EXPECT_EQ(releases, 0);
}

TEST(UpdateDuty, ANewerVersionAnswersAvailableAndRaisesTheCard)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkAtStartup();
    one.answerWith(R"(,"body":"Fixed two things.")");

    const QVariantMap state = one.duty->update();
    EXPECT_EQ(state.value("state").toString(), QStringLiteral("available"));
    EXPECT_EQ(state.value("current").toString(), QStringLiteral("1.1.0"));
    EXPECT_EQ(state.value("latest").toString(), QStringLiteral("1.2.0"));
    EXPECT_EQ(state.value("notes").toString(), QStringLiteral("Fixed two things."));
    EXPECT_TRUE(one.duty->cardVisible());
}

TEST(UpdateDuty, TheSameVersionAnswersUpToDateAndRaisesNothing)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkAtStartup();
    one.answerWithTag("v1.1.0");
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("upToDate"));
    EXPECT_FALSE(one.duty->cardVisible());
}

TEST(UpdateDuty, TheCheckIsWrittenWhenItStartsSoAFailedOneStillCountsForTheDay)
{
    QtApplication qt;
    DutyFixture one{500};
    one.duty->checkAtStartup();
    settle();

    EXPECT_EQ(one.manager.requests, 1);
    EXPECT_EQ(one.reloaded().at("UPDATE").at("lastCheckDate").get<std::string>(), "2026-10-06");

    // The same day, a second startup asks nothing.
    one.duty->checkAtStartup();
    settle();
    EXPECT_EQ(one.manager.requests, 1);
}

TEST(UpdateDuty, AFailedAutomaticCheckLeavesTheStateIdleAndShowsNoCard)
{
    QtApplication qt;
    DutyFixture one{500};
    one.duty->checkAtStartup();
    settle();

    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("idle"));
    EXPECT_FALSE(one.duty->cardVisible());
    // No transport text anywhere in what the surfaces read.
    const QVariantMap state = one.duty->update();
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        EXPECT_EQ(it.value().toString().contains(QStringLiteral("HTTP")), false);
}

TEST(UpdateDuty, AFailedManualCheckSaysOffline)
{
    QtApplication qt;
    DutyFixture one{500};
    one.duty->checkForUpdates();
    settle();

    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("offline"));
    EXPECT_FALSE(one.duty->cardVisible());
}

TEST(UpdateDuty, ASwitchedOffCheckSendsNothingAtAll)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->setAutoCheck(false);
    one.duty->checkAtStartup();
    settle();

    EXPECT_EQ(one.manager.requests, 0);
    EXPECT_EQ(one.reloaded().at("UPDATE").at("autoCheck").get<bool>(), false);
}

TEST(UpdateDuty, SkippingWritesThatExactVersionAndClosesTheCard)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkAtStartup();
    one.answerWith(R"(,"body":"Fixed two things.")");
    ASSERT_TRUE(one.duty->cardVisible());

    one.duty->skipUpdate();
    EXPECT_EQ(one.reloaded().at("UPDATE").at("skipped").get<std::string>(), "1.2.0");
    EXPECT_FALSE(one.duty->cardVisible());

    // The next day's automatic check finds the same version and stays quiet about it.
    one.duty->checkAtStartup();
    settle();
    EXPECT_EQ(one.manager.requests, 1, "the same date must not check again");
}

TEST(UpdateDuty, ASkippedVersionIsQuietOnTheAutomaticPathAndLoudOnTheManualOne)
{
    QtApplication qt;
    DutyFixture one;
    // A document that already carries the skip, as a second launch would load it.
    one.store->document()["UPDATE"]["skipped"]   = std::string("1.2.0");
    one.store->document()["UPDATE"]["autoCheck"] = true;

    one.duty->checkAtStartup();
    one.answerWith("");
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("available"));
    EXPECT_FALSE(one.duty->cardVisible(), "a skipped version must not raise the card again");

    // The reader asks, and the real state is reported whatever was skipped.
    one.duty->checkForUpdates();
    one.answerWith("");
    EXPECT_TRUE(one.duty->cardVisible(), "a manual check ignores what was skipped");
}

/// The reader can press the button while the startup check is still waiting on the network,
/// which is up to the data's whole timeout. The press is not dropped: the answer already on its
/// way is reported as the one they asked for, so a dead network says so and a skipped version
/// still puts its card up.
TEST(UpdateDuty, AManualCheckDuringTheStartupCheckIsAnsweredAsManual)
{
    QtApplication qt;
    DutyFixture one;
    one.store->document()["UPDATE"]["skipped"] = std::string("1.2.0");

    one.duty->checkAtStartup();
    ASSERT_EQ(one.manager.requests, 1);

    one.duty->checkForUpdates(); // pressed before anything came back
    EXPECT_EQ(one.manager.requests, 1, "the press must not start a second request");

    one.answerWith("");
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("available"));
    EXPECT_TRUE(one.duty->cardVisible(), "the card a reader asked for must come up");
}

TEST(UpdateDuty, AFailedCheckPressedDuringStartupIsReportedOfflineRatherThanIdle)
{
    QtApplication qt;
    DutyFixture one{500};

    one.duty->checkAtStartup();
    one.duty->checkForUpdates();
    settle(); // the scripted reply answers with its status, and nothing else

    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("offline"));
    EXPECT_FALSE(one.duty->cardVisible());
}

/// A card the reader is looking at must not blink out because they asked for a check: it stays
/// up through the request and is still up when the same version comes back. The QML side of
/// this -- that the window does not move -- cannot be exercised offline; what is asserted here
/// is the flag the surfaces read, which is what decides it.
TEST(UpdateDuty, ACheckStartedWhileTheCardIsUpLeavesTheCardUp)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkForUpdates();
    one.answerWith("");
    ASSERT_TRUE(one.duty->cardVisible());

    one.duty->checkForUpdates();
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("checking"));
    EXPECT_TRUE(one.duty->cardVisible(), "starting a check took the card down");

    one.answerWith("");
    EXPECT_EQ(one.duty->update().value("latest").toString(), QStringLiteral("1.2.0"));
    EXPECT_TRUE(one.duty->cardVisible(), "the same version took the card down");
}

TEST(UpdateDuty, ACheckThatAnswersThereIsNothingNewTakesTheCardDown)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkForUpdates();
    one.answerWith("");
    ASSERT_TRUE(one.duty->cardVisible());

    // The reader installed the version the card was offering and asked again.
    one.duty->checkForUpdates();
    one.answerWithTag("v1.1.0");
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("upToDate"));
    EXPECT_FALSE(one.duty->cardVisible(), "a card for a version now installed is not the truth");
}

TEST(UpdateDuty, ANewerVersionPromptsAgainAfterTheSkippedOne)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkAtStartup();
    one.answerWith("");
    one.duty->skipUpdate();

    one.duty->checkForUpdates();
    const auto parsed = parseRelease(releaseWithTag("v1.3.0"), 200);
    ASSERT_TRUE(parsed.has_value());
    emit one.client->finished(*parsed);

    EXPECT_EQ(one.duty->update().value("latest").toString(), QStringLiteral("1.3.0"));
    EXPECT_TRUE(one.duty->cardVisible(), "skipping 1.2.0 says nothing about 1.3.0");
}

TEST(UpdateDuty, AMissingKeyStillReadsAsEveryDefault)
{
    QtApplication qt;
    DutyFixture one;
    EXPECT_TRUE(one.duty->autoCheck());
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("idle"));

    one.duty->setAutoCheck(false);
    one.duty->setAutoCheck(true);
    EXPECT_TRUE(one.reloaded().at("UPDATE").at("autoCheck").get<bool>());
}