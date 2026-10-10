/**
 * @file update_pure.cpp
 * @brief The network-free core of the update check: read the answer, decide what it means.
 */

#include "update_pure.h"

#include "util/log.h"

namespace lens::update {
namespace {

/// The one repository this check reads, and the one path under it a release tag lives at.
/// Both are checked rather than assumed: an address from anywhere else is not this project's
/// release page, and the reader's card would be opening someone else's page.
constexpr auto kReleaseHost = "github.com";
const QString kTagPath      = QStringLiteral("/kakuyo1/lens/releases/tag/");

} // namespace

std::optional<Release> parseReleaseLocation(const QUrl& location)
{
    if (not location.isValid() or location.scheme() != QLatin1String("https")) {
        LENS_WARN("the release address is not an https URL; the answer was refused");
        return std::nullopt;
    }
    if (location.host().compare(QLatin1String(kReleaseHost), Qt::CaseInsensitive) != 0) {
        LENS_WARN("the release address is not on {}; the answer was refused", kReleaseHost);
        return std::nullopt;
    }
    if (not location.path().startsWith(kTagPath)) {
        // The repository with no release yet redirects here rather than to a tag, which is the
        // one refusal that is a fact about the project rather than a fault.
        LENS_WARN("the release address is not under {}; the answer was refused", kTagPath.toStdString());
        return std::nullopt;
    }

    const QString tag  = location.path().mid(kTagPath.size());
    const auto version = util::parseTag(tag.toStdString());
    if (not version) {
        LENS_WARN("the release tag is not a plain three-number version; the answer was refused");
        return std::nullopt;
    }

    Release release;
    release.version     = *version;
    release.versionText = QStringLiteral("%1.%2.%3").arg(version->major).arg(version->minor).arg(version->patch);
    release.pageUrl     = location.toString();
    // No notes: the redirect carries none, and nothing behind it is fetched.
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