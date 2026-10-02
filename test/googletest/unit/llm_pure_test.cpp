/**
 * @file llm_pure_test.cpp
 * @brief The network-free LlmClient helpers: mask, build the request, check the response.
 *
 * The request cases pin what data/llm/request.word.json must keep saying, so a data edit
 * that breaks the DeepSeek contract fails here rather than in a paid round trip. The
 * response cases treat the payload as untrusted: anything missing, extra, misspelled, or
 * truncated has to fail the whole batch.
 */

#include <string>
#include <variant>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <gtest/gtest.h>

#include "llm/llm_pure.h"
#include "llm_support.h"

namespace {

using lens::llm::Channel;
using lens::llm::Config;
using lens::llm::WordExplanation;
using lens::llm::buildRequestBody;
using lens::llm::maskSensitive;
using lens::llm::parseExplanations;
using LlmResult = std::variant<QVector<WordExplanation>, QString>;

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::LlmTest;

/// @return The failure message when @p result is an error, or an empty string on success.
QString errorOf(const LlmResult& result) {
    const auto* message = std::get_if<QString>(&result);
    return message ? *message : QString();
}

bool accepted(const LlmResult& result) {
    return std::holds_alternative<QVector<WordExplanation>>(result);
}

/// @brief Wrap @p content the way the chat-completions envelope does.
QByteArray envelope(const QString& content, const QString& finishReason = QStringLiteral("stop")) {
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
                                                    {"finish_reason", finishReason},
                                                    {"message", QJsonObject{{"content", content}}}}}}})
        .toJson(QJsonDocument::Compact);
}

/// @return A word-channel content payload carrying @p items as its `results` array.
QString results(const QString& items) {
    return QStringLiteral("{\"results\":[%1]}").arg(items);
}

const QStringList kAskedFor{"ubiquitous"};
const QString kGoodResult =
    QStringLiteral(R"({"word":"ubiquitous","en":"existing everywhere","zh":"无处不在的"})");

}   // namespace

TEST(LlmPureMask, CollapsesEmailUrlAndLongDigits) {
    const struct {
        const char* input;
        const char* want;
    } cases[] = {
        {"mail a.b+tag@example.com now", "mail <email> now"},
        {"see https://example.com/p?q=1#f end", "see <url> end"},
        {"call 13800138000 now", "call <num> now"},
        {"ubiquitous", "ubiquitous"},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.input);
        EXPECT_EQ(maskSensitive(QString::fromUtf8(item.input)), QString::fromUtf8(item.want));
    }
}

TEST_F(LlmTest,RequestBodyKeepsTheDeepSeekContract) {
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const QStringList words{"ubiquitous", "resilience"};

    for (const auto lang : {"en", "zh"}) {
        SCOPED_TRACE(lang);
        const auto doc = QJsonDocument::fromJson(buildRequestBody(config, Channel::Word, words, lang));
        ASSERT_TRUE(doc.isObject());
        const auto root = doc.object();

        EXPECT_EQ(root.value("model").toString(), config.model);
        EXPECT_FALSE(root.value("stream").toBool());
        EXPECT_EQ(root.value("response_format").toObject().value("type").toString(), "json_object");
        // DeepSeek turns thinking on by default; it costs reasoning tokens on every call.
        EXPECT_EQ(root.value("thinking").toObject().value("type").toString(), "disabled");
        EXPECT_GT(root.value("max_tokens").toInt(), 0) << "an unset cap can truncate the JSON";

        const auto messages = root.value("messages").toArray();
        ASSERT_EQ(messages.size(), 2);
        const auto system = messages.at(0).toObject().value("content").toString();
        const auto user = messages.at(1).toObject().value("content").toString();

        EXPECT_EQ(messages.at(0).toObject().value("role").toString(), "system");
        EXPECT_EQ(messages.at(1).toObject().value("role").toString(), "user");
        // response_format json_object is only accepted when the prompt mentions JSON.
        EXPECT_TRUE(system.contains("json", Qt::CaseInsensitive));
        EXPECT_TRUE(system.contains("\"results\"")) << "the prompt shows no format example";
        EXPECT_TRUE(user.contains("ubiquitous") && user.contains("resilience"))
            << "the user message dropped a requested word";
        // The shared body never names a language, so its presence tracks the switch.
        EXPECT_EQ(system.contains(QStringLiteral("Chinese")),
                  QString(lang) == QStringLiteral("zh"))
            << "the explanation language does not switch the prompt's closing line";
    }
}

TEST_F(LlmTest,AcceptsAWellFormedResponse) {
    const auto result = parseExplanations(Channel::Word, envelope(results(kGoodResult)), kAskedFor);

    ASSERT_TRUE(accepted(result)) << errorOf(result).toStdString();
    const auto& explanations = std::get<QVector<WordExplanation>>(result);
    ASSERT_EQ(explanations.size(), 1);
    EXPECT_EQ(explanations.at(0).word, QStringLiteral("ubiquitous"));
    EXPECT_EQ(explanations.at(0).zh, QString::fromUtf8("无处不在的"));
}

TEST_F(LlmTest,RejectsTheWholeBatchOnAnyMalformedResponse) {
    const struct {
        const char* what;
        QByteArray body;
    } cases[] = {
        {"finish_reason=length, the JSON was cut off", envelope(kGoodResult, QStringLiteral("length"))},
        {"content is not JSON", envelope(QStringLiteral("not json at all"))},
        {"no results key", envelope(QStringLiteral("{}"))},
        {"missing field (no zh)", envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"x"})")))},
        {"empty field (no en)",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"","zh":"y"})")))},
        {"misspelled echo",
         envelope(results(QStringLiteral(R"({"word":"ubiquitos","en":"x","zh":"y"})")))},
        {"missing echo", envelope(results(QString()))},
        {"extra echo",
         envelope(results(QStringLiteral(
             R"({"word":"ubiquitous","en":"x","zh":"y"},{"word":"extra","en":"x","zh":"y"})")))},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.what);
        EXPECT_FALSE(accepted(parseExplanations(Channel::Word, item.body, kAskedFor)));
    }
}
