#pragma once

#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

#include "storage_duty.h"
#include "update/update_client.h"
#include "update/update_pure.h"

/**
 * @file update_duty.h
 * @brief Owns the update check and the download: when each runs, what it says, what it wrote.
 *
 * The rules live in update_pure.h, the check's request in UpdateClient and the download's in
 * DownloadClient; this is the QObject that holds the state the surfaces read, writes the two
 * facts worth remembering -- the day the last automatic check started, and the exact version
 * the reader chose to skip -- and decides what a downloaded file is allowed to become.
 *
 * A download is only ever started from a card the reader is looking at, and a file is only
 * ever launched after its digest has been checked again, at the moment of the launch.
 */

namespace lens::app {

/// @brief How a downloaded file starts, for the tests that drive this duty without a socket.
using Launcher = std::function<bool(const QString& path, const QStringList& arguments)>;

class UpdateDuty final : public QObject {
    Q_OBJECT
public:
    using DateProvider = std::function<QDate()>;

    /**
     * @brief Bind the check to the settings document and the clients that ask.
     *
     * The clients are taken rather than built for the same reason CostDuty takes an LlmClient:
     * they are the seam a test answers through, so the duty's rules -- which date a check may
     * run on, what a skipped version suppresses, what a download may become -- are reachable
     * offline.
     *
     * @param storage      The document owner; `UPDATE` is this duty's section of it.
     * @param client       The client that makes the one check request, owned by the caller.
     * @param downloader   The client that fetches a digest list and an installer.
     * @param currentVersion The version this build carries, as `major.minor.patch`.
     * @param launcher     How a verified installer is started; defaults to a detached process
     *                     with no arguments, and exists so a test can record the call instead
     *                     of running one.
     * @param downloadDir  Where the installer is cached; empty means the reader's own local
     *                     cache, and a test passes a directory of its own so it writes nothing
     *                     outside the run.
     * @param dateProvider Today's local date; a test pins it.
     * @param parent       QObject parent.
     */
    UpdateDuty(StorageDuty& storage, update::UpdateClient& client, update::DownloadClient& downloader, const QString& currentVersion, Launcher launcher = {}, QString downloadDir = {}, DateProvider dateProvider = [] { return QDate::currentDate(); }, QObject* parent = nullptr);

    /**
     * @brief The state the card and the settings page read.
     *
     * @c state `idle`, `checking`, `upToDate`, `available` or `offline`; @c current and
     * @c latest are written as plain `major.minor.patch`; @c notes and @c pageUrl are empty
     * unless a release carried them. Nothing here is ever a transport's own error text.
     */
    QVariantMap update() const;

    /**
     * @brief The download's state: `idle`, `downloading`, `verifying`, `ready` or `failed`.
     *
     * @c percent is 0–100 and only means something while `downloading`; @c available says
     * whether this release publishes the digest the download is checked against, which is what
     * decides the Download button; @c failure is `generic`, `proxy` or `checksum` while the
     * state is `failed`, and empty otherwise.
     */
    QVariantMap download() const;

    /// @brief Check now, on the reader's behalf, and report whatever the real answer is.
    ///
    /// A manual check is not the automatic one: `skipped` does not suppress it, and `offline`
    /// is reported rather than swallowed, because the reader pressed the button.
    void checkForUpdates();

    /// @brief Remember the version now on offer as skipped, and take the card down.
    ///
    /// Skipping a version stops the automatic prompt for that version and not for any other:
    /// a newer one prompts again, which is why a version and not a flag is stored.
    void skipUpdate();

    /// @brief Open the release page in the browser, and take the card down.
    ///
    /// Refuses a page that is not https, so a malformed answer cannot turn the card into an
    /// arbitrary launch. Launches nothing itself: the browser opens the page and nothing else.
    void openReleasePage();

    /// @brief Take the card down without touching anything stored.
    void closeCard();

    /// @brief Whether the card is up, which is the flag and the state together.
    bool cardVisible() const;

