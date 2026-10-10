/**
 * @file update_client.cpp
 * @brief The one HTTP request the update check makes.
 */

#include "update_client.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <chrono>

#include "util/log.h"

namespace lens::update {
namespace {

/// How many redirects one download will follow. GitHub uses one; five is a ceiling, not a plan.
constexpr int kMaxHops = 5;

/// @return A URL with anything that could carry a credential taken out of it, for the log.
///
/// A signed asset address carries a query string that authorises the download, so it is never
/// logged whole: scheme, host and path say which file was asked for, which is what a log is for.
std::string forLog(const QUrl& url)
{
    return QStringLiteral("%1://%2%3").arg(url.scheme(), url.host(), url.path()).toStdString();
}

} // namespace

Settings Settings::load(const std::filesystem::path& path)
{
    LENS_TRACE("Settings::load: reading '{}'", path.string());

    QFile file(QString::fromStdString(path.string()));
    if (not file.open(QIODevice::ReadOnly)) {
        LENS_WARN("the update settings could not be read; no check will be made");
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (not document.isObject()) {
        LENS_WARN("the update settings are not a JSON object; no check will be made");
        return {};
    }

    const QJsonObject root = document.object();
    Settings settings;
    settings.source = QUrl{root.value("source").toString()};
    // A limit that is not a positive number is the default rather than zero: zero would mean
    // no timeout or no ceiling, which is the opposite of what the number was for.
    if (const int timeoutMs = root.value("timeoutMs").toInt(); timeoutMs > 0)
        settings.timeoutMs = timeoutMs;
    if (const auto maxBytes = static_cast<qint64>(root.value("maxBytes").toDouble()); maxBytes > 0)
        settings.maxBytes = maxBytes;
    if (root.contains("sumsName") and root.value("sumsName").isString())
        settings.sumsName = root.value("sumsName").toString();
    if (const auto maxDownloadBytes = static_cast<qint64>(root.value("maxDownloadBytes").toDouble()); maxDownloadBytes > 0)
        settings.maxDownloadBytes = maxDownloadBytes;
    if (const int stallTimeoutMs = root.value("stallTimeoutMs").toInt(); stallTimeoutMs > 0)
        settings.stallTimeoutMs = stallTimeoutMs;
    if (const int connectTimeoutMs = root.value("connectTimeoutMs").toInt(); connectTimeoutMs > 0)
        settings.connectTimeoutMs = connectTimeoutMs;
    return settings;
}

UpdateClient::UpdateClient(Settings settings, QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      settings_(std::move(settings)),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1").arg(QString::fromUtf8(LENS_VERSION)))
{
    LENS_TRACE("UpdateClient created: source='{}' timeout={}ms", settings_.source.toString().toStdString(), settings_.timeoutMs);
}

void UpdateClient::fetch()
{
    // One release page, declared in the data, asked for with nothing the reader owns: no key, no
    // query, no install identifier. A source that is not https is not asked at all -- the same
    // refusal LlmClient::fetchModels() makes, and for the same reason.
    if (not settings_.source.isValid() or settings_.source.scheme() != QLatin1String("https") or settings_.source.host().isEmpty()) {
        LENS_WARN("the release page in the data is not an https URL with a host; nothing was requested");
        emit offline();
        return;
    }

    QNetworkRequest request(settings_.source);
    request.setTransferTimeout(settings_.timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    // The redirect is the answer, so it is not followed. Following it would fetch the release
    // page to read a version this check has already been handed in a header.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    LENS_TRACE("GET {}", settings_.source.toString().toStdString());

    QNetworkReply* reply  = manager_->get(request);
    const qint64 maxBytes = settings_.maxBytes;
    connect(reply, &QNetworkReply::downloadProgress, this, [reply, maxBytes](qint64 received, qint64) {
        if (maxBytes > 0 and received > maxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError or status != 302) {
            // Anything that is not the redirect is `offline`: a 200 means something answered
            // that was not this release page, and a rate limit or a missing release is a fact
            // the reader cannot act on either.
            LENS_WARN("the release page did not redirect: HTTP {} (error {})", status, static_cast<int>(reply->error()));
            emit offline();
            return;
        }
        const QVariant raw = reply->header(QNetworkRequest::LocationHeader);
        if (not raw.isValid()) {
            LENS_WARN("the release page redirected without saying where to");
            emit offline();
            return;
        }
        // The header may be relative; the parser sees the address the redirect really names.
        const auto release = parseReleaseLocation(settings_.source.resolved(QUrl{raw.toUrl()}));
        if (not release) {
            emit offline();
            return;
        }
        emit finished(*release);
    });
}

DownloadClient::DownloadClient(Settings settings, QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      settings_(std::move(settings)),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1").arg(QString::fromUtf8(LENS_VERSION))),
      stall_(new QTimer(this)),
      connect_(new QTimer(this))
{
    stall_->setSingleShot(true);
    connect_->setSingleShot(true);

    // A timer that runs out is the same judgement as a transfer timeout: an answer that arrived
    // with a status is a server's answer and never the proxy text, whatever it went on to do.
    const auto giveUp = [this] {
        const int status = reply_ != nullptr ? reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
        stop(status == 0 ? failureFor(received_) : Failure::Generic);
    };
    connect(stall_, &QTimer::timeout, this, giveUp);
    connect(connect_, &QTimer::timeout, this, giveUp);
}

QUrl DownloadClient::assetUrl(const QString& subdirectory, const QString& name)
{
    return QUrl{QStringLiteral("https://github.com/kakuyo1/lens/releases/download/%1/%2").arg(subdirectory, name)};
}

void DownloadClient::fetchSums(const QString& versionText)
{
    const QUrl url = assetUrl(QStringLiteral("v%1").arg(versionText), settings_.sumsName);
    if (not isAllowedDownloadUrl(url)) {
        LENS_WARN("the checksum file's address is not one this check may ask for");
        emit failed(Failure::Generic);
        return;
    }
    // A list asked for again replaces the one still in flight. It is given up on quietly: the
    // answer it was for is no longer the one the card shows, so it must not be reported.
    if (reply_ != nullptr and activeMode_ == Mode::Sums)
        abandon();
    if (not start(url, Mode::Sums, 0)) {
        LENS_WARN("the checksum list was not asked for: another transfer is still running");
        emit failed(Failure::Generic);
    }
}

void DownloadClient::abandon()
{
    stopped_             = true;
    QNetworkReply* reply = reply_;
    reply_               = nullptr;
    if (reply != nullptr) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    sums_.clear();
    stopTimers();
}

void DownloadClient::fetchInstaller(const QString& versionText, const QString& targetPath)
{
    const QUrl url = assetUrl(QStringLiteral("v%1").arg(versionText), update::installerName(versionText));
    if (not isAllowedDownloadUrl(url)) {
        LENS_WARN("the installer's address is not one this check may ask for");
        emit failed(Failure::Generic);
        return;
    }
    if (not start(url, Mode::Installer, 0)) {
        // The file is not touched: a refused start must not truncate a partial that belongs to
        // a transfer still in flight.
        LENS_WARN("the installer was not asked for: another transfer is still running");
        emit failed(Failure::Generic);
        return;
    }

    // The bytes land beside the target, never on it: a half-written installer is not an
    // installer, and this is the file the duty renames once the digest has been checked.
    file_ = new QFile(targetPath + QStringLiteral(".part"), this);
    if (not file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LENS_WARN("the download file could not be opened for writing");
        stop(Failure::Generic);
    }
}

void DownloadClient::cancel()
{
    stop(Failure::Generic);
}

bool DownloadClient::start(const QUrl& url, Mode mode, int hops)
{
    // A transfer already in flight is never replaced silently: the caller learns it was refused.
    if (reply_ != nullptr)
        return false;

    QNetworkRequest request(url);
    request.setTransferTimeout(settings_.timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    LENS_INFO("GET {}", forLog(url));

    received_ = 0;
    total_    = 0;
    stopped_  = false;
    if (hops == 0) {
        sums_.clear();
        connect_->start(settings_.connectTimeoutMs);
        if (mode == Mode::Installer)
            stall_->start(settings_.stallTimeoutMs);
    }

    reply_      = manager_->get(request);
    activeMode_ = mode;
    // A checksum list is a few lines: it gets the small ceiling, not the installer's.
    const qint64 cap     = mode == Mode::Sums ? settings_.maxBytes : settings_.maxDownloadBytes;
    QNetworkReply* reply = reply_;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, cap](qint64 got, qint64 total) {
        received_ = got;
        if (total > 0)
            total_ = total;
        connect_->stop(); // the connection is made; the stall timer takes over from here
        stall_->start(settings_.stallTimeoutMs);
        if (total > 0 and total > cap) {
            LENS_WARN("the download announced {} bytes, over the ceiling", total);
            reply->abort();
            return;
        }
        if (got > cap) {
            LENS_WARN("the download passed the ceiling at {} bytes", got);
            reply->abort();
            return;
        }
        emit progressed(received_, total_);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, mode] {
        connect_->stop();
        stall_->start(settings_.stallTimeoutMs);
        const QByteArray chunk = reply->readAll();
        // A redirect's body is not the file: it is read and dropped, so it never lands in the
        // installer or the checksum list.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 and status < 400)
            return;
        // The byte count and the ceiling are kept by downloadProgress, the one place that counts,
        // so nothing is counted twice here; this handler only stores the bytes.
        if (mode == Mode::Sums)
            sums_.append(chunk);
        if (file_ != nullptr and not file_->write(chunk)) {
            LENS_WARN("the download could not be written to disk");
            reply->abort();
            return;
        }
        emit progressed(received_, total_);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, mode, hops] { onFinished(reply, url, mode, hops); });
    return true;
}

void DownloadClient::onFinished(QNetworkReply* reply, const QUrl& url, Mode mode, int hops)
{
    const int status    = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool redirect = status >= 300 and status < 400;

    if (reply->error() == QNetworkReply::NoError and redirect) {
        const QVariant raw = reply->header(QNetworkRequest::LocationHeader);
        const QUrl next    = url.resolved(QUrl{raw.toUrl()});
        if (raw.isValid() and hops < kMaxHops and isAllowedDownloadUrl(next)) {
            LENS_INFO("following the redirect to {}", forLog(next));
            reply->deleteLater();
            reply_ = nullptr;
            if (not start(next, mode, hops + 1))
                emit failed(Failure::Generic);
            return;
        }
        // A redirect this program will not follow is a refusal, not a failure to retry: the
        // target is somewhere its download has no business going.
        LENS_WARN("the download was redirected somewhere this check will not follow: {}", forLog(next));
        stop(Failure::Generic);
        return;
    }

    // A release that publishes no checksum file is a release whose installer cannot be checked, so
    // it is not downloadable. That is an answer, not a failure: the list is read on every
    // newer-version check, and reporting its absence as a failed download would be wrong.
    if (mode == Mode::Sums and status == 404) {
        LENS_INFO("this release publishes no {} file", settings_.sumsName.toStdString());
        reply->deleteLater();
        reply_ = nullptr;
        stopTimers();
        emit sumsReady(QByteArray{});
        return;
    }

    if (reply->error() != QNetworkReply::NoError or status != 200) {
        // A status is an answer, whatever it says: a 404 is a release with no installer, and a
        // 5xx is the server's problem. Neither is the network, and neither is fixed by turning
        // a proxy on, so neither is reported as one. A transfer that failed without any HTTP
        // answer at all is the connection itself, and that is what `failureFor` judges: nothing
        // moved, so it is the proxy text; something moved and then stopped, it is a plain failure.
        // The error text is not logged: for an asset it carries the signed address, and that
        // address is a credential for the download. The status and the Qt error code are enough.
        LENS_WARN("the download did not arrive: HTTP {} (error {})", status, static_cast<int>(reply->error()));
        stop(status == 0 ? failureFor(received_) : Failure::Generic);
        return;
    }

    if (mode == Mode::Sums) {
        // readyRead has already taken the bytes it was given; what is left is the tail.
        const QByteArray sums = sums_ + reply->readAll();
        sums_.clear();
        reply->deleteLater();
        reply_ = nullptr;
        stopTimers();
        emit sumsReady(sums);
        return;
    }

    // The installer: whatever was written is now whole, and the duty checks it.
    if (file_ != nullptr and not file_->flush()) {
        LENS_WARN("the download could not be flushed to disk");
        stop(Failure::Generic);
        return;
    }
    const QString path = file_ != nullptr ? file_->fileName() : QString{};
    closeFile();
    reply->deleteLater();
    reply_ = nullptr;
    stopTimers();
    emit installerReady(path);
}

void DownloadClient::stop(Failure why)
{
    // One stop per transfer. abort() finishes the reply synchronously, so without this the
    // finished handler would come back in here and emit a second failure for one download.
    if (stopped_)
        return;
    stopped_ = true;

    QNetworkReply* reply = reply_;
    reply_               = nullptr;
    if (reply != nullptr) {
        // Disconnected before the abort: the finished handler belongs to a transfer that has
        // already been given up on.
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    closeFile();
    stopTimers();
    emit failed(why);
}

void DownloadClient::closeFile()
{
    if (file_ == nullptr)
        return;
    file_->close();
    // deleteLater, not delete: the file is a child of this object, and deleting it outright
    // would leave it in the children list for the destructor to delete a second time.
    file_->deleteLater();
    file_ = nullptr;
}

void DownloadClient::stopTimers()
{
    stall_->stop();
    connect_->stop();
}

DownloadClient::Failure DownloadClient::failureFor(qint64 received) const
{
    // Only a transfer that never got a byte is worth the proxy text: that is what a blocked route
    // looks like. One that moved and then stopped is a network problem of another kind, and a
    // reader on a slow proxy who waits is not told to change something they have already changed.
    if (received == 0)
        return Failure::Proxy;
    return Failure::Generic;
}

}