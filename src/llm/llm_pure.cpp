#include "llm_pure.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "util/log.h"

namespace lens::llm {
namespace {

// Reader-facing messages go through QCoreApplication::translate with both the context and
// the text written out as literals at every site. A helper taking the context as an
// argument would read better and extract worse: lupdate only sees literal arguments, so
// the strings would silently never reach the .ts files.

/// @brief Render a channel's system prompt with the output-language line filled in.
QString renderSystemPrompt(Channel channel, const QString& explanationLang, const QString& preset, bool const etymology)
{
    const PromptTemplate& tmpl = promptTemplate(channel, preset);

    QString line = tmpl.outputLanguageLine.value(explanationLang);
    if (line.isEmpty()) {
        LENS_WARN("unknown explanation language '{}'; using 'en'", explanationLang.toStdString());
        line = tmpl.outputLanguageLine.value(QStringLiteral("en"));
    }

    QString prompt = tmpl.systemPromptTemplate;
    prompt.replace(QLatin1String(kOutputLanguagePlaceholder), line);
    // Clearing both placeholders is the whole of "this word is being asked for without its
    // origin": the sentence and the field it names travel together, and what is left is the prompt
    // exactly as it read before the setting existed. A channel whose template never carried them
    // is untouched, which is how entity and sentence keep out of this.
    prompt.replace(QLatin1String(kEtymologyNotePlaceholder), etymology ? tmpl.etymologyNote : QString{});
    prompt.replace(QLatin1String(kEtymologyFieldPlaceholder), etymology ? tmpl.etymologyField : QString{});
    return prompt;
}

/// @return The JSON object bytes inside a model's content, tolerating a surrounding markdown
/// fence or a line of prose. The model is asked for bare JSON, but a wrapped answer is common
/// enough that failing the whole batch over it would be brittle.
QByteArray extractJsonObject(const QString& content)
{
    QString text = content.trimmed();
    if (text.startsWith(QLatin1String("```"))) {
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0)
            text = text.mid(newline + 1);
        const int fence = text.lastIndexOf(QLatin1String("```"));
        if (fence >= 0)
            text = text.left(fence);
        text = text.trimmed();
    }
    if (!text.startsWith(QLatin1Char('{'))) {
        const int open  = text.indexOf(QLatin1Char('{'));
        const int close = text.lastIndexOf(QLatin1Char('}'));
        if (open >= 0 && close > open)
            text = text.mid(open, close - open + 1);
    }
    return text.toUtf8();
}

/// @brief Validate the schema vocabulary used by the shipped response contracts.
bool matchesSchema(QJsonValue const& value, QJsonObject const& schema)
{
    auto const alternatives = schema.value("anyOf").toArray();
    if (not alternatives.isEmpty()) {
        bool matched = false;
        for (auto const& branch : alternatives)
            matched = matched or matchesSchema(value, branch.toObject());
        if (not matched) return false;
    }
    auto const type = schema.value("type").toString();
    if (type == QLatin1String("string"))
        return value.isString() and value.toString().trimmed().size() >= schema.value("minLength").toInt();
    if (type == QLatin1String("array")) {
        if (not value.isArray() or value.toArray().size() < schema.value("minItems").toInt()) return false;
        for (auto const& item : value.toArray())
            if (not matchesSchema(item, schema.value("items").toObject())) return false;
    }
    if (type == QLatin1String("object") or schema.contains("required")) {
        if (not value.isObject()) return false;
        auto const object = value.toObject();
        for (auto const& field : schema.value("required").toArray())
            if (not object.contains(field.toString())) return false;
        auto const properties = schema.value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it)
            if (object.contains(it.key()) and not matchesSchema(object.value(it.key()), it.value().toObject()))
                return false;
    }
    return true;
}

} // namespace

