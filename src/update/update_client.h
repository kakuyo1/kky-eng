#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <filesystem>

#include "update_pure.h"

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @file update_client.h
 * @brief The one HTTP request the update check makes.
 *
 * One unauthenticated GET of a fixed release page, declared in `data/update.json`, with nothing
 * the reader owns on it: no key, no auth header, no query, no identifier, and no words or
 * selection text. The redirect that page answers with is the whole answer, and it is not
 * followed. Nothing is downloaded, installed or run -- the version decides whether a card is
 * worth putting up, and the card's action opens the release page in the browser.
 */

namespace lens::update {

/// @brief Where the check asks, and the ceilings it asks under, read from `data/update.json`.
struct Settings {
    QUrl source; ///< The release page; a non-https one is refused and nothing is sent.
    int timeoutMs           = 10000;
    qint64 maxBytes         = 2 * 1024 * 1024;
    QString sumsName        = QStringLiteral("SHA256SUMS"); ///< The asset that publishes the installer's digest.
    qint64 maxDownloadBytes = 200 * 1024 * 1024;            ///< Ceiling on one installer.
    int stallTimeoutMs      = 30000;                        ///< A transfer that stops moving is given up on.
    int connectTimeoutMs    = 15000;                        ///< A transfer that never starts is given up on.

    /**
     * @brief Read the source and its limits from `data/update.json`.
     *
     * @param path Normally `<repo>/data/update.json`.
     * @return The settings, with the source left null for a file that does not carry a usable
     *         one -- a null source is refused by the client exactly as a non-https one is, so a
     *         missing or malformed file costs the check and nothing else.
     */
    static Settings load(const std::filesystem::path& path);
};

/**
 * @brief Asks the release page once, and reports one of three answers.
 *
 * A failed attempt is `offline`, never a reason: the transport's own text stops here, so no
 * error string reaches the property the surfaces read.
 */
class UpdateClient : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Build a client, optionally over a caller-supplied network manager.
     *
     * The manager is the seam a test answers through, the same one LlmClient takes. Null makes
     * one owned by this object, which is what the application passes: the default manager's
     * proxy behaviour is the one this check wants, so nothing is set explicitly here.
     *
     * @param settings Source and limits.
     * @param manager Network manager to post through, or null to make one.
     * @param parent  QObject parent.
     */
    explicit UpdateClient(Settings settings, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief Ask the release page, once, and read the redirect it answers with.
    ///
    /// The redirect is not followed: its `Location` is the answer, and it is handed to
    /// parseReleaseLocation(). A non-https source is logged and answered `offline` without a
    /// request being made. There is no retry: one attempt per trigger, and the caller decides
    /// whether there is another.
    void fetch();

signals:
    /// @brief The answer, with the version and the page to open. The notes are always empty.
    void finished(Release release);

    /// @brief The release page could not be reached, answered, or trusted.
    ///
    /// Carries no text: the reason is in the log, and nothing here reaches a surface.
    void offline();

private:
    Settings settings_;
    QNetworkAccessManager* manager_;
    QString userAgent_;
};

/**
 * @brief Fetches the checksum list, then the installer, and reports progress and failure.
 *
 * Two requests: the `SHA256SUMS` asset, which decides whether this release can be downloaded at
 * all and is read whenever a newer version is found, and the installer itself, which is asked
 * for only when the reader presses Download. Redirects are walked by
 * hand -- at most five hops, and only to a host `isAllowedDownloadUrl()` accepts -- because
 * GitHub hands out a short-lived signed address on an asset host, and a request this program
 * makes on the reader's connection is one it has to be able to say no to.
 *
 * A failure is one of two kinds and never text: `Generic`, or `Proxy` when not one byte arrived
 * before the connection or the stall timer gave up. The second says what to try, not what went
 * wrong; the first says what to check. Neither is a transport's own words.
 */
class DownloadClient : public QObject {
    Q_OBJECT
public:
    /// @brief Why a download stopped. `Checksum` never comes from here: that is the duty's.
    enum class Failure { Generic,
                         Proxy };
    Q_ENUM(Failure)

    /**
     * @brief Build a client, optionally over a caller-supplied network manager.
     *
     * @param settings Source and limits, the same ones the check reads.
     * @param manager  Network manager to post through, or null to make one.
     * @param parent   QObject parent.
     */
    explicit DownloadClient(Settings settings, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief Fetch the digest list published for @p versionText.
    void fetchSums(const QString& versionText);

    /// @brief Fetch the installer for @p versionText into @p targetPath, a `.part` file.
    void fetchInstaller(const QString& versionText, const QString& targetPath);

    /// @brief Stop whatever is in flight. The half-written file is the duty's to delete.
    void cancel();

signals:
    /// @brief The `SHA256SUMS` asset arrived, whole or not; parsing decides what it means.
    void sumsReady(QByteArray sums);

    /// @brief Bytes so far, and the total when the server states one.
    void progressed(qint64 received, qint64 total);

    /// @brief The installer arrived whole, at @p path. Nothing has been checked or renamed.
    void installerReady(QString path);

    /// @brief The download stopped. No text: what the reader is told comes from the kind.
    void failed(Failure why);

private:
    /// @return What to report a stop that happened with @p received bytes. Zero bytes is the
    ///         proxy text; anything that moved is a plain failure.
    Failure failureFor(qint64 received) const;
    /// What one request is for: a digest list, or bytes into a file.
    enum class Mode { Sums,
                      Installer };

    /// @return False when a transfer is already in flight and this request was not made.
    bool start(const QUrl& url, Mode mode, int hops);
    void onFinished(QNetworkReply* reply, const QUrl& url, Mode mode, int hops);
    void stop(Failure why);
    /// Drops the transfer in flight without reporting anything, for a request that replaces it.
    void abandon();
    void closeFile();
    void stopTimers();
    /// @return The address one release asset is published at, never a URL this may not ask for.
    static QUrl assetUrl(const QString& subdirectory, const QString& name);

    Settings settings_;
    QNetworkAccessManager* manager_;
    QString userAgent_;
    QNetworkReply* reply_ = nullptr;
    QFile* file_          = nullptr;
    /// The checksum list as it arrives. readyRead drains the reply, so the bytes are kept here
    /// rather than left for finished() to read, where nothing would remain.
    QByteArray sums_;
    QTimer* stall_   = nullptr;
    QTimer* connect_ = nullptr;
    qint64 received_ = 0;
    qint64 total_    = 0;
    /// Whether this transfer has already been given up on, so one download reports once.
    bool stopped_ = false;
    /// The mode of the transfer in flight, so a replacement request knows what it is replacing.
    Mode activeMode_ = Mode::Installer;
};

}