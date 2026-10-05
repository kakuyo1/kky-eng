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
using lens::llm::Usage;
using lens::llm::Explanation;
using lens::llm::buildRequestBody;
using lens::llm::httpErrorFor;
using lens::llm::maskSensitive;
using lens::llm::parseExplanations;
using lens::llm::parseUsage;
using LlmResult = std::variant<QVector<Explanation>, QString>;

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::LlmTest;

/// @return The failure message when @p result is an error, or an empty string on success.
QString errorOf(const LlmResult& result)
{
    const auto* message = std::get_if<QString>(&result);
    return message ? *message : QString();
}

bool accepted(const LlmResult& result)
{
    return std::holds_alternative<QVector<Explanation>>(result);
}

/// @brief Wrap @p content the way the chat-completions envelope does.
QByteArray envelope(const QString& content, const QString& finishReason = QStringLiteral("stop"))
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", finishReason}, {"message", QJsonObject{{"content", content}}}}}}})
        .toJson(QJsonDocument::Compact);
}

/// @return A word-channel content payload carrying @p items as its `results` array.
QString results(const QString& items)
{
    return QStringLiteral("{\"results\":[%1]}").arg(items);
}

const QStringList kAskedFor{"ubiquitous"};
const QString kGoodResult =
    QStringLiteral(R"({"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere","zh":"无处不在的"})");

} // namespace

TEST(LlmPureMask, CollapsesEmailUrlAndLongDigits)
{
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

TEST(LlmPureHttpError, NamesEachStatusTheModelServiceCanAnswerWith)
{
    // Each entry pins a phrase only that arm of the switch carries, and the status number, so a
    // wrong arm -- or one falling through to the default -- fails here rather than in a paid
    // round trip. 402 in particular is DeepSeek's out-of-credit code, which is the one failure a
    // reader has to act on rather than retry.
    const struct {
        int status;
        const char* phrase;
    } cases[] = {
        {400, "malformed"},
        {401, "API key"},
        {402, "credit"},
        {422, "parameters"},
        {429, "rate-limiting"},
        {500, "failed"},
        {503, "overloaded"},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.status);
        const QString message = httpErrorFor(item.status);
        EXPECT_TRUE(message.contains(item.phrase)) << message.toStdString();
        EXPECT_TRUE(message.contains(QString::number(item.status))) << message.toStdString();
    }

    // A status with no arm of its own still names the number rather than collapsing to a
    // generic line, so the reader can search for it.
    EXPECT_TRUE(httpErrorFor(418).contains("418"));
    EXPECT_TRUE(httpErrorFor(0).contains("0"));
}

TEST_F(LlmTest, RequestBodyKeepsTheDeepSeekContract)
{
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
        const auto user   = messages.at(1).toObject().value("content").toString();

        EXPECT_EQ(messages.at(0).toObject().value("role").toString(), "system");
        EXPECT_EQ(messages.at(1).toObject().value("role").toString(), "user");
        // response_format json_object is only accepted when the prompt mentions JSON.
        EXPECT_TRUE(system.contains("json", Qt::CaseInsensitive));
        EXPECT_TRUE(system.contains("\"results\"")) << "the prompt shows no format example";
        // The model copies the example's field names, so every required field has to be in
        // it -- the schema's required list is not in the prompt.
        EXPECT_TRUE(system.contains("\"ipa\"")) << "the format example leaves out the IPA field";
        EXPECT_TRUE(user.contains("ubiquitous") && user.contains("resilience"))
            << "the user message dropped a requested word";
        // The shared body never names a language, so its presence tracks the switch.
        EXPECT_EQ(system.contains(QStringLiteral("Chinese")),
                  QString(lang) == QStringLiteral("zh"))
            << "the explanation language does not switch the prompt's closing line";
    }
}

