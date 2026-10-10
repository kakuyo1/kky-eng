#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>
#include <string>

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

/**
 * @brief The digest a `SHA256SUMS` asset publishes for one file.
 *
 * The file is the one `sha256sum` writes: a 64-character lowercase hex digest, two spaces,
 * and the name. Only an exact name match counts -- no prefix, no suffix -- and anything the
 * file cannot be read as is refused rather than skipped, because a list this check reads as
 * "verified" must not be a list it guessed at.
 *
 * @param sums     Raw bytes of the asset.
 * @param fileName The name to look for, exactly as the release names it.
 * @return The digest as 64 lowercase hex characters, or nothing when the file is not listed,
 *         when a line is malformed, or when the same name is listed twice with two digests.
 */
std::optional<std::string> expectedDigest(const QByteArray& sums, const QString& fileName);

/**
 * @brief The installer asset's name for a version.
 *
 * @param versionText A version that has already passed util::parseTag(); the caller refuses
 *                    anything else, so this only spells the name the release publishes it under.
 * @return `Lens-<version>-setup.exe`, or an empty string for an empty version.
 */
QString installerName(const QString& versionText);

/**
 * @brief Whether a download may be requested from this address.
 *
 * Three hosts, and only three: the release page itself, and the two asset hosts GitHub hands
 * out behind it. Everything else is refused, including a lookalike host and an address that
 * names a different repository, because a redirect this program follows is a request this
 * program makes with the reader's connection.
 *
 * @param url The address a redirect points at, already resolved.
 * @return True when the address is https, carries no userinfo and no fragment, is on one of
 *         the three hosts, and -- for `github.com` -- sits under this project's download path.
 */
bool isAllowedDownloadUrl(const QUrl& url);

/**
 * @brief Whether a file on disk hashes to a digest a release published.
 *
 * Streamed in chunks: an installer is tens of megabytes and this runs twice, once when the
 * download lands and once just before it is launched, so neither copy is ever held in memory.
 *
 * @param filePath    The file to hash.
 * @param expectedHex The digest to compare with, compared case-insensitively.
 * @return True only when the file could be read and its SHA256 is that digest. A missing or
 *         unreadable file is false, never an error the caller has to handle.
 */
bool digestMatches(const QString& filePath, const std::string& expectedHex);

}