QString httpErrorFor(int status)
{
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

QString maskSensitive(const QString& text)
{
    static const QRegularExpression url(R"(\bhttps?://\S+)");
    static const QRegularExpression email(R"([\w.%+-]+@[\w-]+(?:\.[\w-]+)+)");
    static const QRegularExpression digits(R"(\d{6,})");

    QString masked = text;
    masked.replace(url, QStringLiteral("<url>"));
    masked.replace(email, QStringLiteral("<email>"));
    masked.replace(digits, QStringLiteral("<num>"));

    // Only that something was masked is recorded. The masked text itself never reaches a log.
    if (masked != text)
        LENS_TRACE("maskSensitive: replaced sensitive content in a {}-char input", text.size());
    return masked;
}

QByteArray buildRequestBody(const Config& config,
                            Channel channel,
                            const QStringList& inputs,
                            const QString& explanationLang,
                            const QString& preset,
                            bool const etymology)
{
    const RequestTemplate& tmpl = requestTemplate(channel);

    QStringList masked;
    masked.reserve(inputs.size());
    for (const auto& input : inputs)
        masked << maskSensitive(input);

    QJsonObject body{
        {"model", config.model},
        {"messages",
         QJsonArray{
             QJsonObject{{"role", "system"},
                         {"content", renderSystemPrompt(channel, explanationLang, preset, etymology)}},
             QJsonObject{{"role", "user"}, {"content", masked.join(QLatin1Char('\n'))}},
         }},
        {"response_format", tmpl.responseFormat},
        {"thinking", tmpl.thinking},
        {"max_tokens", tmpl.maxTokens},
        {"stream", tmpl.stream},
    };
    auto overrides = config.requestOverrides;
    if (overrides.isEmpty()) {
        for (auto const& value : serviceCatalog().value("providers").toArray()) {
            auto const provider = value.toObject();
            if (provider.value("baseUrl").toString() == config.baseUrl.toString())
                overrides = provider.value("requestOverrides").toObject();
        }
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it.value().isNull())
            body.remove(it.key());
        else
            body.insert(it.key(), it.value());
    }

    LENS_TRACE("buildRequestBody: {} item(s) for channel '{}', preset '{}', model='{}', lang='{}'", inputs.size(), channelKey(channel), preset.toStdString(), config.model.toStdString(), explanationLang.toStdString());
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

std::variant<QVector<Explanation>, QString> parseExplanations(Channel channel,
                                                              const QByteArray& responseBody,
                                                              const QStringList& expectedInputs,
                                                              QString const& explanationLang,
                                                              bool const multipleSenses)
{
    using Result = std::variant<QVector<Explanation>, QString>;

    /// Reject the whole batch, naming the reason once for both the caller and the log.
    const auto reject = [](const QString& reason) -> Result {
        LENS_ERROR("parseExplanations rejected the response: {}", reason.toStdString());
        return reason;
    };

    const auto envelope = QJsonDocument::fromJson(responseBody);
    if (!envelope.isObject())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model answered with something that is not "
                                                  "a JSON object."));

    const auto choices = envelope.object().value("choices").toArray();
    if (choices.isEmpty())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model's answer carries no choices."));

    const auto choice    = choices.at(0).toObject();
    const QString finish = choice.value("finish_reason").toString();
    // Only "stop" means the model finished on its own. "length" means the JSON was cut off
    // mid-way, which no amount of parsing can repair.
    if (finish != QLatin1String("stop"))
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model stopped before finishing "
                                                  "(reason: %1).")
                          .arg(finish.isEmpty()
                                   ? QCoreApplication::translate("lens::llm", "absent")
                                   : finish));

    const QString content = choice.value("message").toObject().value("content").toString();
    const auto payload    = QJsonDocument::fromJson(extractJsonObject(content));
    if (!payload.isObject())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model's answer is not valid JSON."));

    const auto resultsValue = payload.object().value("results");
    if (!resultsValue.isArray())
        return reject(QCoreApplication::translate(
            "lens::llm", "The model's answer has no results array."));

    // Presence is gated on the response schema, so adding a required field is a data edit.
    const bool isWord        = channel == Channel::Word;
    const QString titleField = isWord ? QStringLiteral("word") : QStringLiteral("title");

    QVector<Explanation> parsed;
    for (const auto& item : resultsValue.toArray()) {
        if (!item.isObject())
            return reject(QCoreApplication::translate(
                "lens::llm", "One of the results entries is not an object."));
        const auto obj = item.toObject();

        if (not matchesSchema(item, resultSchema(channel)))
            return reject(QCoreApplication::translate("lens::llm", "A results entry does not match the response schema."));

        Explanation e{obj.value(titleField).toString(),
                      obj.value(QStringLiteral("ipa")).toString(),
                      obj.value(QStringLiteral("en")).toString(),
                      obj.value(QStringLiteral("zh")).toString()};
        e.translation = obj.value("translation").toString();
        // Read and never demanded: the origin is asked for only when the setting is on, an acronym
        // has none, and the model is free to answer "unknown". Failing a whole batch over it would
        // throw away the definitions that did arrive, the same trade `ipa` already makes.
        e.etymology = obj.value(QStringLiteral("etymology")).toString();
        if (isWord and obj.contains("senses")) {
            auto const senses = obj.value("senses").toArray();
            for (auto const& value : senses) {
                auto const sense = value.toObject();
                if (explanationLang != QLatin1String("en") and explanationLang != QLatin1String("zh") and sense.value("translation").toString().trimmed().isEmpty())
                    return reject(QCoreApplication::translate("lens::llm", "The requested explanation language is missing."));
                if (e.senses.size() < (multipleSenses ? kMaxSenses : 1))
                    e.senses.push_back({sense.value("en").toString(), sense.value("zh").toString(), sense.value("translation").toString()});
            }
            e.en          = e.senses.front().en;
            e.zh          = e.senses.front().zh;
            e.translation = e.senses.front().translation;
        } else if (isWord) {
            e.senses.push_back({e.en, e.zh, e.translation});
        }
        if (explanationLang != QLatin1String("en") and explanationLang != QLatin1String("zh") and e.translation.trimmed().isEmpty())
            return reject(QCoreApplication::translate("lens::llm", "The requested explanation language is missing."));
        // Presence is the schema's business; emptiness is not. Both language definitions are
        // required by every channel. A word must also name itself (its IPA stays optional: an
        // acronym has none); the entity and sentence channels carry no echoed title.
        if (e.en.trimmed().isEmpty() || e.zh.trimmed().isEmpty() || (isWord && e.title.trimmed().isEmpty()))
            return reject(QCoreApplication::translate("lens::llm",
                                                      "A results entry has an empty field "
                                                      "(title=%1).")
                              .arg(e.title));
        parsed.push_back(e);
    }

    if (!isWord) {
        // Entity and sentence carry no echoed title on the wire: the app already knows the
        // selected text, and making a model reproduce a long, punctuation-heavy string exactly
        // only invites drift. A model may also split a selected block (a numbered list, several
        // sentences) into more entries than were asked for, so fold those into the one result
        // rather than losing the whole batch.
        QVector<Explanation> ordered;
        if (parsed.size() == expectedInputs.size()) {
            for (int i = 0; i < expectedInputs.size(); ++i) {
                Explanation e = parsed.at(i);
                e.title       = expectedInputs.at(i);
                ordered.push_back(e);
            }
        } else if (expectedInputs.size() == 1 && !parsed.isEmpty()) {
            Explanation e;
            for (const auto& part : parsed) {
                if (!e.en.isEmpty()) e.en += QLatin1Char('\n');
                e.en += part.en;
                if (!e.zh.isEmpty()) e.zh += QLatin1Char('\n');
                e.zh += part.zh;
                if (not e.translation.isEmpty()) e.translation += QLatin1Char('\n');
                e.translation += part.translation;
            }
            e.title = expectedInputs.front();
            ordered.push_back(e);
        } else {
            return reject(QCoreApplication::translate("lens::llm",
                                                      "The model echoed %1 result(s) for the %2 that "
                                                      "were asked for.")
                              .arg(parsed.size())
                              .arg(expectedInputs.size()));
        }
        LENS_TRACE("parseExplanations: accepted {} explanation(s) for channel '{}'", ordered.size(), channelKey(channel));
        return ordered;
    }

    // A word keeps the echo contract: the model must name the word requested, which catches an
    // explanation attached to the wrong entry before it reaches the bubble. Matching is
    // case-insensitive (a model may capitalize an acronym) and the requested spelling wins, so
    // the cache key and the bubble title stay canonical.
    QHash<QString, Explanation> byTitle;
    for (const auto& e : parsed) {
        const QString key = e.title.toLower();
        if (byTitle.contains(key))
            return reject(QCoreApplication::translate("lens::llm",
                                                      "The model echoed the same title twice: %1.")
                              .arg(e.title));
        byTitle.insert(key, e);
    }

    if (byTitle.size() != expectedInputs.size())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model echoed %1 result(s) for the %2 that "
                                                  "were asked for.")
                          .arg(byTitle.size())
                          .arg(expectedInputs.size()));

    QVector<Explanation> ordered;
    ordered.reserve(expectedInputs.size());
    for (const auto& input : expectedInputs) {
        const auto it = byTitle.constFind(input.toLower());
        if (it == byTitle.cend())
            return reject(QCoreApplication::translate(
                              "lens::llm", "The model never echoed \"%1\".")
                              .arg(input));
        Explanation e = *it;
        e.title       = input;
        ordered.push_back(e);
    }

    LENS_TRACE("parseExplanations: accepted {} explanation(s) for channel '{}'", ordered.size(), channelKey(channel));
    return ordered;
}

