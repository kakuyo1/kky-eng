#pragma once

#include <QString>
#include <QUrl>

#include <optional>

#include "util/semver.h"

/**
 * @file update_pure.h
 * @brief The network-free core of the update check: read the answer, decide what it means.
 *
 * Everything here is a function of its arguments. The date the once-a-day rule is judged
 * against arrives as a string rather than being read from the clock, so a test pins it; and the
 * answer arrives as one URL rather than as bytes this program would have to take apart, so what
 * is checked is what is used.
 */

namespace lens::update {

/// @brief One release, as far as this program acts on it.
struct Release {
    util::SemVer version; ///< The three numbers the tag carries, whatever the tag spelled.
    QString versionText;  ///< The same version written out, e.g. "1.2.0".
    QString pageUrl;      ///< The release page, which is the address the answer redirected to.
    QString notes;        ///< Always empty: the redirect carries no notes, and none are fetched.
};

/**
 * @brief Read the address a release page redirects to, and refuse anything else.
 *
 * The answer is a `Location` header, not a document: the release page's latest address replies
 * with a redirect to the newest tag, and that redirect is the whole answer. Nothing is fetched
 * behind it -- no HTML is read and no release body is asked for.
 *
 * Usable means: https, on `github.com`, under `/kakuyo1/lens/releases/tag/`, with a last path
 * segment that is a plain version. That last rule also covers the repository with no release
 * yet, which redirects to `/releases` rather than to a tag: no tag, no release, `offline`.
 *
 * @param location The redirect target, already resolved against the request URL.
 * @return The release, or nothing when the address is not one this program can act on.
 */
std::optional<Release> parseReleaseLocation(const QUrl& location);

/**
 * @brief Whether @p latest is newer than the version this build carries.
 *
 * @return True only for a strictly newer version: equal or older means there is nothing to
 *         tell the reader.
 */
bool isUpdateAvailable(const util::SemVer& latest, const util::SemVer& current);

/**
 * @brief The reader's update preferences, read out of the settings document.
 *
 * Every field is optional and every field is a whole object under the `UPDATE` key, so a
 * document written before the check existed reads as all-defaults rather than as an error.
 */
struct StoredUpdate {
    bool autoCheck = true; ///< Absent means on: a check nobody switched off is wanted.
    QString lastCheckDate; ///< Local `YYYY-MM-DD` of the last automatic check that started.
    QString skipped;       ///< The exact version the reader chose to skip, or empty.
};

/**
 * @brief Whether startup may check, judged against an explicit local date.
 *
 * A failed attempt still counts: the date is written when the check starts, so a machine with
 * no network does not retry on every launch.
 *
 * @param stored  What the document says.
 * @param today   Today's local date as `YYYY-MM-DD`.
 */
bool shouldCheckAutomatically(const StoredUpdate& stored, const QString& today);

/**
 * @brief Whether an automatic check that found a new version may raise the card.
 *
 * The skip is for that exact version: a newer one prompts again, which is the whole point of
 * storing a version rather than a flag.
 *
 * @param stored   What the document says.
 * @param latest   The version the answer named.
 * @param available Whether it is newer than this build at all.
 */
bool shouldPrompt(const StoredUpdate& stored, const QString& latest, bool available);

}