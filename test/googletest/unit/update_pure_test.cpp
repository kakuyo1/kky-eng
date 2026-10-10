/**
 * @file update_pure_test.cpp
 * @brief Offline tests for the update module: the release address, the date and skip rules,
 *        the guards on the request, and what the duty writes.
 *
 * The answer the check now reads is a redirect, not a document: the cases below hand
 * parseReleaseLocation() the address a release page would redirect to, and the client's own
 * `finished` path is driven with a reply that carries the same header.
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

using lens::update::parseReleaseLocation;
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

/// The page the check asks, and the answer it is given back.
constexpr auto kReleasePage = "https://github.com/kakuyo1/lens/releases/latest";

/// @return The address the page redirects to when @p tag is the newest release.
QUrl releaseUrl(const char* tag)
{
    return QUrl{QStringLiteral("https://github.com/kakuyo1/lens/releases/tag/%1").arg(QLatin1String(tag))};
}

/// A reply that finishes on the next turn of the loop, so the client's own connections exist
/// before `finished` arrives. It carries the two things this check reads -- a status and,
/// for a redirect, the `Location` -- and no body, which is the shape a redirect really has.
/// `abort()` is what an answer that outgrew its ceiling does: a cancelled reply, which the
/// client has to read as offline.
class ScriptedReply final : public QNetworkReply {
public:
    ScriptedReply(QObject* parent, int status, QString location = {})
        : QNetworkReply(parent), status_(status)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status_);
        if (not location.isEmpty())
            setHeader(QNetworkRequest::LocationHeader, location);
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
    explicit ScriptedManager(int status = 302, QString location = {})
        : status_(status), location_(std::move(location))
    {
    }

    int requests = 0;
    /// What the request asked about redirects, kept so a case can read it back: a stub manager
    /// never follows one whatever the policy says, so the count alone would prove nothing.
    QVariant redirectPolicy;

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        ++requests;
        redirectPolicy = request.attribute(QNetworkRequest::RedirectPolicyAttribute);
        return new ScriptedReply(this, status_, location_);
    }

private:
    int status_;
    QString location_;
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
/// The client's own request goes to the scripted manager, which counts it and answers it; a
/// case that needs a release to reach the duty delivers it on the signal the client emits once
/// it has read a redirect, built here by the same parser the client uses.
struct DutyFixture {
    DutyFixture(int status = 500)
        : path(std::filesystem::temp_directory_path() / "lens_update_duty_test.json")
    {
        std::filesystem::remove(path);
        store   = std::make_unique<lens::core::KnownStore>(lens::core::KnownStore::load(path));
        storage = std::make_unique<lens::app::StorageDuty>(*store);
        lens::update::Settings settings;
        settings.source   = QUrl{QString::fromLatin1(kReleasePage)};
        settings.maxBytes = 2 * 1024 * 1024;
        client            = std::make_unique<lens::update::UpdateClient>(settings, &manager);
        duty              = std::make_unique<lens::app::UpdateDuty>(*storage, *client, QStringLiteral("1.1.0"), [] { return pinnedDate(); });
    }

    ~DutyFixture()
    {
        duty.reset();
        client.reset();
        storage.reset();
        store.reset();
        std::filesystem::remove(path);
    }

    /// @brief Answer the check in flight with a redirect naming the newer release.
    void answerWithNewer()
    {
        deliver("v1.2.0");
    }

    /// @brief The same, for a redirect that names @p tag.
    void answerWithTag(const char* tag)
    {
        deliver(tag);
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

private:
    void deliver(const char* tag)
    {
        const auto parsed = parseReleaseLocation(releaseUrl(tag));
        ASSERT_TRUE(parsed.has_value());
        emit client->finished(*parsed);
    }
};

} // namespace

TEST(UpdateLocation, ReadsTheVersionAndKeepsThePageItWasGiven)
{
    QtApplication qt;
    for (const char* tag : {"v1.1.0", "1.1.0"}) {
        const QUrl url    = releaseUrl(tag);
        const auto parsed = parseReleaseLocation(url);
        ASSERT_TRUE(parsed.has_value()) << tag;
        EXPECT_EQ(parsed->versionText, QStringLiteral("1.1.0")) << tag;
        EXPECT_EQ(parsed->version, (lens::util::SemVer{1, 1, 0})) << tag;
        EXPECT_EQ(parsed->pageUrl, url.toString()) << tag;
        // The redirect carries no notes, and none are fetched behind it.
        EXPECT_TRUE(parsed->notes.isEmpty()) << tag;
    }
}

TEST(UpdateLocation, RefusesAnythingThatIsNotThisProjectsReleasePage)
{
    QtApplication qt;
    // Another host, including a lookalike and a subdomain of the real one.
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://example.com/kakuyo1/lens/releases/tag/v1.1.0"}).has_value());
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://github.com.evil.test/kakuyo1/lens/releases/tag/v1.1.0"}).has_value());
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://raw.github.com/kakuyo1/lens/releases/tag/v1.1.0"}).has_value());
    // Not https.
    EXPECT_FALSE(parseReleaseLocation(QUrl{"http://github.com/kakuyo1/lens/releases/tag/v1.1.0"}).has_value());
    EXPECT_FALSE(parseReleaseLocation(QUrl{"ftp://github.com/kakuyo1/lens/releases/tag/v1.1.0"}).has_value());
    // Not a release tag path: the repository itself, and someone else's repository.
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://github.com/kakuyo1/lens/releases"}).has_value());
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://github.com/kakuyo1/lens"}).has_value());
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://github.com/other/lens/releases/tag/v1.1.0"}).has_value());
    // The tag itself is not a version.
    EXPECT_FALSE(parseReleaseLocation(releaseUrl("v1.2")).has_value());
    EXPECT_FALSE(parseReleaseLocation(releaseUrl("1.2.0-rc1")).has_value());
    EXPECT_FALSE(parseReleaseLocation(releaseUrl("nightly")).has_value());
    EXPECT_FALSE(parseReleaseLocation(releaseUrl("")).has_value());
    // And nothing at all.
    EXPECT_FALSE(parseReleaseLocation(QUrl{}).has_value());
    EXPECT_FALSE(parseReleaseLocation(QUrl{QString{}}).has_value());
}

TEST(UpdateLocation, RefusesARepositoryWithNoReleaseYet)
{
    QtApplication qt;
    // Where the page sends a repository that has never published: not under the tag path, so
    // there is no version to report and the check reads as offline rather than as up to date.
    EXPECT_FALSE(parseReleaseLocation(QUrl{"https://github.com/kakuyo1/lens/releases"}).has_value());
}

TEST(UpdateLocation, ResolvesARelativeRedirectTheWayTheClientDoes)
{
    QtApplication qt;
    const QUrl source{QString::fromLatin1(kReleasePage)};
    EXPECT_EQ(source.resolved(QUrl{"/kakuyo1/lens/releases/tag/v1.2.0"}),
              QUrl{"https://github.com/kakuyo1/lens/releases/tag/v1.2.0"});
    EXPECT_TRUE(parseReleaseLocation(source.resolved(QUrl{"/kakuyo1/lens/releases/tag/v1.2.0"})).has_value());
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

TEST(UpdateSettings, ReadsTheReleasePageAndTheLimitsOutOfTheData)
{
    QtApplication qt;
    const auto settings = lens::update::Settings::load(std::filesystem::path(LENS_SOURCE_DIR) / "data" / "update.json");
    EXPECT_EQ(settings.source.toString(), QString::fromLatin1(kReleasePage));
    EXPECT_EQ(settings.source.scheme(), QStringLiteral("https"));
    EXPECT_FALSE(settings.source.host().isEmpty());
    EXPECT_EQ(settings.timeoutMs, 10000);
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
    settings.source = QUrl{"http://github.com/kakuyo1/lens/releases/latest"};

    lens::update::UpdateClient client(settings, &manager);
    int offline = 0;
    QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });

    client.fetch();
    settle();

    EXPECT_EQ(manager.requests, 0);
    EXPECT_EQ(offline, 1);
}

TEST(UpdateClient, AsksTheSourceNotToFollowTheRedirect)
{
    QtApplication qt;
    ScriptedManager manager{302, releaseUrl("v1.2.0").toString()};
    lens::update::Settings settings;
    settings.source = QUrl{QString::fromLatin1(kReleasePage)};

    lens::update::UpdateClient client(settings, &manager);
    client.fetch();
    settle();

    // The request says not to, which is the part that matters: the stub manager would not follow
    // one either way, so the request count alone proves nothing about the client.
    EXPECT_EQ(manager.redirectPolicy.toInt(), static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
    EXPECT_NE(manager.redirectPolicy.toInt(), static_cast<int>(QNetworkRequest::NoLessSafeRedirectPolicy));
    EXPECT_EQ(manager.requests, 1);
}

/// The redirect is the answer: a 302 with a Location the parser accepts is a release, and
/// everything else this page can say -- a 200, a rate limit, a redirect with nowhere to go --
/// is offline. This drives the client's own `finished` path, header and all.
TEST(UpdateClient, ARedirectCarryingAReleaseIsTheOnlyAnswerItAccepts)
{
    QtApplication qt;
    {
        ScriptedManager manager{302, releaseUrl("v1.2.0").toString()};
        lens::update::Settings settings;
        settings.source = QUrl{QString::fromLatin1(kReleasePage)};

        lens::update::UpdateClient client(settings, &manager);
        QVector<lens::update::Release> releases;
        int offline = 0;
        QObject::connect(&client, &lens::update::UpdateClient::finished, [&releases](lens::update::Release r) { releases.append(r); });
        QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });

        client.fetch();
        settle();

        ASSERT_EQ(releases.size(), 1);
        EXPECT_EQ(releases.front().versionText, QStringLiteral("1.2.0"));
        EXPECT_EQ(releases.front().pageUrl, releaseUrl("v1.2.0").toString());
        EXPECT_EQ(offline, 0);
    }
    {
        ScriptedManager manager{302, QStringLiteral("https://example.com/kakuyo1/lens/releases/tag/v1.2.0")};
        lens::update::Settings settings;
        settings.source = QUrl{QString::fromLatin1(kReleasePage)};

        lens::update::UpdateClient client(settings, &manager);
        int offline = 0;
        QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });

        client.fetch();
        settle();

        EXPECT_EQ(offline, 1, "a redirect to another host is not this project's release page");
    }
    {
        // The page itself answered, which is not an answer this check can use.
        ScriptedManager manager{200};
        lens::update::Settings settings;
        settings.source = QUrl{QString::fromLatin1(kReleasePage)};

        lens::update::UpdateClient client(settings, &manager);
        int offline = 0;
        QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });

        client.fetch();
        settle();

        EXPECT_EQ(offline, 1, "a 200 is not the redirect this check reads");
    }
    {
        ScriptedManager manager{302};
        lens::update::Settings settings;
        settings.source = QUrl{QString::fromLatin1(kReleasePage)};

        lens::update::UpdateClient client(settings, &manager);
        int offline = 0;
        QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });

        client.fetch();
        settle();

        EXPECT_EQ(offline, 1, "a redirect that says nothing about where is no answer");
    }
}

TEST(UpdateClient, AnAnswerLargerThanTheCeilingIsAbortedAndReadsAsOffline)
{
    QtApplication qt;
    ScriptedManager manager{302, releaseUrl("v1.2.0").toString()};
    lens::update::Settings settings;
    settings.source   = QUrl{QString::fromLatin1(kReleasePage)};
    settings.maxBytes = 16;

    lens::update::UpdateClient client(settings, &manager);
    int offline  = 0;
    int releases = 0;
    QObject::connect(&client, &lens::update::UpdateClient::offline, [&offline] { ++offline; });
    QObject::connect(&client, &lens::update::UpdateClient::finished, [&releases](lens::update::Release) { ++releases; });

    client.fetch();
    ASSERT_EQ(manager.requests, 1);

    // What the reply reports once more than the ceiling has arrived. The handler the client
    // connected is what calls abort(), and an aborted reply is an error, so this reaches the
    // same offline branch a dead network does.
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
    one.answerWithNewer();

    const QVariantMap state = one.duty->update();
    EXPECT_EQ(state.value("state").toString(), QStringLiteral("available"));
    EXPECT_EQ(state.value("current").toString(), QStringLiteral("1.1.0"));
    EXPECT_EQ(state.value("latest").toString(), QStringLiteral("1.2.0"));
    // The redirect carries no notes, so the key is there and empty rather than absent.
    EXPECT_TRUE(state.value("notes").toString().isEmpty());
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
    one.answerWithNewer();
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
    one.answerWithNewer();
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("available"));
    EXPECT_FALSE(one.duty->cardVisible(), "a skipped version must not raise the card again");

    // The reader asks, and the real state is reported whatever was skipped.
    one.duty->checkForUpdates();
    one.answerWithNewer();
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

    one.answerWithNewer();
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
    one.answerWithNewer();
    ASSERT_TRUE(one.duty->cardVisible());

    one.duty->checkForUpdates();
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("checking"));
    EXPECT_TRUE(one.duty->cardVisible(), "starting a check took the card down");

    one.answerWithNewer();
    EXPECT_EQ(one.duty->update().value("latest").toString(), QStringLiteral("1.2.0"));
    EXPECT_TRUE(one.duty->cardVisible(), "the same version took the card down");
}

TEST(UpdateDuty, ACheckThatAnswersThereIsNothingNewTakesTheCardDown)
{
    QtApplication qt;
    DutyFixture one;
    one.duty->checkForUpdates();
    one.answerWithNewer();
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
    one.answerWithNewer();
    one.duty->skipUpdate();

    one.duty->checkForUpdates();
    const auto parsed = parseReleaseLocation(releaseUrl("v1.3.0"));
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