namespace {

/// @brief Whether an id names something a chat completion can be sent to.
///
/// `/models` answers with the whole catalogue, not with the chat models in it: embeddings, speech,
/// image and video models sit in the same array, and a service that resells other people's models
/// (OpenRouter) answers with several hundred of them. The app can use exactly one kind, so the
/// rest are dropped before the list ever reaches the reader.
///
/// Where the service says so itself -- `architecture.output_modalities`, which OpenRouter fills in
/// -- that answer is used. Everywhere else the id is the only thing there is, which is why the
/// markers below are read as substrings: `tts-1` and `gpt-4o-mini-tts` are the same category and
/// share no prefix. They are all words that name a modality no chat request reaches, so a chat
/// model carrying one would be a surprise, not a rule being bent.
bool speaksChat(const QJsonObject& entry)
{
    const auto modalities = entry.value("architecture").toObject().value("output_modalities");
    if (modalities.isArray() and not modalities.toArray().contains(QStringLiteral("text")))
        return false;

    static const char* const kNotChat[]{"embed", "rerank", "moderation", "whisper", "tts", "audio", "speech", "transcri", "dall-e", "dalle", "image", "video", "sora"};
    const QString id = entry.value("id").toString().toLower();
    for (const char* marker : kNotChat)
        if (id.contains(QLatin1String(marker))) return false;
    return true;
}

} // namespace

