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

/// @brief Connection settings, loaded from the reader's settings document.
struct Config {
    QUrl baseUrl;                 ///< OpenAI-format base, e.g. https://api.deepseek.com (no /anthropic).
    QString apiKey;               ///< Bearer token. Never logged, never echoed in errors.
    QString model;                ///< Model name, e.g. "deepseek-flash".
    QJsonObject requestOverrides; ///< Provider-specific wire options; null removes a default.
};

/// @brief One frequency-ordered word sense; translation is in the requested output language.
struct Sense {
    QString en, zh, translation;
};

inline constexpr int kMaxSenses = 3;

/// @brief One explanation as returned by the model.
/// @note `title` is `word` on the word channel and the echoed sentence or entity otherwise.
///       `ipa` is required only by the word schema; sentence and entity responses have three
///       fields (`title`, `en`, `zh`).
struct Explanation {
    QString title, ipa, en, zh;
    QString translation;
    QVector<Sense> senses;
};

/// @brief Token counts the service reports for one call, for the cost surfaces.
///
/// Missing from the response is recorded as zero rather than failing the batch: losing one
/// line of a tally is survivable, throwing away an explanation that succeeded is not.
struct Usage {
    int promptTokens     = 0;
    int completionTokens = 0;
    QString model; ///< Model captured for this request, even if settings changed in flight.
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
    /// @brief Build a client, optionally over a caller-supplied network manager.
    ///
    /// The manager is the seam that lets a test answer without a socket: pass a subclass whose
    /// createRequest() returns a stub reply, and the status-code, empty-body and transport-failure
    /// branches become reachable offline. It must outlive this object. Null (the default) makes
    /// one owned by this object, which is what every caller in the application passes.
    ///
    /// @param config  Connection settings.
    /// @param manager Network manager to post through, or null to make one.
    /// @param parent  QObject parent.
    explicit LlmClient(Config config, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief Set an explanation language from the loaded catalog.
    ///
    /// The language only switches the closing line of the system prompt. It is
    /// a setter rather than a Config field because the reader can change it at
    /// runtime from the settings popup.
    void setExplanationLang(const QString& lang);

    /// @brief Replace the model used by subsequent requests.
    void setModel(const QString& model);

    /// @brief Replace the OpenAI-compatible endpoint used by subsequent requests.
    void setBaseUrl(const QUrl& baseUrl);

    /// @brief Replace the bearer token used by subsequent requests without logging it.
    void setApiKey(const QString& apiKey);

    /// @brief Apply a catalog provider's endpoint, default model and wire options.
    /// @return False for an unknown provider; custom retains the current endpoint and model.
    bool setProvider(QString const& provider);

    /// @brief Ask the service which models this account can use.
    ///
    /// A `GET /models`, which costs nothing and is the only current answer there is: the ids a
    /// provider supports change under the app, and a name that is no longer one of them is the 400
    /// nobody can read (docs/adr/0017). Failure is not worth a card -- the list this already has
    /// stays, and explaining still works.
    void fetchModels();

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

    /// @return The endpoint used by subsequent requests, without exposing the bearer token.
    QUrl baseUrl() const
    {
        return config_.baseUrl;
    }

signals:
    /// @brief Emitted with the validated explanations, in the order the words were asked for.
    /// @param usage Token counts from the same response, all zero when it carried none.
    void batchFinished(QVector<Explanation> results, Usage usage);

    /// @brief Emitted on transport, HTTP status, or response-validation failure.
    /// @param message Reader-facing reason, already routed through translation.
    ///                Never contains the API key.
    void failed(QString message);

    /// @brief The service's own list of model ids, in the order it gave them.
    void modelsFetched(QStringList models);

public slots:
    /// @brief Look up the whole payload in one HTTP request.
    /// @param words Payload to explain. More than 20 entries are silently truncated to the
    ///              first 20; the current caller sends one, so the cap is not reached.
    void explainWords(QStringList words);

private:
    Config config_;
    Channel channel_ = Channel::Word;
    QString preset_  = QStringLiteral("default");
    QString lang_    = QStringLiteral("en");
    QNetworkAccessManager* manager_;
};

}
