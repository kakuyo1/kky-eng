#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include "llm_protocol.h" // Channel

class QNetworkAccessManager;

/**
 * @file llm_client.h
 * @brief Asynchronous client for the explanation model (DeepSeek, OpenAI-compatible, BYOK).
 *
 * The request body and the response check live in llm_pure.h so they can be tested without
 * a network. This header holds only the transport: a QObject around one HTTP round trip.
 */

namespace lens::llm {

/// @brief Connection settings, loaded from settings.local.json.
struct Config {
    QUrl baseUrl;   ///< OpenAI-format base, e.g. https://api.deepseek.com (no /anthropic).
    QString apiKey; ///< Bearer token. Never logged, never echoed in errors.
    QString model;  ///< Model name, e.g. "deepseek-flash".
};

/// @brief One explained word as returned by the model.
struct WordExplanation {
    QString word, en, zh;
};

/**
 * @brief Sends a batch of words and reports the validated explanations back.
 *
 * One HTTP request per call. The response is treated as untrusted data and goes through
 * the checks in parseExplanations() before anything is emitted.
 */
class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(Config config, QObject* parent = nullptr);

    /// @brief Set the explanation language, "en" or "zh".
    ///
    /// The language only switches the closing line of the system prompt (PHASE1.md
    /// section 5). It is a setter rather than a Config field because the reader can change
    /// it at runtime from the settings popup.
    void setExplanationLang(const QString& lang);

    /// @brief Set which protocol the next request speaks.
    ///
    /// The channel is decided locally, before anything is sent, and selects the request
    /// template and the response schema. Defaults to Channel::Word, the only channel
    /// phase 1 produces; the entity and sentence channels arrive with phase 2.
    void setChannel(Channel channel);

signals:
    /// @brief Emitted with the validated explanations, in the order the words were asked for.
    void batchFinished(QVector<WordExplanation> results);

    /// @brief Emitted on transport, HTTP status, or response-validation failure.
    /// @param message Reader-facing reason, already routed through translation.
    ///                Never contains the API key.
    void failed(QString message);

public slots:
    /// @brief Look up the whole payload in one HTTP request.
    /// @param words Payload to explain. More than 20 entries are silently truncated to the
    ///              first 20; the current caller sends one, so the cap is not reached.
    void explainWords(QStringList words);

private:
    Config config_;
    Channel channel_ = Channel::Word;
    QString lang_ = QStringLiteral("en");
    QNetworkAccessManager* manager_;
};

}