QStringList parseModelIds(QByteArray const& body, QJsonObject const& responseRules)
{
    const auto document = QJsonDocument::fromJson(body);
    if (not document.isObject()) {
        LENS_WARN("the model list was not valid JSON; keeping the one we have");
        return {};
    }
    const auto arrayField = responseRules.value("arrayField").toString();
    const auto idField    = responseRules.value("idField").toString();
    const auto data       = document.object().value(arrayField);
    if (arrayField.isEmpty() or idField.isEmpty() or not data.isArray() or data.toArray().size() > 2000) {
        LENS_WARN("the model list did not arrive in the shape this reads; keeping the one we have");
        return {};
    }
    const auto capabilityField = responseRules.value("capabilityField").toString();
    const auto capabilityValue = responseRules.value("capabilityValue").toString();
    const auto stripPrefix     = responseRules.value("stripPrefix").toString();
    QStringList ids;
    for (const auto& entry : data.toArray()) {
        if (not entry.isObject()) return {};
        const auto object = entry.toObject();
        const auto rawId  = object.value(idField);
        if (not rawId.isString()) return {};
        auto id = rawId.toString().trimmed();
        if (not stripPrefix.isEmpty() and id.startsWith(stripPrefix))
            id.remove(0, stripPrefix.size());
        if (id.isEmpty() or id.size() > 256) return {};
        if (ids.contains(id)) continue;
        if (not capabilityField.isEmpty()) {
            const auto capabilities = object.value(capabilityField);
            if (not capabilities.isArray()) return {};
            if (not capabilities.toArray().contains(capabilityValue)) continue;
        }
        auto chatEntry = object;
        chatEntry.insert("id", id);
        if (not speaksChat(chatEntry)) {
            continue;
        }
        ids.append(id);
    }
    LENS_INFO("the service lists {} model(s) this app can use", ids.size());
    return ids;
}

Usage parseUsage(const QByteArray& responseBody)
{
    const auto envelope = QJsonDocument::fromJson(responseBody).object();
    const auto usage    = envelope.value("usage").toObject();
    if (usage.isEmpty()) {
        LENS_WARN("the response carries no usage object; the cost tally loses this call");
        return {};
    }

    Usage counts;
    counts.promptTokens     = usage.value("prompt_tokens").toInt();
    counts.completionTokens = usage.value("completion_tokens").toInt();
    LENS_TRACE("usage: {} prompt / {} completion token(s)", counts.promptTokens, counts.completionTokens);
    return counts;
}

}
