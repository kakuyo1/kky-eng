#pragma once

#include <QDate>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

#include "storage_duty.h"
#include "update/update_client.h"
#include "update/update_pure.h"

/**
 * @file update_duty.h
 * @brief Owns the update check: when it runs, what it says, and what the reader chose.
 *
 * The rules live in update_pure.h and the request in UpdateClient; this is the QObject that
 * holds the state the surfaces read and writes the two facts worth remembering -- the day the
 * last automatic check started, and the exact version the reader chose to skip.
 *
 * Nothing is downloaded, installed or run. The card's action opens the release page in the
 * browser and stops there.
 */

namespace lens::app {

class UpdateDuty final : public QObject {
    Q_OBJECT
public:
    using DateProvider = std::function<QDate()>;

    /**
     * @brief Bind the check to the settings document and the client that asks.
     *
     * The client is taken rather than built for the same reason CostDuty takes an LlmClient:
     * it is the seam a test answers through, so the duty's rules -- which date a check may run
     * on, what a skipped version suppresses, what is written where -- are reachable offline.
     *
     * @param storage      The document owner; `UPDATE` is this duty's section of it.
     * @param client       The client that makes the one request, owned by the caller.
     * @param currentVersion The version this build carries, as `major.minor.patch`.
     * @param dateProvider Today's local date; a test pins it.
     * @param parent       QObject parent.
     */
    UpdateDuty(StorageDuty& storage, update::UpdateClient& client, const QString& currentVersion, DateProvider dateProvider = [] { return QDate::currentDate(); }, QObject* parent = nullptr);

    /**
     * @brief The state the card and the settings page read.
     *
     * @c state `idle`, `checking`, `upToDate`, `available` or `offline`; @c current and
     * @c latest are written as plain `major.minor.patch`; @c notes and @c pageUrl are empty
     * unless a release carried them. Nothing here is ever a transport's own error text.
     */
    QVariantMap update() const;

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
    /// retry on every launch.
    void checkAtStartup();

signals:
    /// @brief The state, the versions, or the card's visibility changed.
    void updateChanged();

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

    StorageDuty& storage_;
    update::UpdateClient& client_;
    DateProvider dateProvider_;
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
};

}
