/**
 * @file update_pure_test.cpp
 * @brief Offline tests for the update module: the release address, the download's rules, the
 *        date and skip rules, the guards on the request, and what the duty writes.
 *
 * The answer the check reads is a redirect, not a document: the cases below hand
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
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cctype>
#include <filesystem>
#include <memory>

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include "app/storage_duty.h"
#include "app/update_duty.h"
#include "core/known_store.h"
#include "update/update_client.h"
#include "update/update_pure.h"

using lens::update::expectedDigest;
using lens::update::installerName;
using lens::update::isAllowedDownloadUrl;
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
    ScriptedReply(QObject* parent, int status, QString location = {}, qint64 length = 0, bool answers = true, QByteArray body = {})
        : QNetworkReply(parent), status_(status), body_(std::move(body))
    {
        // A reply that never answers has no status: a real connection that times out before its
        // headers arrive reports none, and the proxy judgement depends on that difference.
        if (answers)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status_);
        if (not location.isEmpty())
            setHeader(QNetworkRequest::LocationHeader, location);
        if (length > 0)
            setHeader(QNetworkRequest::ContentLengthHeader, length);
        open(QIODevice::ReadOnly);
        if (answers)
            QTimer::singleShot(0, this, [this] { deliver(); });
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("the answer outgrew its ceiling"));
        deliver();
    }

    qint64 readData(char* data, qint64 max) override
    {
        if (body_.isEmpty())
            return -1;
        const qint64 n = std::min<qint64>(max, body_.size());
        std::memcpy(data, body_.constData(), static_cast<std::size_t>(n));
        body_.remove(0, static_cast<int>(n));
        return n;
    }

    /// Finish once, whichever of the two paths arrives first: a reply that finishes twice is
    /// not a shape Qt produces, and the counts below are only meaningful if it does not.
    void deliver()
    {
        if (delivered_)
            return;
        delivered_ = true;
        // A real transfer announces its bytes before it finishes, and the client reads them then.
        if (not body_.isEmpty())
            emit readyRead();
        setFinished(true);
        emit finished();
    }

private:
    int status_;
    QByteArray body_;
    bool delivered_ = false;
};

/// The seam LlmClient takes, counting what it was asked for.
class ScriptedManager final : public QNetworkAccessManager {
public:
    explicit ScriptedManager(int status = 302, QString location = {}, bool answers = true, QByteArray body = {})
        : status_(status), location_(std::move(location)), answers_(answers), body_(std::move(body))
    {
    }

    int requests = 0;
    /// What the answer says it is, when a case wants the server to announce a size.
    qint64 announceLength = 0;
    /// What the request asked about redirects, kept so a case can read it back: a stub manager
    /// never follows one whatever the policy says, so the count alone would prove nothing.
    QVariant redirectPolicy;

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        ++requests;
        redirectPolicy = request.attribute(QNetworkRequest::RedirectPolicyAttribute);
        return new ScriptedReply(this, status_, location_, announceLength, answers_, body_);
    }

private:
    int status_;
    QString location_;
    /// Whether a reply ever finishes. False leaves it pending, which is how a case drives a
    /// client whose answer it delivers itself rather than one the stub hands over.
    bool answers_;
    QByteArray body_;
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

/// One duty over a temporary document, the clients it asks through, and a cache directory of
/// its own so a download case writes nothing to the machine running it.
///
/// The check's own request goes to the scripted manager, which counts it and answers it; a case
/// that needs a release to reach the duty delivers it on the signal the client emits once it
/// has read a redirect, built here by the same parser the client uses. The download's signals
/// are delivered the same way, because a stub reply cannot hand a reader bytes.
struct DutyFixture {
    /// `answers` false leaves the check's stub quiet, which is what a download case wants: its
    /// own answers arrive as the duty's signals, and a check answer landing mid-case would
    /// wipe the state the case is checking.
    explicit DutyFixture(bool answers = true)
        : path(std::filesystem::temp_directory_path() / "lens_update_duty_test.json"),
          manager(500, QString{}, answers)
    {
        std::filesystem::remove(path);
        QDir(cache).removeRecursively();
        QDir().mkpath(cache);
        store   = std::make_unique<lens::core::KnownStore>(lens::core::KnownStore::load(path));
        storage = std::make_unique<lens::app::StorageDuty>(*store);
        lens::update::Settings settings;
        settings.source   = QUrl{QString::fromLatin1(kReleasePage)};
        settings.maxBytes = 2 * 1024 * 1024;
        client            = std::make_unique<lens::update::UpdateClient>(settings, &manager);
        // The download's own stub never answers: these cases deliver the sums and the installer
        // themselves, so nothing the stub invents arrives in the middle of what they are checking.
        downloader = std::make_unique<lens::update::DownloadClient>(settings, &downloadManager);
        duty       = std::make_unique<lens::app::UpdateDuty>(*storage, *client, *downloader, QStringLiteral("1.1.0"), [this](const QString& to, const QStringList& args) {
                                                         launched  = to;
                                                         arguments = args;
                                                         return launchWorks; }, cache, [] { return pinnedDate(); });
    }

    ~DutyFixture()
    {
        duty.reset();
        downloader.reset();
        client.reset();
        storage.reset();
        store.reset();
        std::filesystem::remove(path);
        QDir(cache).removeRecursively();
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

    /// @brief The file a download for @p version writes to, which is never the final name.
    QString partialAt(const QString& version = QStringLiteral("1.2.0")) const
    {
        return installerAt(version) + QStringLiteral(".part");
    }

    /// @brief The file the checked installer occupies, for @p version.
    QString installerAt(const QString& version = QStringLiteral("1.2.0")) const
    {
        return cache + QLatin1Char('/') + lens::update::installerName(version);
    }

    /// @brief A `SHA256SUMS` body listing @p name with the digest of @p contents.
    static QByteArray sumsFor(const QString& name, const QByteArray& contents)
    {
        const QByteArray digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
        return digest + "  " + name.toUtf8() + "\n";
    }

    std::filesystem::path path;
    QString cache = QDir::temp().filePath("lens-update-duty-cache");
    ScriptedManager manager;
    /// Never answers: the download cases hand the duty their own sums and installer.
    ScriptedManager downloadManager{200, QString{}, false};
    std::unique_ptr<lens::core::KnownStore> store;
    std::unique_ptr<lens::app::StorageDuty> storage;
    std::unique_ptr<lens::update::UpdateClient> client;
    std::unique_ptr<lens::update::DownloadClient> downloader;
    std::unique_ptr<lens::app::UpdateDuty> duty;
    QString launched;
    QStringList arguments;
    /// What the launcher reports: whether the installer started. A case turns it off to see the
    /// failure the card must show instead of closing Lens.
    bool launchWorks = true;

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

        EXPECT_EQ(offline, 1) << "a redirect to another host is not this project's release page";
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

        EXPECT_EQ(offline, 1) << "a 200 is not the redirect this check reads";
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

        EXPECT_EQ(offline, 1) << "a redirect that says nothing about where is no answer";
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
    DutyFixture one;
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
    DutyFixture one;
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
    DutyFixture one;
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
    EXPECT_EQ(one.manager.requests, 1) << "the same date must not check again";
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
    EXPECT_FALSE(one.duty->cardVisible()) << "a skipped version must not raise the card again";

    // The reader asks, and the real state is reported whatever was skipped.
    one.duty->checkForUpdates();
    one.answerWithNewer();
    EXPECT_TRUE(one.duty->cardVisible()) << "a manual check ignores what was skipped";
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
    EXPECT_EQ(one.manager.requests, 1) << "the press must not start a second request";

    one.answerWithNewer();
    EXPECT_EQ(one.duty->update().value("state").toString(), QStringLiteral("available"));
    EXPECT_TRUE(one.duty->cardVisible()) << "the card a reader asked for must come up";
}

TEST(UpdateDuty, AFailedCheckPressedDuringStartupIsReportedOfflineRatherThanIdle)
{
    QtApplication qt;
    DutyFixture one;

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
    EXPECT_TRUE(one.duty->cardVisible()) << "starting a check took the card down";

    one.answerWithNewer();
    EXPECT_EQ(one.duty->update().value("latest").toString(), QStringLiteral("1.2.0"));
    EXPECT_TRUE(one.duty->cardVisible()) << "the same version took the card down";
}

TEST(UpdateDuty, ACardThatAppearsIsOfferedOnceAndARecheckOfItIsNot)
{
    QtApplication qt;
    DutyFixture one;
    int offered = 0;
    QObject::connect(one.duty.get(), &lens::app::UpdateDuty::newVersionOffered, [&offered] { ++offered; });

    one.duty->checkForUpdates();
    one.answerWithNewer();
    EXPECT_EQ(offered, 1);

    one.duty->checkForUpdates();
    one.answerWithNewer();
    EXPECT_EQ(offered, 1) << "a recheck of a card that is already up is not a new offer";
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
    EXPECT_FALSE(one.duty->cardVisible()) << "a card for a version now installed is not the truth";
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
    EXPECT_TRUE(one.duty->cardVisible()) << "skipping 1.2.0 says nothing about 1.3.0";
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
TEST(UpdateSums, ReadsTheDigestOfTheExactNameItIsAskedFor)
{
    QtApplication qt;
    const QByteArray sums =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Lens-1.1.0-setup.exe\n"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  something-else.zip\n";

    const auto digest = expectedDigest(sums, QStringLiteral("Lens-1.1.0-setup.exe"));
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(*digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(UpdateSums, RefusesWhenTheFileIsNotListedOrIsOnlyNearlyListed)
{
    QtApplication qt;
    const QByteArray sums = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Lens-1.1.0-setup.exe\n";
    EXPECT_FALSE(expectedDigest(sums, QStringLiteral("Lens-1.2.0-setup.exe")).has_value());
    EXPECT_FALSE(expectedDigest(sums, QStringLiteral("setup.exe")).has_value());
    EXPECT_FALSE(expectedDigest(sums, QStringLiteral("Lens-1.1.0-setup.exe.bak")).has_value());
    EXPECT_FALSE(expectedDigest(QByteArray(), QStringLiteral("Lens-1.1.0-setup.exe")).has_value());
    EXPECT_FALSE(expectedDigest(sums, QString{}).has_value());
}

TEST(UpdateSums, RefusesALineItCannotReadAsADigest)
{
    QtApplication qt;
    const QByteArray good = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Lens-1.1.0-setup.exe\n";
    const QString name    = QStringLiteral("Lens-1.1.0-setup.exe");
    // Upper case, one space, a short digest, and a stray note: a list this check cannot read as
    // a list of digests is refused rather than partly believed.
    EXPECT_FALSE(expectedDigest("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855  Lens-1.1.0-setup.exe\n", name).has_value());
    EXPECT_FALSE(expectedDigest("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 Lens-1.1.0-setup.exe\n", name).has_value());
    EXPECT_FALSE(expectedDigest("e3b0  Lens-1.1.0-setup.exe\n", name).has_value());
    EXPECT_FALSE(expectedDigest(good + "a note someone left in the file\n", name).has_value());
}

TEST(UpdateSums, RefusesTheSameFileListedTwiceWithTwoDigests)
{
    QtApplication qt;
    const QString name = QStringLiteral("Lens-1.1.0-setup.exe");
    const QByteArray same =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Lens-1.1.0-setup.exe\n"
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Lens-1.1.0-setup.exe\n";
    EXPECT_TRUE(expectedDigest(same, name).has_value()) << "the same digest twice says the same thing twice";

    const QByteArray other =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Lens-1.1.0-setup.exe\n"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  Lens-1.1.0-setup.exe\n";
    EXPECT_FALSE(expectedDigest(other, name).has_value());
}

TEST(UpdateInstaller, IsNamedTheWayTheReleasePublishesIt)
{
    QtApplication qt;
    EXPECT_EQ(installerName(QStringLiteral("1.1.0")), QStringLiteral("Lens-1.1.0-setup.exe"));
    EXPECT_EQ(installerName(QStringLiteral("1.12.3")), QStringLiteral("Lens-1.12.3-setup.exe"));
    EXPECT_TRUE(installerName(QString{}).isEmpty());
    // The name is only ever spelled for a version that already parsed, which is the caller's
    // half of the rule: free text never reaches here.
    EXPECT_FALSE(lens::util::parseTag("not-a-version").has_value());
}

TEST(UpdateDownloadUrls, AcceptsTheThreeHostsAndRefusesEverythingElse)
{
    QtApplication qt;
    EXPECT_TRUE(isAllowedDownloadUrl(QUrl{"https://github.com/kakuyo1/lens/releases/download/v1.1.0/SHA256SUMS"}));
    EXPECT_TRUE(isAllowedDownloadUrl(QUrl{"https://release-assets.githubusercontent.com/github-production-release-asset/abc?token=x"}));
    EXPECT_TRUE(isAllowedDownloadUrl(QUrl{"https://objects.githubusercontent.com/x"}));

    // Not https.
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"http://github.com/kakuyo1/lens/releases/download/v1.1.0/SHA256SUMS"}));
    // Another host, and the two that only look like GitHub.
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://example.com/kakuyo1/lens/releases/download/v1.1.0/SHA256SUMS"}));
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://github.com.evil.test/kakuyo1/lens/releases/download/v1.1.0/SHA256SUMS"}));
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://evil.release-assets.githubusercontent.com.attacker.test/x"}));
    // github.com, but outside this project's download path.
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://github.com/kakuyo1/lens/releases/tag/v1.1.0"}));
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://github.com/other/lens/releases/download/v1.1.0/SHA256SUMS"}));
    // A credential in the address, and a fragment.
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://user:pass@github.com/kakuyo1/lens/releases/download/v1.1.0/x"}));
    EXPECT_FALSE(isAllowedDownloadUrl(QUrl{"https://github.com/kakuyo1/lens/releases/download/v1.1.0/x#fragment"}));
}

TEST(UpdateDigest, MatchesTheFileItPublishedAndNothingElse)
{
    QtApplication qt;
    const QByteArray contents("a small installer, standing in for the real one");
    const QString path = QDir::temp().filePath("lens-update-digest-fixture.bin");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(contents);
    file.close();

    const std::string digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex().toStdString();
    EXPECT_TRUE(lens::update::digestMatches(path, digest));

    std::string upper = digest;
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    EXPECT_TRUE(lens::update::digestMatches(path, upper)) << "a digest is not case sensitive";

    // One byte different is a different file, and a file that is not there is not a match.
    EXPECT_FALSE(lens::update::digestMatches(path, std::string(64, 'a')));
    EXPECT_FALSE(lens::update::digestMatches(QDir::temp().filePath("lens-update-digest-missing.bin"), digest));

    QFile::remove(path);
}

TEST(UpdateDownload, RefusesARedirectItWillNotFollowWithoutAskingTwice)
{
    QtApplication qt;
    ScriptedManager manager{302, QStringLiteral("https://evil.example.com/Lens-1.2.0-setup.exe")};
    lens::update::Settings settings;
    settings.source = QUrl{QString::fromLatin1(kReleasePage)};

    lens::update::DownloadClient client(settings, &manager);
    int failures = 0;
    QObject::connect(&client, &lens::update::DownloadClient::failed, [&failures] { ++failures; });

    client.fetchInstaller(QStringLiteral("1.2.0"), QDir::temp().filePath("lens-update-refused.part"));
    settle();

    EXPECT_EQ(manager.requests, 1);
    EXPECT_EQ(failures, 1);
}

TEST(UpdateDownload, FollowsARedirectToAnAssetHostItAccepts)
{
    QtApplication qt;
    ScriptedManager manager{302, QStringLiteral("https://release-assets.githubusercontent.com/github-production-release-asset/abc?token=x")};
    lens::update::Settings settings;
    settings.source = QUrl{QString::fromLatin1(kReleasePage)};

    lens::update::DownloadClient client(settings, &manager);
    client.fetchSums(QStringLiteral("1.2.0"));
    settle();

    EXPECT_EQ(manager.requests, 2) << "the second request is the asset host";
    EXPECT_EQ(manager.redirectPolicy.toInt(), static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
}

TEST(UpdateDownload, StopsAnAnswerThatAnnouncesMoreThanTheCeiling)
{
    QtApplication qt;
    ScriptedManager manager{200};
    manager.announceLength = 1024 * 1024 * 1024; // a gigabyte, against a ceiling far below it
    lens::update::Settings settings;
    settings.source           = QUrl{QString::fromLatin1(kReleasePage)};
    settings.maxDownloadBytes = 1024;

    lens::update::DownloadClient client(settings, &manager);
    int failures = 0;
    QObject::connect(&client, &lens::update::DownloadClient::failed, [&failures] { ++failures; });

    client.fetchInstaller(QStringLiteral("1.2.0"), QDir::temp().filePath("lens-update-capped.part"));
    const auto reply = manager.findChild<QNetworkReply*>();
    ASSERT_NE(reply, nullptr);
    emit reply->downloadProgress(2048, manager.announceLength);
    settle();

    EXPECT_EQ(failures, 1);
    QFile::remove(QDir::temp().filePath("lens-update-capped.part.part"));
}

TEST(UpdateDownload, StopsATransferThatArrivesPastTheCeiling)
{
    QtApplication qt;
    ScriptedManager manager{200, QString{}, false};
    lens::update::Settings settings;
    settings.source           = QUrl{QString::fromLatin1(kReleasePage)};
    settings.maxDownloadBytes = 1024;

    lens::update::DownloadClient client(settings, &manager);
    int failures = 0;
    QObject::connect(&client, &lens::update::DownloadClient::failed, [&failures] { ++failures; });

    client.fetchInstaller(QStringLiteral("1.2.0"), QDir::temp().filePath("lens-update-over.part"));
    const auto reply = manager.findChild<QNetworkReply*>();
    ASSERT_NE(reply, nullptr);
    emit reply->readyRead();
    settle();

    EXPECT_EQ(failures, 1);
    QFile::remove(QDir::temp().filePath("lens-update-over.part.part"));
}

TEST(UpdateDownload, CallsADownloadThatMovedNothingTheProxyText)
{
    QtApplication qt;
    ScriptedManager manager{200, QString{}, false}; // a transfer that never arrives and never says why
    lens::update::Settings settings;
    settings.source           = QUrl{QString::fromLatin1(kReleasePage)};
    settings.connectTimeoutMs = 60;

    lens::update::DownloadClient client(settings, &manager);
    QVector<lens::update::DownloadClient::Failure> failures;
    QObject::connect(&client, &lens::update::DownloadClient::failed, [&failures](lens::update::DownloadClient::Failure why) {
        failures.append(why);
    });

    client.fetchInstaller(QStringLiteral("1.2.0"), QDir::temp().filePath("lens-update-proxy.part"));

    // The hint is a timer, so the case waits for it rather than asserting at once.
    QElapsedTimer waited;
    waited.start();
    while (failures.isEmpty() and waited.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_EQ(failures.size(), 1);
    EXPECT_EQ(failures.front(), lens::update::DownloadClient::Failure::Proxy);
    QFile::remove(QDir::temp().filePath("lens-update-proxy.part.part"));
}

TEST(UpdateDownload, AMissingInstallerIsNotAProxyProblem)
{
    QtApplication qt;
    ScriptedManager manager{404};
    lens::update::Settings settings;
    settings.source = QUrl{QString::fromLatin1(kReleasePage)};

    lens::update::DownloadClient client(settings, &manager);
    QVector<lens::update::DownloadClient::Failure> failures;
    QObject::connect(&client, &lens::update::DownloadClient::failed, [&failures](lens::update::DownloadClient::Failure why) {
        failures.append(why);
    });

    client.fetchInstaller(QStringLiteral("1.2.0"), QDir::temp().filePath("lens-update-404.part"));
    settle();

    ASSERT_EQ(failures.size(), 1);
    EXPECT_EQ(failures.front(), lens::update::DownloadClient::Failure::Generic)
        << "a 404 is the server answering, not the network failing, and a proxy does not fix it";
    QFile::remove(QDir::temp().filePath("lens-update-404.part.part"));
}

TEST(UpdateDownload, AMissingChecksumFileIsAnAnswerNotAFailure)
{
    QtApplication qt;
    ScriptedManager manager{404};
    lens::update::Settings settings;
    settings.source   = QUrl{QString::fromLatin1(kReleasePage)};
    settings.sumsName = QStringLiteral("SHA256SUMS");

    lens::update::DownloadClient client(settings, &manager);
    int failures = 0;
    QByteArray received;
    bool answered = false;
    QObject::connect(&client, &lens::update::DownloadClient::failed, [&failures](lens::update::DownloadClient::Failure) { ++failures; });
    QObject::connect(&client, &lens::update::DownloadClient::sumsReady, [&](QByteArray sums) {
        answered = true;
        received = sums;
    });

    client.fetchSums(QStringLiteral("1.1.0"));
    settle();

    EXPECT_EQ(failures, 0) << "a release with no checksum file is not a failed download";
    EXPECT_TRUE(answered);
    EXPECT_TRUE(received.isEmpty()) << "an empty list means no digest, and the card keeps View only";
}

TEST(UpdateDownload, ASumsListArrivingInTheBodyIsHandedOverWhole)
{
    QtApplication qt;
    // The bytes the release actually serves: one `sha256sum` line, then a newline.
    const QByteArray listing = "e1341962e994b1d278a2e2f66d87b01b0ff3f16f84e4d61edbd09248042c9a2b  Lens-1.1.0-setup.exe\n";
    ScriptedManager manager{200, {}, true, listing};
    lens::update::Settings settings;
    settings.source   = QUrl{QString::fromLatin1(kReleasePage)};
    settings.sumsName = QStringLiteral("SHA256SUMS");

    lens::update::DownloadClient client(settings, &manager);
    QByteArray received;
    bool answered = false;
    QObject::connect(&client, &lens::update::DownloadClient::sumsReady, [&](QByteArray sums) {
        answered = true;
        received = sums;
    });

    client.fetchSums(QStringLiteral("1.1.0"));
    settle();

    ASSERT_TRUE(answered);
    EXPECT_EQ(received, listing) << "readyRead drained the body and nothing kept it, so the list reached the parser empty";
}

TEST(UpdateDuty, AFailedChecksumReadWhileNothingIsDownloadingIsNotShownAsADownloadFailure)
{
    QtApplication qt;
    DutyFixture one(false);
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->failed(lens::update::DownloadClient::Failure::Generic);
    settle();

    EXPECT_NE(one.duty->download().value("state").toString(), QStringLiteral("failed"))
        << "the reader asked for nothing, so there is no download to have failed";
    EXPECT_TRUE(one.duty->cardVisible()) << "the card still offers View";
}

TEST(UpdateDuty, AReleaseWithNoChecksumForTheInstallerIsNotDownloadable)
{
    QtApplication qt;
    DutyFixture one(false);
    one.duty->checkForUpdates();
    one.answerWithNewer();

    emit one.downloader->sumsReady(QByteArray("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  other.zip\n"));
    settle();

    const QVariantMap state = one.duty->download();
    EXPECT_EQ(state.value("available").toBool(), false) << "the card keeps View only";
    EXPECT_EQ(state.value("state").toString(), QStringLiteral("idle"));
    EXPECT_EQ(one.downloadManager.requests, 1) << "nothing is fetched after a list that cannot check it";
}

TEST(UpdateDuty, ADownloadThatDoesNotMatchIsDeletedAndSaidSo)
{
    QtApplication qt;
    DutyFixture one(false);
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), "something else entirely"));
    settle();

    one.duty->downloadUpdate();
    QFile partial(one.partialAt());
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write("the installer as it arrived");
    partial.close();
    emit one.downloader->installerReady(one.partialAt());
    settle();

    const QVariantMap state = one.duty->download();
    EXPECT_EQ(state.value("state").toString(), QStringLiteral("failed"));
    EXPECT_EQ(state.value("failure").toString(), QStringLiteral("checksum"));
    EXPECT_FALSE(QFile::exists(one.partialAt())) << "a file that does not match is not left on disk";
    EXPECT_FALSE(QFile::exists(one.installerAt()));
}

TEST(UpdateDuty, ACheokedDownloadBecomesTheInstallerItClaimsToBe)
{
    QtApplication qt;
    DutyFixture one(false);
    const QByteArray contents("the installer as it arrived");
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), contents));
    settle();

    one.duty->downloadUpdate();
    QFile partial(one.partialAt());
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write(contents);
    partial.close();
    emit one.downloader->installerReady(one.partialAt());
    settle();

    EXPECT_EQ(one.duty->download().value("state").toString(), QStringLiteral("ready"));
    EXPECT_TRUE(QFile::exists(one.installerAt()));
    EXPECT_FALSE(QFile::exists(one.partialAt()));
}

TEST(UpdateDuty, CancellingDeletesTheHalfWrittenFileAndGoesBackToIdle)
{
    QtApplication qt;
    DutyFixture one(false);
    one.duty->checkForUpdates();
    one.answerWithNewer();

    one.duty->downloadUpdate();
    QFile partial(one.partialAt());
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write("half an installer");
    partial.close();

    one.duty->cancelDownload();

    EXPECT_EQ(one.duty->download().value("state").toString(), QStringLiteral("idle"));
    EXPECT_FALSE(QFile::exists(one.partialAt()));
}

TEST(UpdateDuty, InstallsOnlyWhatStillMatchesAtTheMomentItIsRun)
{
    QtApplication qt;
    DutyFixture one(false);
    const QByteArray contents("the installer as it arrived");
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), contents));
    settle();
    one.duty->downloadUpdate();

    QFile partial(one.partialAt());
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write(contents);
    partial.close();
    emit one.downloader->installerReady(one.partialAt());
    settle();
    ASSERT_EQ(one.duty->download().value("state").toString(), QStringLiteral("ready"));

    // The file is changed after it was checked: the launch must notice and refuse.
    QFile changed(one.installerAt());
    ASSERT_TRUE(changed.open(QIODevice::WriteOnly));
    changed.write("something else entirely");
    changed.close();

    one.duty->installUpdate();

    EXPECT_TRUE(one.launched.isEmpty()) << "a file that no longer matches must not be run";
    EXPECT_EQ(one.duty->download().value("failure").toString(), QStringLiteral("checksum"));
    EXPECT_FALSE(QFile::exists(one.installerAt())) << "nothing is left on disk to run by hand";
}

TEST(UpdateDuty, RunsTheCheckedInstallerWithNoArgumentsAtAll)
{
    QtApplication qt;
    DutyFixture one(false);
    const QByteArray contents("the installer as it arrived");
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), contents));
    settle();
    one.duty->downloadUpdate();

    QFile partial(one.partialAt());
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write(contents);
    partial.close();
    emit one.downloader->installerReady(one.partialAt());
    settle();

    one.duty->installUpdate();

    EXPECT_EQ(one.launched, one.installerAt());
    EXPECT_TRUE(one.arguments.isEmpty()) << "the installer is run as the reader would run it";
}

TEST(UpdateDuty, AnInstallerThatWillNotStartLeavesTheCardSayingSo)
{
    QtApplication qt;
    DutyFixture one(false);
    const QByteArray contents("the installer as it arrived");
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), contents));
    settle();
    one.duty->downloadUpdate();

    QFile partial(one.partialAt());
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write(contents);
    partial.close();
    emit one.downloader->installerReady(one.partialAt());
    settle();

    one.launchWorks = false;
    one.duty->installUpdate();

    EXPECT_EQ(one.launched, one.installerAt()) << "the start was attempted";
    EXPECT_EQ(one.duty->download().value("state").toString(), QStringLiteral("failed"))
        << "a launch that did not start is a failure the card shows, not a silent quit";
    EXPECT_EQ(one.duty->download().value("failure").toString(), QStringLiteral("generic"));
}

/// A check that lands mid-download names a different release. The transfer belongs to the
/// version it was started for, so it is given up on rather than allowed to land under the new
/// name: a file checked against one release's digest must never be run as another's.
TEST(UpdateDuty, ACheckLandingMidDownloadDoesNotStrandTheOldVersionUnderTheNewName)
{
    QtApplication qt;
    DutyFixture one(false);
    const QByteArray oldContents("the 1.2.0 installer as it arrived");
    one.duty->checkForUpdates();
    one.answerWithNewer();
    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), oldContents));
    settle();
    one.duty->downloadUpdate();
    settle();

    QFile partial(one.partialAt("1.2.0"));
    ASSERT_TRUE(partial.open(QIODevice::WriteOnly));
    partial.write(oldContents);
    partial.close();

    // A check lands while the transfer is still running, and names 1.3.0.
    one.duty->checkForUpdates();
    const auto newer = parseReleaseLocation(releaseUrl("v1.3.0"));
    ASSERT_TRUE(newer.has_value());
    emit one.client->finished(*newer);

    EXPECT_EQ(one.duty->download().value("state").toString(), QStringLiteral("idle"))
        << "the transfer for the old version was given up on";
    EXPECT_FALSE(QFile::exists(one.partialAt("1.2.0"))) << "its half-written file was deleted";

    // Even if the old transfer somehow completed afterwards, nothing is run as the new one.
    emit one.downloader->installerReady(one.partialAt("1.3.0"));
    settle();
    one.duty->installUpdate();

    EXPECT_TRUE(one.launched.isEmpty());
    EXPECT_FALSE(QFile::exists(one.installerAt("1.3.0"))) << "an old-version file must not take the new version's name";
}

TEST(UpdateDuty, ACheckNeverStartsADownloadOnItsOwn)
{
    QtApplication qt;
    DutyFixture one(false);
    one.duty->checkAtStartup();
    one.answerWithNewer();
    settle();

    // One request, and it is the small digest list that says whether a download is offered at
    // all. The installer itself is a second request, and only a press makes one.
    EXPECT_EQ(one.downloadManager.requests, 1);
    EXPECT_EQ(one.duty->download().value("state").toString(), QStringLiteral("idle"));
}

TEST(UpdateDuty, KnowsWhetherAReleaseIsDownloadableBeforeTheReaderAsks)
{
    QtApplication qt;
    DutyFixture one(false);
    one.duty->checkForUpdates();
    one.answerWithNewer();

    // The list has not arrived yet, so the card offers nothing rather than offering a download
    // that cannot be checked.
    EXPECT_EQ(one.duty->download().value("available").toBool(), false);

    emit one.downloader->sumsReady(DutyFixture::sumsFor(installerName("1.2.0"), QByteArray("the installer")));
    settle();

    EXPECT_EQ(one.duty->download().value("available").toBool(), true);
    EXPECT_EQ(one.duty->download().value("state").toString(), QStringLiteral("idle"));
    EXPECT_EQ(one.downloadManager.requests, 1) << "knowing is not downloading";
}
