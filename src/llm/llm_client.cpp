#include "llm_client.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>
#include <variant>

#include "core/log.h"
#include "llm_pure.h"

namespace lens::llm {
namespace {

constexpr int kMaxWords  = 20; ///< Contract limit.
constexpr int kTimeoutMs = 30000;

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

void LlmClient::setPreset(const QString& preset)
{
    LENS_TRACE("LlmClient::setPreset: '{}' -> '{}'", preset_.toStdString(), preset.toStdString());
    preset_ = preset;
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
    LENS_INFO("explaining {} item(s) on channel '{}' preset '{}' with '{}'", words.size(), channelKey(channel), preset.toStdString(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply =
        manager_->post(request, buildRequestBody(config_, channel, words, lang_, preset));
    connect(reply, &QNetworkReply::finished, this, [this, reply, words, channel] {
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
        const auto parsed     = parseExplanations(channel, body, words);
        if (const auto* message = std::get_if<QString>(&parsed)) {
            emit failed(*message);
            return;
        }
        LENS_INFO("received {} explanation(s)", words.size());
        emit batchFinished(std::get<QVector<Explanation>>(parsed), parseUsage(body));
    });
}

}
