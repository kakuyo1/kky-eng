/**
 * @file update_pure.cpp
 * @brief The network-free core of the update check: read the answer, decide what it means.
 */

#include "update_pure.h"

#include <QCryptographicHash>
#include <QFile>

#include "util/log.h"

namespace lens::update {
namespace {

/// The one repository this check reads, and the one path under it a release tag lives at.
/// Both are checked rather than assumed: an address from anywhere else is not this project's
/// release page, and the reader's card would be opening someone else's page.
constexpr auto kReleaseHost = "github.com";
const QString kTagPath      = QStringLiteral("/kakuyo1/lens/releases/tag/");

/// Where this project's release assets are published. A download from `github.com` is only
/// ever this path: the check asks for a file it names, not for whatever the host serves.
const QString kDownloadPath = QStringLiteral("/kakuyo1/lens/releases/download/");

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

std::optional<std::string> expectedDigest(const QByteArray& sums, const QString& fileName)
{
    if (fileName.isEmpty()) {
        LENS_WARN("the checksum file was asked for a file with no name");
        return std::nullopt;
    }

    std::optional<std::string> found;
    const QList<QByteArray> lines = sums.split('\n');
    for (const QByteArray& raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty())
            continue;

        // Exactly what `sha256sum` writes: 64 hex, two spaces, the name. Anything else is a
        // file this check cannot read as a list of digests, and a list read as a list of
        // digests is what a download is checked against.
        if (line.size() < 67 or line.at(64) != ' ' or line.at(65) != ' ')
            return std::nullopt;
        for (int index = 0; index < 64; ++index) {
            const char digit = line.at(index);
            const bool hex   = (digit >= '0' and digit <= '9') or (digit >= 'a' and digit <= 'f');
            if (not hex)
                return std::nullopt;
        }

        const QByteArray name = line.mid(66);
        if (QString::fromUtf8(name) != fileName)
            continue;

        const std::string digest = line.left(64).toStdString();
        if (found and *found != digest) {
            // The same file listed twice with two digests: one of them is wrong, and this check
            // cannot tell which, so it refuses rather than picks.
            LENS_WARN("the checksum file lists {} twice with two digests", fileName.toStdString());
            return std::nullopt;
        }
        found = digest;
    }

    if (not found)
        LENS_WARN("the checksum file does not list {}", fileName.toStdString());
    return found;
}

QString installerName(const QString& versionText)
{
    if (versionText.isEmpty())
        return {};
    return QStringLiteral("Lens-%1-setup.exe").arg(versionText);
}

bool isAllowedDownloadUrl(const QUrl& url)
{
    if (not url.isValid() or url.scheme() != QLatin1String("https"))
        return false;
    // A userinfo in the address is a credential this program did not put there, and a fragment
    // is not sent anyway; both are reasons to refuse rather than to tidy up.
    if (not url.userInfo().isEmpty() or not url.fragment().isEmpty())
        return false;

    const QString host = url.host();
    if (host.compare(QLatin1String(kReleaseHost), Qt::CaseInsensitive) == 0)
        return url.path().startsWith(kDownloadPath);
    return host.compare(QLatin1String("release-assets.githubusercontent.com"), Qt::CaseInsensitive) == 0 or host.compare(QLatin1String("objects.githubusercontent.com"), Qt::CaseInsensitive) == 0;
}

bool digestMatches(const QString& filePath, const std::string& expectedHex)
{
    QFile file(filePath);
    if (not file.open(QIODevice::ReadOnly)) {
        LENS_WARN("the downloaded file could not be read for checking");
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    // Chunked rather than read whole: the installer is tens of megabytes and this runs twice.
    while (not file.atEnd()) {
        const QByteArray chunk = file.read(64 * 1024);
        if (chunk.isEmpty() and not file.atEnd())
            return false;
        hash.addData(chunk);
    }
    file.close();

    return QString::fromLatin1(hash.result().toHex()).compare(QString::fromStdString(expectedHex), Qt::CaseInsensitive) == 0;
}

}