/**
 * @file update_duty.cpp
 * @brief Owns the update check: when it runs, what it says, and what the reader chose.
 */

#include "update_duty.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "util/log.h"

namespace lens::app {

UpdateDuty::UpdateDuty(StorageDuty& storage,
                       update::UpdateClient& client,
                       update::DownloadClient& downloader,
                       const QString& currentVersion,
                       Launcher launcher,
                       QString downloadDir,
                       DateProvider dateProvider,
                       QObject* parent)
    : QObject(parent),
      storage_(storage),
      client_(client),
      downloader_(downloader),
      launcher_(std::move(launcher)),
      dateProvider_(std::move(dateProvider)),
      downloadDir_(std::move(downloadDir)),
      current_(currentVersion)
{
    if (not launcher_) {
        launcher_ = [](const QString& path, const QStringList& arguments) {
            // No arguments at all, and no shell: the installer is a program a reader ran on
            // purpose, not a silent step, so it opens its own window and asks.
            return QProcess::startDetached(path, arguments);
        };
    }
    connect(&client_, &update::UpdateClient::finished, this, [this](update::Release release) {
        inFlight_ = false;
        applyResult(std::move(release), manual_);
    });
    connect(&client_, &update::UpdateClient::offline, this, [this] {
        inFlight_ = false;
        reportOffline(manual_);
    });
    connect(&downloader_, &update::DownloadClient::sumsReady, this, &UpdateDuty::onSumsReady);
    connect(&downloader_, &update::DownloadClient::installerReady, this, &UpdateDuty::onInstallerReady);
    connect(&downloader_, &update::DownloadClient::progressed, this, [this](qint64 received, qint64 total) {
        downloadPercent_ = total > 0 ? static_cast<int>(received * 100 / total) : 0;
        emit downloadChanged();
    });
    connect(&downloader_, &update::DownloadClient::failed, this, [this](update::DownloadClient::Failure why) {
        if (cancelling_)
            return;
        // Only a download the reader asked for can fail as one. The checksum list is read without
        // the reader's asking, so its failure is logged and the card keeps View only.
        if (downloadState_ != QLatin1String("downloading") and downloadState_ != QLatin1String("verifying")) {
            LENS_WARN("the checksum list could not be read; the card keeps View only");
            return;
        }
        deletePartial();
        failDownload(why == update::DownloadClient::Failure::Proxy ? QStringLiteral("proxy") : QStringLiteral("generic"));
    });
    LENS_INFO("update check ready: current version {}", current_.toStdString());
}

QVariantMap UpdateDuty::update() const
{
    return QVariantMap{{QStringLiteral("state"), state_},
                       {QStringLiteral("current"), current_},
                       {QStringLiteral("latest"), latest_},
                       {QStringLiteral("notes"), notes_},
                       {QStringLiteral("pageUrl"), pageUrl_}};
}

update::StoredUpdate UpdateDuty::stored() const
{
    update::StoredUpdate settings;
    const nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object() or not document.contains("UPDATE") or not document.at("UPDATE").is_object())
        return settings;

    const nlohmann::json& section = document.at("UPDATE");
    if (section.contains("autoCheck") and section.at("autoCheck").is_boolean())
        settings.autoCheck = section.at("autoCheck").get<bool>();
    if (section.contains("lastCheckDate") and section.at("lastCheckDate").is_string())
        settings.lastCheckDate = QString::fromStdString(section.at("lastCheckDate").get<std::string>());
    if (section.contains("skipped") and section.at("skipped").is_string())
        settings.skipped = QString::fromStdString(section.at("skipped").get<std::string>());
    return settings;
}

void UpdateDuty::writeAutoCheck(bool on)
{
    nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object())
        return;
    document["UPDATE"]["autoCheck"] = on;
    storage_.save();
}

void UpdateDuty::writeLastCheckDate(const QString& date)
{
    nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object())
        return;
    document["UPDATE"]["lastCheckDate"] = date.toStdString();
    storage_.save();
}

void UpdateDuty::writeSkipped(const QString& version)
{
    nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object())
        return;
    document["UPDATE"]["skipped"] = version.toStdString();
    storage_.save();
}

void UpdateDuty::setAutoCheck(bool on)
{
    if (stored().autoCheck == on)
        return;
    writeAutoCheck(on);
    LENS_INFO("the startup update check is now {}", on ? "on" : "off");
    emit updateChanged();
}

