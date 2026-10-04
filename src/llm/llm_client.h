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

/// @brief One explanation as returned by the model.
/// @note `title` is `word` on the word channel and the echoed sentence or entity otherwise.
///       `ipa` is required only by the word schema; sentence and entity responses have three
///       fields (`title`, `en`, `zh`).
struct Explanation {
    QString title, ipa, en, zh;
};

/// @brief Token counts the service reports for one call, for the cost surfaces.
///
/// Missing from the response is recorded as zero rather than failing the batch: losing one
/// line of a tally is survivable, throwing away an explanation that succeeded is not.
struct Usage {
    int promptTokens = 0;
    int completionTokens = 0;
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
    /// phase 1 produces.
    void setChannel(Channel channel);

    /// @brief Select the loaded prompt preset for the next request.
    void setPreset(const QString& preset);

    /// @return The model name as sent on the wire, which is the key the price list uses.
    QString model() const
    {
        return config_.model;
    }

signals:
    /// @brief Emitted with the validated explanations, in the order the words were asked for.
    /// @param usage Token counts from the same response, all zero when it carried none.
    void batchFinished(QVector<Explanation> results, Usage usage);

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
    QString preset_ = QStringLiteral("default");
    QString lang_ = QStringLiteral("en");
    QNetworkAccessManager* manager_;
};

}
