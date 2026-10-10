#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

#include "util/semver.h"

/**
 * @file update_pure.h
 * @brief The network-free core of the update check: read the answer, decide what it means.
 *
 * Everything here is a function of its arguments. The date the once-a-day rule is judged
 * against arrives as a string rather than being read from the clock, so a test pins it; and
 * the response arrives as raw bytes, so the parse is exercised against exactly what the
 * source sends rather than against a struct this program built itself.
 */

namespace lens::update {

/// @brief One release, as far as this program acts on it.
struct Release {
    util::SemVer version; ///< The three numbers the tag carries, whatever the tag spelled.
    QString versionText;  ///< The same version written out, e.g. "1.2.0".
    QString pageUrl;      ///< The release page, https and with a host, or the parse failed.
    QString notes;        ///< First paragraph of the release body, truncated, or empty.
};

/**
 * @brief Read a `releases/latest` answer, and refuse anything this program cannot use.
 *
 * Required: `tag_name`, which must parse as a plain version, and `html_url`, which must be an
 * https URL with a host. Either missing or of the wrong type rejects the whole answer rather
 * than yielding a half-release: a card that names a version with no page to open is worse than
 * no card. Optional: `body`, which may be null, and whose first paragraph becomes the notes.
 *
 * Drafts and pre-releases are not in a `latest` answer at all, so nothing here filters them
 * a second time.
 *
 * @param body           Raw response bytes, treated as untrusted.
 * @param maxNotesChars  Ceiling on the notes, cut with an ellipsis when it bites.
 * @return The release, or nothing when the answer is not one.
 */
std::optional<Release> parseRelease(const QByteArray& body, int maxNotesChars);

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