bool UpdateDuty::autoCheck() const
{
    return stored().autoCheck;
}

void UpdateDuty::checkAtStartup()
{
    const QString today = dateProvider_().toString(Qt::ISODate);
    if (not update::shouldCheckAutomatically(stored(), today)) {
        LENS_TRACE("the startup update check was not due on {}", today.toStdString());
        return;
    }
    // Written before the answer is known: a machine that cannot reach the source must not
    // retry on every launch, and a failed attempt is still an attempt.
    writeLastCheckDate(today);
    startCheck(false);
}

void UpdateDuty::checkForUpdates()
{
    startCheck(true);
}

void UpdateDuty::startCheck(bool manual)
{
    if (inFlight_) {
        // A reader who pressed the button during the startup check is answered by the answer
        // already on its way, and it is answered as what they asked for: dropping the press
        // would leave the status row saying nothing on a dead network, and would keep the card
        // down for a version they had chosen to skip.
        manual_ = manual_ or manual;
        LENS_INFO("a check is already running; the answer will be reported as {}",
                  manual_ ? "asked for" : "automatic");
        return;
    }
    manual_   = manual;
    inFlight_ = true;
    state_    = QStringLiteral("checking");
    // The card is not taken down for a check the reader asked for. It is what they are reading,
    // and a window that blinks out and comes back in the middle of the screen is a card that
    // moved under their cursor. cardVisible() counts `checking` as still up, and the answer
    // decides: a newer version replaces what is on the card, anything else takes it down.
    emit updateChanged();
    LENS_INFO("checking for updates ({})", manual ? "asked for" : "at startup");
    client_.fetch();
}

void UpdateDuty::applyResult(update::Release const& release, bool manual)
{
    // A transfer belongs to the version it was started for. If this answer names a different
    // one, the transfer is given up on first: its half-written file, its digest and the name it
    // would land under all name the old release, and letting it finish would put a checked
    // file under a name it was never checked as.
    const bool downloading = downloadState_ == QLatin1String("downloading") or downloadState_ == QLatin1String("verifying");
    if (downloading and release.versionText != downloadVersion_) {
        LENS_INFO("a newer release arrived while {} was downloading; the transfer was cancelled", downloadVersion_.toStdString());
        cancelDownload();
    }
    // An installer that finished for one version is not offered under another's card, and is
    // not launched from one: the card's version and the file's version must be the same.
    const bool finished = downloadState_ == QLatin1String("ready") or downloadState_ == QLatin1String("failed");
    if (finished and release.versionText != downloadVersion_)
        discardDownload();
    // Recomputed: the cancel above may have just ended the transfer.
    const bool busy = downloadState_ == QLatin1String("downloading") or downloadState_ == QLatin1String("verifying");

    latest_  = release.versionText;
    notes_   = release.notes;
    pageUrl_ = release.pageUrl;

    const auto current   = util::parseTag(current_.toStdString());
    const bool available = current and update::isUpdateAvailable(release.version, *current);
    state_               = available ? QStringLiteral("available") : QStringLiteral("upToDate");

    // The automatic path is the one that may stay quiet, and `skipped` is compared against the
    // exact version -- so a newer release prompts again. The manual path reports whatever is
    // really there, and raises the card when there is something new to look at.
    cardVisible_ = available and (manual or update::shouldPrompt(stored(), latest_, available));

    // Whether this release can be downloaded at all is a fact about the release, and the card
    // cannot offer a download without it. It is one small request, and it is asked for only
    // when a newer version was found -- which is to say, only when the reader is about to be
    // shown a card at all. It fetches a list; it downloads nothing.
    // Not while a transfer is running: the state of that transfer is its own, and a new list
    // request would only replace the one it is still waiting on.
    // A verified installer keeps the answer it was checked against: a re-check of the same version
    // must not reset it, or a failed list read would turn a ready download back into idle.
    if (not busy and downloadState_ != QLatin1String("ready")) {
        sumsKnown_         = false;
        downloadAvailable_ = false;
        downloadWanted_    = false;
        if (available) {
            sumsFor_ = latest_;
            downloader_.fetchSums(latest_);
        }
    }

    LENS_INFO("update check answered: latest {} against {} -- {}", latest_.toStdString(), current_.toStdString(), available ? "newer" : "nothing new");
    emit updateChanged();
}

