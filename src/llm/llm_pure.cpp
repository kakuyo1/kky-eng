#include "llm_pure.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <spdlog/spdlog.h>

namespace lens::llm {
namespace {

/// Non-thinking mode defaults to 8K output tokens. At most 20 words go out per request, so
/// 4K is ample; it is set explicitly only so the behaviour does not depend on a remote default.
constexpr int kMaxTokens = 4096;

/// The system prompt from PHASE1.md section 5. Only the closing sentence varies with the
/// explanation language. It contains the word "JSON" and a format example, both of which
/// DeepSeek requires for json_object output; without them the model streams whitespace
/// until max_tokens is exhausted.
QString systemPrompt(const QString& lang) {
    return QStringLiteral(
               "你是英语学习工具的释义助手，用户是 CET-4 以上水平的成人学习者。\n"
               "输入一个单词列表；对每个单词给出最常见的词义：一条英文定义、一条中文释义、一个简短例句。\n"
               "常见词若有多义，取最常见义项。\n"
               "输出必须是合法 JSON，符合此结构：\n"
               "{\"results\":[{\"word\":\"...\",\"en\":\"...\",\"zh\":\"...\",\"example\":\"...\"}]}\n"
               "必须逐一回显输入单词（原样拼写），不增不减。\n") +
           (lang == QLatin1String("zh") ? QStringLiteral("中文释义为主、例句用中文。")
                                        : QStringLiteral("英文定义为主、例句用英文。"));
}

}   // namespace

QString maskSensitive(const QString& text) {
    static const QRegularExpression url(R"(\bhttps?://\S+)");
    static const QRegularExpression email(R"([\w.%+-]+@[\w-]+(?:\.[\w-]+)+)");
    static const QRegularExpression digits(R"(\d{6,})");

    QString masked = text;
    masked.replace(url, QStringLiteral("<url>"));
    masked.replace(email, QStringLiteral("<email>"));
    masked.replace(digits, QStringLiteral("<num>"));

    // Log only that something was masked. The masked content itself never reaches the log.
    if (masked != text)
        spdlog::trace("maskSensitive: replaced sensitive content in a {}-char input",
                      text.size());
    return masked;
}

QByteArray buildRequestBody(const Config& config, const QStringList& words,
                            const QString& explanationLang) {
    QStringList masked;
    masked.reserve(words.size());
    for (const auto& word : words) masked << maskSensitive(word);

    const QJsonObject body{
        {"model", config.model},
        {"messages",
         QJsonArray{
             QJsonObject{{"role", "system"}, {"content", systemPrompt(explanationLang)}},
             QJsonObject{{"role", "user"}, {"content", masked.join(QLatin1Char('\n'))}},
         }},
        {"response_format", QJsonObject{{"type", "json_object"}}},
        {"thinking", QJsonObject{{"type", "disabled"}}},   // DeepSeek defaults to thinking on
        {"max_tokens", kMaxTokens},
        {"stream", false},
    };

    spdlog::trace("buildRequestBody: {} word(s), model='{}', lang='{}'", words.size(),
                  config.model.toStdString(), explanationLang.toStdString());
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

std::variant<QVector<WordExplanation>, QString> parseExplanations(const QByteArray& responseBody,
                                                                  const QStringList& expectedWords) {
    using Result = std::variant<QVector<WordExplanation>, QString>;

    /// Reject the whole batch, naming the reason once for both the caller and the log.
    const auto reject = [](const QString& reason) -> Result {
        spdlog::error("parseExplanations rejected the response: {}", reason.toStdString());
        return reason;
    };

    const auto envelope = QJsonDocument::fromJson(responseBody);
    if (!envelope.isObject()) return reject(QStringLiteral("响应不是合法 JSON 对象"));

    const auto choices = envelope.object().value("choices").toArray();
    if (choices.isEmpty()) return reject(QStringLiteral("响应缺少 choices"));

    const auto choice = choices.at(0).toObject();
    const QString finish = choice.value("finish_reason").toString();
    // Only "stop" means the model finished on its own. "length" means the JSON was cut off
    // mid-way, which no amount of parsing can repair.
    if (finish != QLatin1String("stop"))
        return reject(QStringLiteral("生成未正常结束（finish_reason=%1）")
                          .arg(finish.isEmpty() ? QStringLiteral("缺失") : finish));

    const QString content = choice.value("message").toObject().value("content").toString();
    const auto payload = QJsonDocument::fromJson(content.toUtf8());
    if (!payload.isObject()) return reject(QStringLiteral("模型输出不是合法 JSON"));

    const auto resultsValue = payload.object().value("results");
    if (!resultsValue.isArray()) return reject(QStringLiteral("模型输出缺少 results 数组"));

    QHash<QString, WordExplanation> byWord;
    for (const auto& item : resultsValue.toArray()) {
        if (!item.isObject()) return reject(QStringLiteral("results 元素不是对象"));
        const auto obj = item.toObject();
        for (const auto* field : {"word", "en", "zh", "example"})
            if (!obj.value(QLatin1String(field)).isString())
                return reject(QStringLiteral("results 元素缺字段：%1").arg(QLatin1String(field)));

        WordExplanation e{obj.value("word").toString(), obj.value("en").toString(),
                          obj.value("zh").toString(), obj.value("example").toString()};
        // The example is not shown in phase 1, so an empty one must not sink the batch.
        // The three fields the overlay does show must be present and non-empty.
        if (e.word.isEmpty() || e.en.isEmpty() || e.zh.isEmpty())
            return reject(QStringLiteral("results 元素字段为空（word=%1）").arg(e.word));
        if (byWord.contains(e.word))
            return reject(QStringLiteral("回显单词重复：%1").arg(e.word));
        byWord.insert(e.word, e);
    }

    if (byWord.size() != expectedWords.size())
        return reject(QStringLiteral("回显词数不符：请求 %1 个，返回 %2 个")
                          .arg(expectedWords.size())
                          .arg(byWord.size()));

    // Restore request order. The order the model chose carries no meaning.
    QVector<WordExplanation> ordered;
    ordered.reserve(expectedWords.size());
    for (const auto& word : expectedWords) {
        const auto it = byWord.constFind(word);
        if (it == byWord.cend()) return reject(QStringLiteral("回显缺少单词：%1").arg(word));
        ordered.push_back(*it);
    }

    spdlog::trace("parseExplanations: accepted {} explanation(s)", ordered.size());
    return ordered;
}

}
