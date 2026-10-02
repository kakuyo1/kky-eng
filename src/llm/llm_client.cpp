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

constexpr int kMaxWords = 20;   ///< Contract limit; see PHASE1.md section 4.3.
constexpr int kTimeoutMs = 30000;

/// Map an HTTP status onto a message for the user. The API key is never part of it, and
/// neither is the response body, which may echo the request.
QString httpErrorFor(int status) {
    switch (status) {
        case 400: return QStringLiteral("请求格式错误（400）");
        case 401: return QStringLiteral("API key 无效或缺失（401）");
        case 402: return QStringLiteral("账户余额不足（402）");
        case 422: return QStringLiteral("请求参数无效（422）");
        case 429: return QStringLiteral("请求过于频繁，已被限流（429）");
        case 500: return QStringLiteral("DeepSeek 服务端错误（500）");
        case 503: return QStringLiteral("DeepSeek 服务过载（503）");
        default: return QStringLiteral("HTTP %1").arg(status);
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

void LlmClient::explainWords(QStringList words) {
    if (words.isEmpty()) {
        LENS_ERROR("LlmClient::explainWords called with no words");
        emit failed(QStringLiteral("没有待查单词"));
        return;
    }
    if (words.size() > kMaxWords) {
        LENS_WARN("LlmClient::explainWords: {} words requested, keeping the first {}",
                     words.size(), kMaxWords);
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

    LENS_INFO("explaining {} word(s) with '{}'", words.size(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply = manager_->post(request, buildRequestBody(config_, words, lang_));
    connect(reply, &QNetworkReply::finished, this, [this, reply, words] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) {   // no status code at all: the transfer itself never completed
            LENS_ERROR("request failed before a response: {}", reply->errorString().toStdString());
            emit failed(QStringLiteral("网络请求失败：%1").arg(reply->errorString()));
            return;
        }
        if (status != 200) {
            LENS_ERROR("request failed with HTTP {}", status);
            emit failed(httpErrorFor(status));
            return;
        }

        const auto parsed = parseExplanations(reply->readAll(), words);
        if (const auto* message = std::get_if<QString>(&parsed)) {
            emit failed(*message);
            return;
        }
        LENS_INFO("received {} explanation(s)", words.size());
        emit batchFinished(std::get<QVector<WordExplanation>>(parsed));
    });
}

}