void UpdateDuty::reportOffline(bool manual)
{
    // A transfer for the version this answer just cleared has no card to belong to, and its list
    // would be dropped on arrival; it is given up on here, as a newer answer would give it up.
    if (downloadState_ == QLatin1String("downloading") or downloadState_ == QLatin1String("verifying"))
        cancelDownload();
    latest_.clear();
    notes_.clear();
    pageUrl_.clear();
    cardVisible_ = false;
    // An automatic check that could not reach the source leaves the state as it was: the reader
    // did not ask, so there is nothing to report. The manual one is the reader asking, and is
    // answered.
    state_ = manual ? QStringLiteral("offline") : QStringLiteral("idle");
    LENS_WARN("the update source could not be reached; {}",
              manual ? "reporting it" : "saying nothing, since nobody asked");
    emit updateChanged();
}

void UpdateDuty::skipUpdate()
{
    if (latest_.isEmpty())
        return;
    writeSkipped(latest_);
    cardVisible_ = false;
    LENS_INFO("skipped version {}", latest_.toStdString());
    emit updateChanged();
}

void UpdateDuty::openReleasePage()
{
    const QUrl url{pageUrl_};
    if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty()) {
        LENS_WARN("the release page is not an https URL with a host; nothing was opened");
        return;
    }
    cardVisible_ = false;
    emit updateChanged();
    LENS_INFO("opening the release page in the browser");
    QDesktopServices::openUrl(url);
}

void UpdateDuty::closeCard()
{
    if (not cardVisible_)
        return;
    cardVisible_ = false;
    emit updateChanged();
}

bool UpdateDuty::cardVisible() const
{
    // `checking` counts as up: a check that is under way does not retract what the card is
    // already saying, and the surfaces take the card down when the answer says otherwise.
    return cardVisible_ and (state_ == QLatin1String("available") or state_ == QLatin1String("checking"));
}

QVariantMap UpdateDuty::download() const
{
    return QVariantMap{{QStringLiteral("state"), downloadState_},
                       {QStringLiteral("percent"), downloadPercent_},
                       {QStringLiteral("available"), downloadAvailable_},
                       {QStringLiteral("failure"), downloadFailure_}};
}

QString UpdateDuty::downloadDirectory() const
{
    if (not downloadDir_.isEmpty())
        return downloadDir_;

    // The reader's own cache, not the install tree: an installer this program writes has no
    // business in a folder the program may not be allowed to write, and this is the one place
    // on the machine where a per-user file already belongs.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
    QDir().mkpath(directory);
    return directory;
}

QString UpdateDuty::installerPath() const
{
    const QString name = update::installerName(downloadVersion_);
    if (name.isEmpty())
        return {};
    return downloadDirectory() + QLatin1Char('/') + name;
}

QString UpdateDuty::partialPath() const
{
    const QString name = update::installerName(downloadVersion_);
    if (name.isEmpty())
        return {};
    return downloadDirectory() + QLatin1Char('/') + name + QStringLiteral(".part");
}

void UpdateDuty::deletePartial()
{
    const QString partial = partialPath();
    if (partial.isEmpty())
        return;
    QFile::remove(partial);
}

void UpdateDuty::discardDownload()
{
    // The file's name is this version's, so it is removed before the version is forgotten.
    QFile::remove(installerPath());
    deletePartial();
    expectedDigest_.clear();
    downloadVersion_.clear();
    resetDownload(QStringLiteral("idle"));
    LENS_INFO("the downloaded installer for another version was discarded");
}

void UpdateDuty::resetDownload(const QString& state)
{
    downloadState_   = state;
    downloadPercent_ = 0;
    if (state != QLatin1String("failed"))
        downloadFailure_.clear();
    emit downloadChanged();
}

void UpdateDuty::failDownload(const QString& kind)
{
    downloadFailure_ = kind;
    resetDownload(QStringLiteral("failed"));
    LENS_WARN("the update download failed ({})", kind.toStdString());
}

