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

#include "util/log.h"

namespace lens::update {

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
    if (const int maxNotesChars = root.value("maxNotesChars").toInt(); maxNotesChars > 0)
        settings.maxNotesChars = maxNotesChars;
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
    // One source, declared in the data, asked for with nothing the reader owns: no key, no
    // query, no install identifier. A source that is not https is not asked at all -- the same
    // refusal LlmClient::fetchModels() makes, and for the same reason.
    if (not settings_.source.isValid() or settings_.source.scheme() != QLatin1String("https") or settings_.source.host().isEmpty()) {
        LENS_WARN("the update source in the data is not an https URL with a host; nothing was requested");
        emit offline();
        return;
    }

    QNetworkRequest request(settings_.source);
    request.setTransferTimeout(settings_.timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    LENS_TRACE("GET {}", settings_.source.toString().toStdString());

    QNetworkReply* reply  = manager_->get(request);
    const qint64 maxBytes = settings_.maxBytes;
    connect(reply, &QNetworkReply::downloadProgress, this, [reply, maxBytes](qint64 received, qint64) {
        if (maxBytes > 0 and received > maxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError or (status != 0 and status != 200)) {
            LENS_WARN("the update source was not fetched: HTTP {} ({})", status, reply->errorString().toStdString());
            emit offline();
            return;
        }
        const auto release = parseRelease(reply->readAll(), settings_.maxNotesChars);
        if (not release) {
            emit offline();
            return;
        }
        emit finished(*release);
    });
}

}