    /// @brief Set whether startup may check; read back by the settings page's switch.
    void setAutoCheck(bool on);

    /// @return Whether startup may check.
    bool autoCheck() const;

    /// @brief Run the automatic check if today's date and the stored switch allow it.
    ///
    /// Called once after the settings have finished loading. A start is written to the
    /// document before the answer is known, so a machine that cannot reach the source does not
    /// retry on every launch. It never starts a download: nothing is fetched on the reader's
    /// behalf but the version.
    void checkAtStartup();

    /// @brief Fetch the checksum list for the version now on offer.
    ///
    /// Only from a card the reader is looking at, and only for a release that was found: a
    /// download is a thing a reader asked for, so the automatic check never starts one. A
    /// release that publishes no digest for the installer is simply not downloadable, and the
    /// card keeps View.
    void downloadUpdate();

    /// @brief Stop the transfer and delete the half-written file.
    void cancelDownload();

    /// @brief Run the installer that has been checked, and close Lens.
    ///
    /// The digest is recomputed here, at the moment of the launch, and the file is only run
    /// when it still matches. A file that no longer matches is deleted rather than reported
    /// on, so there is nothing on disk to run by hand afterwards.
    void installUpdate();

signals:
    /// @brief The state, the versions, or the card's visibility changed.
    void updateChanged();

    /// @brief The download's state, its percentage, or whether it can be downloaded changed.
    void downloadChanged();

private:
    /// @brief Read the `UPDATE` section, filling in every default.
    update::StoredUpdate stored() const;
    /// @brief Write one field of the `UPDATE` section, leaving the other two alone.
    ///
    /// The section is one object, so a write has to reach into it rather than go through
    /// StorageDuty::writeDocument(), which writes a top-level scalar.
    void writeAutoCheck(bool on);
    void writeLastCheckDate(const QString& date);
    void writeSkipped(const QString& version);
    void startCheck(bool manual);
    void applyResult(update::Release const& release, bool manual);
    void reportOffline(bool manual);

    /// @brief Where a downloaded installer lives: the reader's own cache, nothing else.
    QString downloadDirectory() const;
    /// @return The path the installer lands on once checked, empty when no download was started.
    QString installerPath() const;
    /// @brief The `.part` beside it, which is the only file a transfer ever writes to.
    QString partialPath() const;
    void deletePartial();
    void onSumsReady(QByteArray sums);
    void onInstallerReady(QString path);
    void failDownload(const QString& kind);
    /// Removes a finished download for a version the card no longer shows.
    void discardDownload();
    void resetDownload(const QString& state);

    StorageDuty& storage_;
    update::UpdateClient& client_;
    update::DownloadClient& downloader_;
    Launcher launcher_;
    DateProvider dateProvider_;
    QString downloadDir_;
    QString current_;
    QString latest_;
    QString notes_;
    QString pageUrl_;
    QString state_    = QStringLiteral("idle");
    bool cardVisible_ = false;
    bool inFlight_    = false;
    /// Whether the request in flight is the one the reader asked for, which decides what a
    /// failure says: an automatic check that fails says nothing, a manual one answers.
    bool manual_ = true;

    QString downloadState_ = QStringLiteral("idle");
    QString downloadFailure_;
    std::string expectedDigest_;
    int downloadPercent_    = 0;
    bool downloadAvailable_ = false;
    /// Whether this release's digest list has been read, which is what `downloadAvailable_`
    /// means: before it is, the card offers nothing rather than offering something untested.
    bool sumsKnown_ = false;
    /// Whether a press is waiting for that list to arrive.
    bool downloadWanted_ = false;
    /// The version the download in flight is for. Everything the download touches on disk and
    /// every digest it is checked against is this one, never `latest_`: a check that lands while
    /// a transfer is running names a different release, and a file must not be renamed into it.
    QString downloadVersion_;
    /// The version the checksum list in flight was asked for, checked against the card's version.
    QString sumsFor_;
    /// Set while cancelDownload() stops the transfer, so its own stop report is not a failure.
    bool cancelling_ = false;
};

}