TEST_F(LlmTest, AcceptsAWellFormedResponse)
{
    const auto result = parseExplanations(Channel::Word, envelope(results(kGoodResult)), kAskedFor);

    ASSERT_TRUE(accepted(result)) << errorOf(result).toStdString();
    const auto& explanations = std::get<QVector<Explanation>>(result);
    ASSERT_EQ(explanations.size(), 1);
    EXPECT_EQ(explanations.at(0).title, QStringLiteral("ubiquitous"));
    EXPECT_EQ(explanations.at(0).ipa, QString::fromUtf8("/juːˈbɪkwɪtəs/")) << "the IPA was dropped on the way out";
    EXPECT_EQ(explanations.at(0).zh, QString::fromUtf8("无处不在的"));
}

TEST_F(LlmTest, UsesSentencePresetsAndTheSharedEntityPreset)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};

    const auto sentencePrompt = [&](const QString& lang, const QString& preset) {
        const auto body = QJsonDocument::fromJson(
            buildRequestBody(config, Channel::Sentence, {"New York is busy."}, lang, preset));
        return body.object().value("messages").toArray().at(0).toObject().value("content").toString();
    };
    const auto entity       = QJsonDocument::fromJson(buildRequestBody(config, Channel::Entity, {"New York"}, "en"));
    const auto entityPrompt = entity.object().value("messages").toArray().at(0).toObject().value("content").toString();

    // With an English explanation language the two sentence items are the same job: explain in
    // plain English. The distinction only exists for Chinese readers.
    EXPECT_EQ(sentencePrompt("en", "translate"), sentencePrompt("en", "explain"));
    EXPECT_TRUE(sentencePrompt("en", "translate").contains("plain English"));

    // With Chinese, translate is literal and explain is plain-language.
    EXPECT_TRUE(sentencePrompt("zh", "translate").contains("literally"));
    EXPECT_TRUE(sentencePrompt("zh", "explain").contains("everyday Chinese"));
    EXPECT_NE(sentencePrompt("zh", "translate"), sentencePrompt("zh", "explain"));

    EXPECT_TRUE(entityPrompt.contains("named entity"));
}

TEST_F(LlmTest, StampsTheRequestedTextOntoEntityAndSentenceResults)
{
    // Entity and sentence responses carry only the two explanations; the app supplies the
    // title, so a model that drifts on a long echo cannot fail the batch.
    const auto entity = parseExplanations(
        Channel::Entity,
        envelope(QStringLiteral(R"({"results":[{"en":"a city","zh":"一座城市"}]})")),
        {"New York"});
    ASSERT_TRUE(accepted(entity)) << errorOf(entity).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(entity).at(0).title, "New York");

    const auto sentence = parseExplanations(
        Channel::Sentence,
        envelope(QStringLiteral(R"({"results":[{"en":"The city is busy.","zh":"纽约很忙。"}]})")),
        {"New York is busy."});
    ASSERT_TRUE(accepted(sentence)) << errorOf(sentence).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(sentence).at(0).title, "New York is busy.");

    // A missing title no longer matters for entity / sentence, but a missing language does.
    const auto missing = parseExplanations(
        Channel::Sentence, envelope(QStringLiteral(R"({"results":[{"zh":"只有中文"}]})")), {"text"});
    EXPECT_FALSE(accepted(missing));
}

TEST_F(LlmTest, FoldsSeveralEntityOrSentenceResultsIntoTheOneRequested)
{
    // A model may split a selected block (a numbered list, several sentences) into more entries
    // than the single item that was sent; fold them instead of failing the whole batch.
    const auto split = parseExplanations(
        Channel::Sentence,
        envelope(QStringLiteral(
            R"({"results":[{"en":"First line.","zh":"第一行。"},{"en":"Second line.","zh":"第二行。"}]})")),
        {"First line.\nSecond line."});
    ASSERT_TRUE(accepted(split)) << errorOf(split).toStdString();
    const auto& result = std::get<QVector<Explanation>>(split).at(0);
    EXPECT_EQ(result.title, "First line.\nSecond line.");
    EXPECT_EQ(result.en, "First line.\nSecond line.");
    EXPECT_EQ(result.zh, "第一行。\n第二行。");
}

TEST_F(LlmTest, ToleratesAMarkdownFencedContent)
{
    // A model often wraps its JSON in a ```json fence even when asked not to; that is not a
    // reason to throw the whole explanation away.
    const QString fenced = QStringLiteral(
        "```json\n{\"results\":[{\"word\":\"ubiquitous\",\"en\":\"x\",\"zh\":\"y\"}]}\n```");
    const auto parsed = parseExplanations(Channel::Word, envelope(fenced), {"ubiquitous"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).at(0).title, "ubiquitous");
}

