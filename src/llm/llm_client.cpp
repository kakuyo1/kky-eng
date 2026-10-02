#include "llm_client.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>
#include <variant>

#include "core/log.h"
#include "llm_pure.h"

namespace lens::llm {
namespace {

constexpr int kMaxWords = 20;   ///< Contract limit; see PHASE1.md section 4.3.
constexpr int kTimeoutMs = 30000;

/// Map an HTTP status onto something the reader can act on. The API key is never part of
/// it, and neither is the response body, which may echo the request.
QString httpErrorFor(int status) {
    switch (status) {
        case 400:
            return QCoreApplication::translate("lens::llm",
                                               "The request was rejected as malformed (400).");
        case 401:
            return QCoreApplication::translate("lens::llm",
                                               "The API key is missing or not accepted (401).");
        case 402:
            return QCoreApplication::translate("lens::llm",
                                               "The account is out of credit (402).");
        case 422:
            return QCoreApplication::translate("lens::llm",
                                               "The request parameters were rejected (422).");
        case 429:
            return QCoreApplication::translate(
                "lens::llm", "Too many requests; the service is rate-limiting (429).");
        case 500:
            return QCoreApplication::translate("lens::llm",
                                               "The explanation service failed (500).");
        case 503:
            return QCoreApplication::translate("lens::llm",
                                               "The explanation service is overloaded (503).");
        default:
            return QCoreApplication::translate("lens::llm", "Unexpected HTTP status %1.")
                .arg(status);
    }
}

}   // namespace

LlmClient::LlmClient(Config config, QObject* parent)
    : QObject(parent), config_(std::move(config)), manager_(new QNetworkAccessManager(this)) {
    LENS_TRACE("LlmClient created: model='{}' base='{}'", config_.model.toStdString(),
               config_.baseUrl.toString().toStdString());
}

void LlmClient::setExplanationLang(const QString& lang) {
    LENS_TRACE("LlmClient::setExplanationLang: '{}' -> '{}'", lang_.toStdString(),
               lang.toStdString());
    lang_ = lang;
}

void LlmClient::setChannel(Channel channel) {
    LENS_TRACE("LlmClient::setChannel: '{}' -> '{}'", channelKey(channel_), channelKey(channel));
    channel_ = channel;
}

void LlmClient::explainWords(QStringList words) {
    if (words.isEmpty()) {
        LENS_ERROR("LlmClient::explainWords called with nothing to look up");
        emit failed(tr("There is nothing to look up."));
        return;
    }
    if (words.size() > kMaxWords) {
        LENS_WARN("LlmClient::explainWords: {} entries requested, keeping the first {}", words.size(),
                  kMaxWords);
        words = words.mid(0, kMaxWords);
    }

    QUrl url = config_.baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    LENS_INFO("explaining {} item(s) on channel '{}' with '{}'", words.size(), channelKey(channel_),
              config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply =
        manager_->post(request, buildRequestBody(config_, channel_, words, lang_));
    connect(reply, &QNetworkReply::finished, this, [this, reply, words] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) {   // no status code at all: the transfer itself never completed
            LENS_ERROR("request failed before a response: {}", reply->errorString().toStdString());
            emit failed(tr("The request could not reach the service: %1").arg(reply->errorString()));
            return;
        }
        if (status != 200) {
            LENS_ERROR("request failed with HTTP {}", status);
            emit failed(httpErrorFor(status));
            return;
        }

        const auto parsed = parseExplanations(channel_, reply->readAll(), words);
        if (const auto* message = std::get_if<QString>(&parsed)) {
            emit failed(*message);
            return;
        }
        LENS_INFO("received {} explanation(s)", words.size());
        emit batchFinished(std::get<QVector<WordExplanation>>(parsed));
    });
}

}
