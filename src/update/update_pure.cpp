/**
 * @file update_pure.cpp
 * @brief The network-free core of the update check: read the answer, decide what it means.
 */

#include "update_pure.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QUrl>

#include <string>

#include "util/log.h"

namespace lens::update {
namespace {

/// @return The body up to the first blank line, trimmed, and no longer than @p maxChars.
///
/// A release body is a change log: the first paragraph is what changed, and everything under
/// it is a list the card has no room for. Cut mid-word is preferred to cut silently -- the
/// ellipsis says the sentence continues rather than pretending it ended there.
QString firstParagraph(const QString& body, int maxChars)
{
    QString paragraph;
    // Split on the newline alone, so a body that came back with CRLF ends does not carry a
    // carriage return into the middle of every line the card draws.
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            break;
        if (not paragraph.isEmpty())
            paragraph += QLatin1Char('\n');
        paragraph += line;
    }
    paragraph = paragraph.trimmed();
    if (maxChars > 0 and paragraph.size() > maxChars)
        paragraph = paragraph.left(maxChars).trimmed() + QStringLiteral("...");
    return paragraph;
}

} // namespace

std::optional<Release> parseRelease(const QByteArray& body, int maxNotesChars)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError or not document.isObject()) {
        LENS_WARN("the release answer was not a JSON object ({})", error.errorString().toStdString());
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (not root.value("tag_name").isString() or not root.value("html_url").isString()) {
        LENS_WARN("the release answer carried no tag_name or html_url string");
        return std::nullopt;
    }

    const auto version = util::parseTag(root.value("tag_name").toString().toStdString());
    if (not version) {
        LENS_WARN("the release tag is not a plain three-number version; the answer was refused");
        return std::nullopt;
    }

    const QUrl pageUrl{root.value("html_url").toString()};
    if (not pageUrl.isValid() or pageUrl.scheme() != QLatin1String("https") or pageUrl.host().isEmpty()) {
        LENS_WARN("the release page is not an https URL with a host; the answer was refused");
        return std::nullopt;
    }

    // A null body is a release with nothing written under it, which is a release rather than a
    // malformed answer: the card shows the version and nothing else.
    const QJsonValue notes = root.value("body");
    const QString text     = notes.isString() ? notes.toString() : QString{};

    Release release;
    release.version     = *version;
    release.versionText = QStringLiteral("%1.%2.%3").arg(version->major).arg(version->minor).arg(version->patch);
    release.pageUrl     = pageUrl.toString();
    release.notes       = firstParagraph(text, maxNotesChars);
    return release;
}

bool isUpdateAvailable(const util::SemVer& latest, const util::SemVer& current)
{
    return latest > current;
}

bool shouldCheckAutomatically(const StoredUpdate& stored, const QString& today)
{
    return stored.autoCheck and stored.lastCheckDate != today;
}

bool shouldPrompt(const StoredUpdate& stored, const QString& latest, bool available)
{
    return available and stored.skipped != latest;
}

}