void UpdateDuty::downloadUpdate()
{
    // A download is a thing a reader asked for on a card they are looking at. The automatic
    // check never comes through here, and a check that has not found anything cannot start one.
    if (state_ != QLatin1String("available") or latest_.isEmpty()) {
        LENS_WARN("a download was asked for with no release on offer; nothing was requested");
        return;
    }
    if (downloadState_ == QLatin1String("downloading") or downloadState_ == QLatin1String("verifying"))
        return; // one download at a time

    // Remembered for the whole download: the file it writes, the digest it is checked against
    // and the name it lands under are all this version's, whatever a later check finds.
    downloadVersion_ = latest_;
    deletePartial(); // a half-written file from an earlier run is not an answer to anything
    resetDownload(QStringLiteral("downloading"));

    if (sumsKnown_) {
        if (not downloadAvailable_) {
            resetDownload(QStringLiteral("idle"));
            LENS_INFO("this release publishes no checksum; the card keeps View only");
            return;
        }
        downloader_.fetchInstaller(downloadVersion_, partialPath());
        return;
    }
    // The list this release publishes has not been read yet, so the press is remembered and
    // answered by the answer rather than guessing whether the download exists at all.
    downloadWanted_ = true;
    sumsFor_        = latest_;
    downloader_.fetchSums(latest_);
}

void UpdateDuty::cancelDownload()
{
    // Only a transfer in progress can be cancelled. A verified installer is kept until the reader
    // installs it or a newer answer discards it; cancelling it here would orphan it on disk.
    if (downloadState_ != QLatin1String("downloading") and downloadState_ != QLatin1String("verifying"))
        return;
    // The transfer reports its own stop through `failed`; that report is not a failure the reader
    // should see, because the reader asked for this one.
    cancelling_ = true;
    downloader_.cancel();
    cancelling_ = false;
    deletePartial();
    expectedDigest_.clear();
    downloadWanted_ = false;
    downloadVersion_.clear();
    resetDownload(QStringLiteral("idle"));
    LENS_INFO("the update download was cancelled");
}

void UpdateDuty::onSumsReady(QByteArray sums)
{
    // A list is only for the version it was asked for. A newer answer may have asked for another
    // one since, and that list is then not this card's list: it is ignored, and the one for the
    // current version is the one that counts.
    if (sumsFor_ != latest_)
        return;
    const auto digest = update::expectedDigest(sums, update::installerName(latest_));
    // Fail closed: a release that publishes no digest for this installer cannot be checked, and
    // a file this program cannot check is not one it downloads.
    sumsKnown_         = true;
    downloadAvailable_ = digest.has_value();
    expectedDigest_    = digest.value_or(std::string{});

    if (not downloadAvailable_) {
        downloadWanted_ = false;
        resetDownload(QStringLiteral("idle"));
        LENS_INFO("this release publishes no checksum for the installer; the card keeps View only");
        return;
    }

    emit downloadChanged();
    if (downloadWanted_) {
        // The reader pressed Download while the list was still being read.
        downloadWanted_  = false;
        downloadVersion_ = latest_;
        downloader_.fetchInstaller(downloadVersion_, partialPath());
    }
}

void UpdateDuty::onInstallerReady(QString path)
{
    resetDownload(QStringLiteral("verifying"));
    if (path.isEmpty() or expectedDigest_.empty() or not update::digestMatches(path, expectedDigest_)) {
        deletePartial();
        failDownload(QStringLiteral("checksum"));
        return;
    }

    // Checked, and only now a file with the name the release publishes it under. Any older
    // installer is replaced rather than kept beside it.
    const QString target = installerPath();
    QFile::remove(target);
    if (not QFile::rename(path, target)) {
        LENS_WARN("the checked installer could not be moved into place");
        QFile::remove(path); // a checked partial is not left behind where nothing will clean it up
        failDownload(QStringLiteral("generic"));
        return;
    }
    LENS_INFO("the installer for {} was downloaded and checked", latest_.toStdString());
    resetDownload(QStringLiteral("ready"));
}

void UpdateDuty::installUpdate()
{
    const QString path = installerPath();
    if (downloadState_ != QLatin1String("ready") or path.isEmpty()) {
        LENS_WARN("an install was asked for with nothing downloaded");
        return;
    }
    // Checked again, here, rather than trusted from the moment it landed: this is the last
    // point at which the program can refuse to run a file.
    if (expectedDigest_.empty() or not update::digestMatches(path, expectedDigest_)) {
        QFile::remove(path);
        failDownload(QStringLiteral("checksum"));
        return;
    }

    // A launch that did not start leaves Lens running with the card saying why, rather than
    // closing the application with nothing shown.
    if (not launcher_(path, QStringList{})) {
        LENS_WARN("the verified installer could not be started");
        failDownload(QStringLiteral("generic"));
        return;
    }
    LENS_INFO("the verified installer was started; Lens is closing");
    // The tray's own quit path, which is the application's exit.
    QCoreApplication::quit();
}

}