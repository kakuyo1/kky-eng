#include "llm_client.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <utility>
#include <variant>

#include "core/log.h"
#include "llm_pure.h"

namespace lens::llm {
namespace {

constexpr int kMaxWords             = 20; ///< Contract limit.
constexpr int kTimeoutMs            = 30000;
constexpr int kModelListTimeoutMs   = 10000;
constexpr qint64 kMaxModelListBytes = 2 * 1024 * 1024;

} // namespace

LlmClient::LlmClient(Config config, QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      config_(std::move(config)),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this))
{
    LENS_TRACE("LlmClient created: model='{}' base='{}'", config_.model.toStdString(), config_.baseUrl.toString().toStdString());
}

void LlmClient::setExplanationLang(const QString& lang)
{
    LENS_TRACE("LlmClient::setExplanationLang: '{}' -> '{}'", lang_.toStdString(), lang.toStdString());
    lang_ = lang;
}

void LlmClient::setModel(const QString& model)
{
    LENS_TRACE("LlmClient::setModel: '{}' -> '{}'", config_.model.toStdString(), model.toStdString());
    config_.model = model;
}

void LlmClient::setBaseUrl(const QUrl& baseUrl)
{
    LENS_TRACE("LlmClient::setBaseUrl: '{}' -> '{}'", config_.baseUrl.toString().toStdString(), baseUrl.toString().toStdString());
    config_.baseUrl = baseUrl;
}

void LlmClient::setApiKey(const QString& apiKey)
{
    config_.apiKey = apiKey;
    LENS_TRACE("LlmClient::setApiKey: key replaced (value hidden)");
}

void LlmClient::setChannel(Channel channel)
{
    LENS_TRACE("LlmClient::setChannel: '{}' -> '{}'", channelKey(channel_), channelKey(channel));
    channel_ = channel;
}

bool LlmClient::setProvider(QString const& provider)
{
    auto const entry = serviceProvider(provider);
    if (entry.isEmpty()) return false;
    config_.requestOverrides = entry.value("requestOverrides").toObject();
    provider_                = provider;
    // The provider duty reconciles the current model with this service's cached or seeded list.
    if (provider != QLatin1String("custom"))
        setBaseUrl(QUrl{entry.value("baseUrl").toString()});
    return true;
}

void LlmClient::setPreset(const QString& preset)
{
    LENS_TRACE("LlmClient::setPreset: '{}' -> '{}'", preset_.toStdString(), preset.toStdString());
    preset_ = preset;
}

void LlmClient::setEtymology(bool etymology)
{
    LENS_TRACE("LlmClient::setEtymology: '{}'", etymology ? "on" : "off");
    etymology_ = etymology;
}

void LlmClient::fetchModels(QString provider)
{
    const auto entry     = serviceProvider(provider);
    const auto modelList = entry.value("modelList").toObject();
    if (entry.isEmpty() or modelList.isEmpty() or provider != provider_) return;
    if (config_.apiKey.isEmpty()) {
        LENS_INFO("no API key yet; the model list stays the one already known");
        return;
    }
    auto const method = modelList.value("method").toString();
    auto const path   = modelList.value("path").toString();
    if (method != QLatin1String("GET") or path.isEmpty()) return;
    QUrl url{path};
    if (not url.isValid() or url.scheme().isEmpty()) {
        url           = config_.baseUrl;
        auto basePath = url.path();
        while (basePath.endsWith(QLatin1Char('/')))
            basePath.chop(1);
        url.setPath(basePath + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
    }
    if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty()) return;
    QUrlQuery query;
    const auto queryValues = modelList.value("query").toObject();
    for (auto it = queryValues.begin(); it != queryValues.end(); ++it)
        query.addQueryItem(it.key(), it.value().toString());
    if (not query.isEmpty()) url.setQuery(query);

    QNetworkRequest request(url);
    const auto auth     = modelList.value("auth").toObject();
    const auto authType = auth.value("type").toString();
    const auto header   = auth.value("header").toString();
    if (authType == QLatin1String("bearer") and header == QLatin1String("Authorization"))
        request.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    else if (authType == QLatin1String("header") and not header.isEmpty())
        request.setRawHeader(header.toUtf8(), config_.apiKey.toUtf8());
    else
        return;
    request.setTransferTimeout(kModelListTimeoutMs);
    LENS_TRACE("GET {} (key hidden)", url.adjusted(QUrl::RemoveQuery).toString().toStdString());

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxModelListBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider, responseRules = modelList.value("response").toObject()] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError or (status != 0 and status != 200)) {
            // Not a failure the reader has to act on: the list is a convenience, and the one
            // already known still works.
            LENS_WARN("the model list was not fetched: HTTP {} ({})", status, reply->errorString().toStdString());
            return;
        }
        const QStringList ids = parseModelIds(reply->readAll(), responseRules);
        if (ids.isEmpty()) return;
        emit modelsFetched(provider, ids);
    });
}

void LlmClient::explainWords(QStringList words)
{
    if (words.isEmpty()) {
        LENS_ERROR("LlmClient::explainWords called with nothing to look up");
        emit failed(tr("There is nothing to look up."));
        return;
    }
    if (words.size() > kMaxWords) {
        LENS_WARN("LlmClient::explainWords: {} entries requested, keeping the first {}", words.size(), kMaxWords);
        words = words.mid(0, kMaxWords);
    }
    if (config_.apiKey.isEmpty()) {
        emit failed(tr("The API key is missing."));
        return;
    }

    QUrl url     = config_.baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    const Channel channel = channel_;
    const QString preset  = preset_;
    auto const language   = lang_;
    auto const model      = config_.model;
    LENS_INFO("explaining {} item(s) on channel '{}' preset '{}' with '{}'", words.size(), channelKey(channel), preset.toStdString(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply =
        manager_->post(request, buildRequestBody(config_, channel, words, lang_, preset, etymology_));
    connect(reply, &QNetworkReply::finished, this, [this, reply, words, channel, language, preset, model] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && (status == 0 || status == 200)) {
            // Qt can retain a successful status after the stream fails. In that case the body is
            // incomplete and must not be passed to the JSON/schema validator.
            LENS_ERROR("request failed before a response: {}", reply->errorString().toStdString());
            emit failed(tr("The request could not reach the service: %1").arg(reply->errorString()));
            return;
        }
        if (status == 0) { // no status code at all: the transfer itself never completed
            LENS_ERROR("request failed before a response: {}", reply->errorString().toStdString());
            emit failed(tr("The request could not reach the service: %1").arg(reply->errorString()));
            return;
        }
        if (status != 200) {
            LENS_ERROR("request failed with HTTP {}", status);
            emit failed(httpErrorFor(status));
            return;
        }

        const QByteArray body = reply->readAll();
        const auto parsed     = parseExplanations(channel, body, words, language, preset == QLatin1String("multiple"));
        if (const auto* message = std::get_if<QString>(&parsed)) {
            emit failed(*message);
            return;
        }
        LENS_INFO("received {} explanation(s)", words.size());
        auto usage  = parseUsage(body);
        usage.model = model;
        emit batchFinished(std::get<QVector<Explanation>>(parsed), usage);
    });
}

}
