#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <filesystem>

#include "update_pure.h"

class QNetworkAccessManager;

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
    int timeoutMs   = 10000;
    qint64 maxBytes = 2 * 1024 * 1024;

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

}