TEST_F(LlmTest, MatchesAWordEchoCaseInsensitively)
{
    // The request sends a lower-cased lemma; a model may capitalize an acronym on echo. The
    // requested spelling wins, so the cache key and the bubble title stay canonical.
    const auto parsed = parseExplanations(
        Channel::Word,
        envelope(results(QStringLiteral(R"({"word":"QML","en":"a markup language","zh":"一种标记语言"})"))),
        {"qml"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).at(0).title, "qml");
}

TEST_F(LlmTest, RejectsTheWholeBatchOnAnyMalformedResponse)
{
    const struct {
        const char* what;
        QByteArray body;
    } cases[] = {
        {"finish_reason=length, the JSON was cut off", envelope(kGoodResult, QStringLiteral("length"))},
        {"content is not JSON", envelope(QStringLiteral("not json at all"))},
        {"no results key", envelope(QStringLiteral("{}"))},
        {"missing field (no zh)", envelope(results(QStringLiteral(R"({"word":"ubiquitous","ipa":"/x/","en":"x"})")))},
        {"empty field (no en)",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","ipa":"/x/","en":"","zh":"y"})")))},
        {"misspelled echo",
         envelope(results(QStringLiteral(R"({"word":"ubiquitos","ipa":"/x/","en":"x","zh":"y"})")))},
        {"missing echo", envelope(results(QString()))},
        {"extra echo",
         envelope(results(QStringLiteral(
             R"({"word":"ubiquitous","ipa":"/x/","en":"x","zh":"y"},{"word":"extra","ipa":"/x/","en":"x","zh":"y"})")))},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.what);
        EXPECT_FALSE(accepted(parseExplanations(Channel::Word, item.body, kAskedFor)));
    }
}

TEST_F(LlmTest, AcceptsAWordResponseWithoutIPAForAnAcronym)
{
    // IPA is optional even on the word channel: a lone acronym has no pronunciation, and a
    // lone all-caps selection is routed there. The bubble simply draws no pronunciation line.
    const auto parsed = parseExplanations(
        Channel::Word,
        envelope(results(QStringLiteral(R"({"word":"qml","en":"a UI markup language","zh":"一种界面标记语言"})"))),
        {"qml"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    const auto& result = std::get<QVector<Explanation>>(parsed).at(0);
    EXPECT_EQ(result.title, "qml");
    EXPECT_TRUE(result.ipa.isEmpty());
}

TEST_F(LlmTest, ReadsTokenUsageOffTheEnvelope)
{
    const QJsonObject usage{{"prompt_tokens", 1200}, {"completion_tokens", 340}, {"total_tokens", 1540}};
    const QByteArray body = QJsonDocument(QJsonObject{{"usage", usage}}).toJson(QJsonDocument::Compact);

    const Usage counts = parseUsage(body);
    EXPECT_EQ(counts.promptTokens, 1200);
    EXPECT_EQ(counts.completionTokens, 340);
}

TEST_F(LlmTest, TreatsMissingUsageAsZeroRatherThanAFailure)
{
    // A response with no usage is still a good explanation; only the tally loses a line.
    const QByteArray body = envelope(results(kGoodResult));
    EXPECT_TRUE(accepted(parseExplanations(Channel::Word, body, kAskedFor)));

    const Usage counts = parseUsage(body);
    EXPECT_EQ(counts.promptTokens, 0);
    EXPECT_EQ(counts.completionTokens, 0);
}

TEST_F(LlmTest, TreatsNonNumericUsageAsZero)
{
    const QJsonObject usage{{"prompt_tokens", QStringLiteral("many")}, {"completion_tokens", QJsonValue::Null}};
    const QByteArray body = QJsonDocument(QJsonObject{{"usage", usage}}).toJson(QJsonDocument::Compact);

    const Usage counts = parseUsage(body);
    EXPECT_EQ(counts.promptTokens, 0);
    EXPECT_EQ(counts.completionTokens, 0);
}
