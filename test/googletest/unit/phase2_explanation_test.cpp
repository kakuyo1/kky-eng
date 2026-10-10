/**
 * @file phase2_explanation_test.cpp
 * @brief Offline Phase 2 explanation contracts, persistence and provider round trips.
 */

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>

#include "app/app_controller.h"
#include "app/explanation_duty.h"
#include "app/storage_duty.h"
#include "core/known_store.h"
#include "llm/llm_pricing.h"
#include "llm/llm_pure.h"
#include "llm_support.h"

namespace {

using namespace lens;
using lens::test::LlmTest;

QByteArray envelope(QJsonArray const& results)
{
    auto const content = QString::fromUtf8(QJsonDocument{QJsonObject{{"results", results}}}.toJson());
    return QJsonDocument{QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}},
                                     {"usage", QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}}}}
        .toJson();
}

QJsonObject word(int const count = 3, QString const& translation = QStringLiteral("translation"))
{
    QJsonArray senses;
    for (int i = 0; i < count; ++i)
        senses.append(QJsonObject{{"en", QStringLiteral("sense %1").arg(i)}, {"zh", QStringLiteral("meaning %1").arg(i)}, {"translation", translation}});
    return {{"word", "bank"}, {"ipa", "/bank/"}, {"senses", senses}};
}

QJsonObject plainWord(QString const& lemma, QString const& etymology = {})
{
    QJsonObject value{{"word", lemma}, {"ipa", "/x/"}, {"en", "English"}, {"zh", "中文"}};
    if (not etymology.isEmpty()) value["etymology"] = etymology;
    return value;
}

struct QtApplication {
    int argc      = 1;
    char name[32] = "phase2_explanation_test";
    char* argv[2] = {name, nullptr};
    QCoreApplication application{argc, argv};
};

struct Reply final : QNetworkReply {
    explicit Reply(QByteArray body_, QObject* parent)
        : QNetworkReply(parent), body(std::move(body_))
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { setFinished(true); emit finished(); });
    }
    void abort() override
    {}
    qint64 readData(char* data, qint64 const size) override
    {
        if (body.isEmpty()) return -1;
        auto const count = std::min(size, static_cast<qint64>(body.size()));
        std::memcpy(data, body.constData(), static_cast<std::size_t>(count));
        body.remove(0, count);
        return count;
    }
    QByteArray body;
};

struct Manager final : QNetworkAccessManager {
    QByteArray response = envelope({word()});
    QByteArray posted;
    QUrl endpoint;
    QUrl listed;
    QByteArray listedAuthorization;
    QByteArray listedGoogleKey;
    QByteArray modelResponseOverride;
    bool usesTestToken = false;
    int posts          = 0;
    int gets           = 0;
    QNetworkReply* createRequest(Operation operation, QNetworkRequest const& request, QIODevice* device) override
    {
        // The model list is a GET and carries no body at all, so it is answered without a device:
        // the assertions here are about the POST, and reading a device this call does not have is
        // how a null dereference gets into a test fake.
        if (operation == GetOperation) {
            ++gets;
            listed              = request.url();
            listedAuthorization = request.rawHeader("Authorization");
            listedGoogleKey     = request.rawHeader("x-goog-api-key");
            // One source, answered by host. The body spans two vendors and carries the two kinds of
            // entry the reader must never see: a routing variant and a model that also writes image.
            const QByteArray ids = not modelResponseOverride.isEmpty()
                                       ? modelResponseOverride
                                   : request.url().host() == QLatin1String("openrouter.ai")
                                       ? QByteArrayLiteral(R"({"data":[
        {"id":"openai/gpt-4.1-mini","architecture":{"output_modalities":["text"]}},
        {"id":"openai/gpt-4.1-mini:free","architecture":{"output_modalities":["text"]}},
        {"id":"openai/image-preview","architecture":{"output_modalities":["text","image"]}},
        {"id":"deepseek/deepseek-chat","architecture":{"output_modalities":["text"]}},
        {"id":"google/gemini-2.5-flash","architecture":{"output_modalities":["text"]}}
    ]})")
                                       : QByteArrayLiteral(R"({"data":[]})");
            return new Reply{ids, this};
        }
        ++posts;
        endpoint      = request.url();
        usesTestToken = request.rawHeader("Authorization") == "Bearer fake-test-token";
        posted        = device->readAll();
        return new Reply{response, this};
    }
};

void run(app::ExplanationDuty& duty, llm::LlmClient& client)
{
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QObject::connect(&client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    duty.runSelectionAction("explain", "bank");
    loop.exec();
}

app::PendingSelection selectionFor(QString const& lemma)
{
    app::PendingSelection result;
    result.kind   = "word";
    result.text   = lemma;
    result.lemma  = lemma;
    result.preset = "default";
    result.anchor = QPoint{200, 400};
    return result;
}

app::PendingSelection selection()
{
    return selectionFor("bank");
}

} // namespace

TEST_F(LlmTest, Phase2AcceptsLegacyAndCapsOrderedSenses)
{
    auto const legacy = llm::parseExplanations(llm::Channel::Word,
                                               envelope({QJsonObject{{"word", "bank"}, {"en", "first"}, {"zh", "meaning"}}}),
                                               {"bank"});
    ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(legacy));
    EXPECT_EQ(std::get<QVector<llm::Explanation>>(legacy).front().senses.size(), 1);
    for (bool const multiple : {false, true}) {
        auto const parsed = llm::parseExplanations(llm::Channel::Word, envelope({word(5)}), {"bank"}, "en", multiple);
        ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(parsed));
        auto const result = std::get<QVector<llm::Explanation>>(parsed).front();
        EXPECT_EQ(result.senses.size(), multiple ? 3 : 1);
        EXPECT_EQ(result.en, "sense 0");
        EXPECT_EQ(result.ipa, "/bank/");
    }
}

TEST_F(LlmTest, Phase2RejectsMalformedNestedSensesAndDuplicateWords)
{
    for (auto const& senses : QJsonArray{QJsonArray{}, "wrong type", QJsonArray{7}, QJsonArray{QJsonObject{{"en", "valid"}}}, QJsonArray{QJsonObject{{"en", " "}, {"zh", "valid"}}}}) {
        auto value      = word();
        value["senses"] = senses;
        EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(llm::Channel::Word, envelope({value}), {"bank"}, "en", true)));
    }
    EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(llm::Channel::Word, envelope({word(), word()}), {"bank"}, "en", true)));
    auto invalidTail      = word(4);
    auto senses           = invalidTail["senses"].toArray();
    senses[3]             = QJsonObject{{"en", "tail"}};
    invalidTail["senses"] = senses;
    EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(llm::Channel::Word, envelope({invalidTail}), {"bank"}, "en", true)));
}

TEST_F(LlmTest, Phase2AdditionalLanguagesRequireTextOnEveryChannel)
{
    for (auto const* language : {"es", "ja"}) {
        for (auto const channel : {llm::Channel::Word, llm::Channel::Entity, llm::Channel::Sentence}) {
            QJsonObject const value = channel == llm::Channel::Word ? word() : QJsonObject{{"en", "English"}, {"zh", "Chinese"}, {"translation", "selected language"}};
            auto const parsed       = llm::parseExplanations(channel, envelope({value}), {"bank"}, language, true);
            ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(parsed));
            EXPECT_FALSE(std::get<QVector<llm::Explanation>>(parsed).front().translation.isEmpty());
            auto missing = value;
            if (channel == llm::Channel::Word)
                missing = word(2, QString{});
            else
                missing.remove("translation");
            EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(channel, envelope({missing}), {"bank"}, language, true)));
        }
    }
}

TEST_F(LlmTest, Phase2CachePreservesLegacyEntriesAndSeparatesAllContexts)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto const path = std::filesystem::path{temporary.path().toStdWString()} / "fixture.json";
    {
        std::ofstream file{path};
        file << R"({"untouched":"sentinel","cache":{"en":{"bank":{"ipa":"","en":"legacy","zh":"legacy meaning"},"broken":{"ipa":7,"en":"x","zh":"y"}}}})";
    }
    auto store = core::KnownStore::load(path);
    ASSERT_TRUE(store.cacheGet("bank", {"en", false}));
    EXPECT_FALSE(store.cacheGet("bank", {"en", true}));
    EXPECT_FALSE(store.cacheGet("broken"));
    for (auto const* language : {"en", "zh", "es", "ja"}) {
        for (bool const multiple : {false, true}) {
            if (std::string{language} == "en" and not multiple) continue;
            core::WordCache entry{.ipa = "/bank/", .en = language, .zh = "meaning", .translation = language, .etymology = "from " + std::string{language}};
            entry.senses = {{language, "meaning", language}, {"second", "meaning 2", language}};
            store.cachePut("bank", entry, {language, multiple});
        }
    }
    store.save();
    auto const restored = core::KnownStore::load(path);
    EXPECT_EQ(restored.document().at("untouched"), "sentinel");
    EXPECT_EQ(restored.cacheGet("bank", {"en", false})->en, "legacy");
    // A legacy entry has no origin, and one must not be invented for it.
    EXPECT_TRUE(restored.cacheGet("bank", {"en", false})->etymology.empty());
    for (auto const* language : {"en", "zh", "es", "ja"}) {
        for (bool const multiple : {false, true}) {
            auto const entry = restored.cacheGet("bank", {language, multiple});
            ASSERT_TRUE(entry);
            EXPECT_EQ(entry->senses.size(), std::string{language} == "en" and not multiple ? 0u : multiple ? 2u
                                                                                                           : 1u);
            // Outside the cache key, so it survives every context this loop covers.
            if (not(std::string{language} == "en" and not multiple))
                EXPECT_EQ(entry->etymology, "from " + std::string{language});
        }
    }
}

TEST_F(LlmTest, Phase2LanguagesRoundTripThroughDutyTransportAndCache)
{
    QtApplication qt;
    for (auto const* language : {"es", "ja"}) {
        QTemporaryDir temporary;
        auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
        store.setExplanationLang(language);
        store.document()["multiSense"] = "true";
        app::StorageDuty storage{store};
        Manager manager;
        llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
        auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
        app::CostDuty cost{storage.statsStore(), client, pricing};
        app::ExplanationDuty duty{storage, client, cost};
        duty.setSelection(selection());
        run(duty, client);
        ASSERT_EQ(duty.bubble().value("senses").toList().size(), 3);
        EXPECT_EQ(duty.bubble().value("senses").toList().front().toMap().value("text").toString(), "translation");
        EXPECT_TRUE(manager.posted.contains(language == std::string{"es"} ? "Spanish" : "Japanese"));
        ASSERT_TRUE(store.cacheGet("bank", {language, true}));
        duty.dismissBubble();
        duty.runSelectionAction("explain", "bank");
        EXPECT_EQ(manager.posts, 1);
        EXPECT_EQ(duty.bubble().value("senses").toList().size(), 3);
        EXPECT_EQ(store.cacheGet("bank", {language, true})->senses.size(), 3u);
    }
}

TEST_F(LlmTest, Phase2TheEtymologySettingBringsTheOriginAlongAndKeepsItOutOfTheKey)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};

    auto const explain = [&](QString const& lemma) {
        const int before = manager.posts;
        duty.setSelection(selectionFor(lemma));
        QEventLoop loop;
        QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
        QObject::connect(&client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        duty.runSelectionAction("explain", lemma);
        // A word already stored is answered without a round trip, so waiting would be waiting for
        // something that is not coming -- and the bubble is up before this returns.
        if (manager.posts > before) loop.exec();
    };
    auto const askedForOrigin = [&] {
        // The prompt is a string inside the posted body, so its own quotes arrive escaped; the
        // question is what the model was told, which means reading the body.
        const auto messages = QJsonDocument::fromJson(manager.posted).object().value("messages").toArray();
        if (messages.isEmpty()) return false;
        return messages.at(0).toObject().value("content").toString().contains(QStringLiteral("\"etymology\":\"...\""));
    };

    // Explained with the setting off, the word is bought without its origin and the card shows none.
    manager.response = envelope({plainWord("bank")});
    explain("bank");
    EXPECT_FALSE(askedForOrigin()) << "the origin was paid for though the setting was off";
    EXPECT_TRUE(duty.bubble().value("etymology").toString().isEmpty());

    // The setting decides what is asked for, never what was already paid for: turning it on does
    // not go back and buy the origin of a word explained while it was off (docs/adr/0019).
    duty.dismissBubble();
    store.document()["etymology"] = "true";
    explain("bank");
    EXPECT_EQ(manager.posts, 1) << "the stored definition was bought a second time";
    EXPECT_TRUE(duty.bubble().value("etymology").toString().isEmpty());

    // Asked for from the start, the origin arrives with the definition and is drawn under it.
    duty.dismissBubble();
    manager.response = envelope({plainWord("resilience", "Latin resilire, to leap back.")});
    explain("resilience");
    ASSERT_TRUE(askedForOrigin());
    EXPECT_EQ(duty.bubble().value("etymology").toString(), QStringLiteral("Latin resilire, to leap back."));

    // And it is stored, so putting it away and taking it out again costs nothing: the entry is
    // reused whole, and the setting only decides whether the card draws the part it holds.
    duty.dismissBubble();
    store.document()["etymology"] = "false";
    explain("resilience");
    EXPECT_EQ(manager.posts, 2);
    EXPECT_TRUE(duty.bubble().value("etymology").toString().isEmpty()) << "a stored origin was drawn with the setting off";

    duty.dismissBubble();
    store.document()["etymology"] = "true";
    explain("resilience");
    EXPECT_EQ(manager.posts, 2) << "the same word was paid for twice";
    EXPECT_EQ(duty.bubble().value("etymology").toString(), QStringLiteral("Latin resilire, to leap back."));
}

TEST_F(LlmTest, Phase2ChangingSettingsDuringResponseKeepsTheOriginalCacheContext)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store                     = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    store.document()["multiSense"] = "true";
    app::StorageDuty storage{store};
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};
    duty.setSelection(selection());
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    duty.runSelectionAction("explain", "bank");
    store.setExplanationLang("ja");
    store.document()["multiSense"] = "false";
    loop.exec();
    EXPECT_TRUE(duty.bubble().isEmpty());
    EXPECT_TRUE(store.cacheGet("bank", {"en", true}));
    EXPECT_FALSE(store.cacheGet("bank", {"ja", false}));
}

TEST_F(LlmTest, Phase2AProviderChoiceMovesTheEndpointItsWireOptionsAndTheModel)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    // A model that belongs to neither service, so both trips through the loop prove the same
    // thing: the one standing is from somewhere else, and following the service replaces it.
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "gpt-4.1-nano"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};
    for (auto const* provider : {"DeepSeek", "openai"}) {
        ASSERT_TRUE(duty.setProvider(provider));
        auto const entry = llm::serviceProvider(provider);
        // The endpoint, its wire quirks -- and the model, which follows the service: a name from
        // another one is the 400 whose message names nothing (docs/adr/0017).
        EXPECT_EQ(client.model(), provider == std::string_view{"DeepSeek"} ? "deepseek-flash" : "gpt-4.1-mini");
        EXPECT_EQ(store.document().at("URL"), entry.value("baseUrl").toString().toStdString());
        duty.setSelection(selection());
        duty.dismissBubble();
        store.setExplanationLang("en");
        // A fresh lemma is not needed: clear the in-memory cache by using a different mode.
        store.document()["multiSense"] = provider == std::string{"DeepSeek"} ? "false" : "true";
        run(duty, client);
        EXPECT_EQ(manager.endpoint, QUrl{entry.value("baseUrl").toString() + "/chat/completions"});
        auto const body = QJsonDocument::fromJson(manager.posted).object();
        EXPECT_EQ(body.contains("thinking"), provider == std::string{"DeepSeek"});
    }
    // One shared source, not one endpoint per provider, and it is asked with nothing the reader
    // owns. What came back is split per provider by the vendor prefix in the catalog, and each
    // slice is that provider's cache -- the seed is only for a machine that has never reached it.
    EXPECT_GE(manager.gets, 1);
    EXPECT_EQ(manager.listed, QUrl{"https://openrouter.ai/api/v1/models"});
    EXPECT_TRUE(manager.listedAuthorization.isEmpty()) << "the shared source was sent the API key";
    EXPECT_TRUE(manager.listedGoogleKey.isEmpty());
    // The routing variant and the image-capable entry are in the answer and in neither slice.
    EXPECT_EQ(store.document().at("MODELS").at("DeepSeek"), nlohmann::json::array({"deepseek-chat"}));
    EXPECT_EQ(store.document().at("MODELS").at("openai"), nlohmann::json::array({"gpt-4.1-mini"}));

    // The price shown is the typed model's, not the provider's.
    ASSERT_TRUE(duty.setModel("gpt-4.1-mini"));
    EXPECT_GT(pricing.cost(client.model(), {1000, 1000}), 0.0);
    EXPECT_FALSE(duty.settings().value("modelPrice").toString().isEmpty());
    ASSERT_TRUE(duty.setModel("unlisted-model"));
    EXPECT_EQ(pricing.cost("unlisted-custom", {1000, 1000}), 0.0);
    EXPECT_TRUE(duty.settings().value("modelPrice").toString().contains("No listed price"));
    EXPECT_FALSE(duty.setProvider("unknown"));
    EXPECT_TRUE(duty.setProvider("custom"));
    EXPECT_EQ(client.model(), "unlisted-model");
    auto const restored = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    EXPECT_EQ(restored.document().at("PROVIDER"), "custom");
}

/// The contract of the shared source in one case: one GET, no key, and an answer that reaches
/// more than one provider's cache because it is classified per vendor rather than per endpoint.
TEST_F(LlmTest, OneSharedSourceIsAskedWithNoKeyAndItsAnswerReachesEveryProvider)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    // A client holding no key at all, which used to be exactly the case that fetched nothing.
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "", "deepseek-flash"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};
    QStringList fetched;
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::modelsFetched, &loop, [&](QStringList models) {
        fetched = std::move(models);
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);

    duty.refreshModels();
    loop.exec();

    EXPECT_EQ(manager.gets, 1);
    EXPECT_EQ(manager.listed, QUrl{"https://openrouter.ai/api/v1/models"});
    EXPECT_TRUE(manager.listed.query().isEmpty()) << "the shared source was asked a filtered question";
    EXPECT_TRUE(manager.listedAuthorization.isEmpty()) << "the API key went to the shared source";
    EXPECT_TRUE(manager.listedGoogleKey.isEmpty());

    // Whole catalogue, prefix-stripped per provider, and the last provider gets no entry at all.
    ASSERT_EQ(fetched.size(), 3);
    EXPECT_EQ(fetched.front(), QStringLiteral("openai/gpt-4.1-mini"));
    EXPECT_EQ(store.document().at("MODELS").at("DeepSeek"), nlohmann::json::array({"deepseek-chat"}));
    EXPECT_EQ(store.document().at("MODELS").at("openai"), nlohmann::json::array({"gpt-4.1-mini"}));
    EXPECT_EQ(store.document().at("MODELS").at("Google"), nlohmann::json::array({"gemini-2.5-flash"}));
    EXPECT_FALSE(store.document().at("MODELS").contains("custom")) << "custom declares no prefix";
}

TEST_F(LlmTest, ModelListRefreshKeepsTheUserCacheWhenAResponseIsMalformed)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store                             = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    store.document()["MODELS"]["DeepSeek"] = nlohmann::json::array({"cached-model"});
    app::MouseSelectionHook hook;
    app::GlobalHotkey hotkey;
    Manager manager;
    manager.modelResponseOverride = QByteArrayLiteral(R"({"data":"malformed"})");
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "deepseek-flash"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::AppController controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};

    ASSERT_EQ(controller.models().size(), 1);
    EXPECT_EQ(controller.models().front().toMap().value("value").toString(), "cached-model");
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::modelsFetched, &loop, &QEventLoop::quit);
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(controller.models().front().toMap().value("value").toString(), "cached-model");
    EXPECT_EQ(store.document().at("MODELS").at("DeepSeek").at(0), "cached-model");
}

/// A fetched list is the industry's, not the standing service's, so it does not get to say what
/// that service carries: the model in force is left exactly as it was. The correction lives where
/// it fired -- the reader changing the service -- and still happens there.
TEST_F(LlmTest, AFetchedListNeverRewritesTheModelInForceButAProviderChangeStillDoes)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    // A model from another service, so setProvider writes MODEL to deepseek-flash and the fetch
    // then has a standing value it would have to overwrite to prove it does not.
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "gpt-4.1-nano"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};

    ASSERT_TRUE(duty.setProvider("DeepSeek"));
    ASSERT_EQ(client.model(), "deepseek-flash");
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::modelsFetched, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    // The answer lists deepseek-chat, not the deepseek-flash that is in force. Under the old rule
    // this silently moved every DeepSeek reader onto a name the price list does not carry.
    ASSERT_EQ(store.document().at("MODELS").at("DeepSeek").at(0), "deepseek-chat");
    EXPECT_EQ(client.model(), "deepseek-flash");
    EXPECT_EQ(store.document().at("MODEL"), "deepseek-flash");
    EXPECT_FALSE(pricing.cost(client.model(), {1000, 1000}) <= 0.0) << "the shipped default lost its price";

    // Re-picking the provider already in force is a tap on the dropdown row, not a change, and the
    // dropdown passes the current value straight through. Before the shared model list this tap was
    // harmless, because the list it corrected against was this service's own; now it would replace
    // a valid model with whichever id the source happens to list first.
    ASSERT_TRUE(duty.setProvider("DeepSeek"));
    EXPECT_EQ(client.model(), "deepseek-flash") << "re-picking the standing provider rewrote the model";
    EXPECT_EQ(store.document().at("MODEL"), "deepseek-flash");

    // Changing the service is the one place the model still follows: the standing one is by
    // construction from somewhere else, and the 400 that names nothing is what that produces.
    ASSERT_TRUE(duty.setProvider("openai"));
    EXPECT_EQ(client.model(), "gpt-4.1-mini");
    EXPECT_EQ(store.document().at("MODEL"), "gpt-4.1-mini");
}

TEST_F(LlmTest, Phase2DefaultsToOneSenseAndQueuesTheLatestRequestedMode)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};
    duty.setSelection(selection());
    run(duty, client);
    EXPECT_EQ(duty.bubble().value("senses").toList().size(), 1);
    EXPECT_TRUE(duty.bubble().value("senses").toList().front().toMap().value("text").toString().isEmpty());
    EXPECT_FALSE(manager.posted.contains("one to three"));

    store.setExplanationLang("es");
    QEventLoop loop;
    int responses = 0;
    QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, [&](auto, llm::Usage const usage) {
        EXPECT_EQ(usage.model, "test-model");
        if (++responses == 2) loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    duty.setSelection(selection());
    duty.runSelectionAction("explain", "bank");
    store.document()["multiSense"] = "true";
    duty.setSelection(selection());
    duty.runSelectionAction("explain", "bank");
    loop.exec();
    EXPECT_EQ(responses, 2);
    EXPECT_EQ(manager.posts, 3);
    EXPECT_EQ(duty.bubble().value("senses").toList().size(), 3);
    EXPECT_TRUE(store.cacheGet("bank", {"es", false}));
    EXPECT_TRUE(store.cacheGet("bank", {"es", true}));
}

TEST_F(LlmTest, Phase2BudgetStopsQueuedDispatchWithoutAnErrorNotice)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "deepseek-flash"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    const QDate date   = QDate::currentDate();
    app::CostDuty cost{storage.statsStore(), client, pricing, [date] { return date; }};
    ASSERT_TRUE(cost.setDailyBudget(0.000045));
    app::ExplanationDuty duty{storage, client, cost};

    duty.setSelection(selection());
    duty.runSelectionAction("explain", "bank");
    duty.setSelection(selectionFor("river"));
    duty.runSelectionAction("explain", "river");

    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(manager.posts, 1);
    EXPECT_TRUE(cost.budgetStatus().exhausted);
    EXPECT_TRUE(duty.notice().isEmpty());
}

TEST_F(LlmTest, Phase2ControllerAppliesChoicesAndRestoresPersistedWireSettings)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto const path             = std::filesystem::path{temporary.path().toStdWString()} / "fixture.json";
    auto store                  = core::KnownStore::load(path);
    store.document()["API-KEY"] = "fake-test-token";
    auto const pricing          = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::MouseSelectionHook hook;
    app::GlobalHotkey hotkey;
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    app::AppController controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};
    int settingsChanges = 0;
    QObject::connect(&controller, &app::AppController::settingsChanged, [&] { ++settingsChanges; });
    auto const initial = controller.settings();
    EXPECT_EQ(initial.value("languages").toList().size(), 4);
    EXPECT_EQ(initial.value("providers").toList().size(), 9);
    EXPECT_EQ(initial.value("provider").toString(), "DeepSeek");
    // The provider carries no model, so the one the client was built with stands.
    EXPECT_EQ(initial.value("model").toString(), "test-model");
    EXPECT_FALSE(initial.contains("API-KEY"));
    EXPECT_FALSE(initial.contains("apiKey"));

    auto const request = [&](llm::LlmClient& active) {
        QEventLoop loop;
        QObject::connect(&active, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
        QObject::connect(&active, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        active.explainWords({"bank"});
        loop.exec();
        EXPECT_TRUE(manager.usesTestToken);
        return QJsonDocument::fromJson(manager.posted).object();
    };
    EXPECT_TRUE(request(client).contains("thinking"));
    controller.setProvider("custom");
    EXPECT_EQ(controller.settings().value("url").toString(), "https://api.deepseek.com");
    // The custom endpoint keeps the address it was on, and a model is always in force: which one
    // follows from the service, and the rule itself is pinned in the provider test above.
    EXPECT_FALSE(controller.settings().value("model").toString().isEmpty());
    EXPECT_EQ(store.document().at("URL"), "https://api.deepseek.com");
    EXPECT_FALSE(request(client).contains("thinking"));
    settingsChanges = 0;
    controller.setProvider("openai");
    controller.setModel(" gpt-4.1-nano ");
    EXPECT_EQ(settingsChanges, 2);
    auto const chosen = controller.settings();
    EXPECT_EQ(chosen.value("model").toString(), "gpt-4.1-nano");
    EXPECT_EQ(chosen.value("url").toString(), "https://api.openai.com/v1");
    EXPECT_FALSE(chosen.value("modelPrice").toString().isEmpty());
    auto const body = request(client);
    EXPECT_EQ(body.value("model").toString(), "gpt-4.1-nano");
    EXPECT_FALSE(body.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://api.openai.com/v1/chat/completions"});

    controller.setProvider("unknown");
    controller.setModel(" ");
    // Two: the provider and the model. A fetched model list is not one of them any more -- it
    // changes nothing in the settings map (docs/adr/0020) -- and neither rejected call counts.
    EXPECT_EQ(settingsChanges, 2);
    controller.setApiUrl("https://example.invalid/v2");
    auto restored = core::KnownStore::load(path);
    llm::LlmClient restarted{{QUrl{"https://wrong.invalid"}, "fake-test-token", "wrong-model"}, &manager};
    app::AppController restoredController{restored, restarted, hook, hotkey, pricing, test::sourceDir() / "data"};
    auto const restoredBody = request(restarted);
    EXPECT_EQ(restoredBody.value("model").toString(), "gpt-4.1-nano");
    EXPECT_FALSE(restoredBody.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://example.invalid/v2/chat/completions"});
    EXPECT_EQ(restoredController.settings().value("provider").toString(), "openai");
    EXPECT_EQ(restored.document().at("API-KEY"), "fake-test-token");

    controller.setProvider("custom");
    controller.setModel("custom-model");
    auto customStore = core::KnownStore::load(path);
    llm::LlmClient custom{{QUrl{"https://wrong.invalid"}, "fake-test-token", "wrong-model"}, &manager};
    app::AppController customController{customStore, custom, hook, hotkey, pricing, test::sourceDir() / "data"};
    auto const customBody = request(custom);
    EXPECT_EQ(customBody.value("model").toString(), "custom-model");
    EXPECT_FALSE(customBody.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://example.invalid/v2/chat/completions"});
    EXPECT_EQ(customController.settings().value("provider").toString(), "custom");
}

TEST_F(LlmTest, Phase2ControllerRejectsAnInsecurePersistedServiceUrl)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto store                   = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    store.document()["PROVIDER"] = "DeepSeek";
    store.document()["URL"]      = "http://untrusted.invalid/v1";
    store.document()["MODEL"]    = "deepseek-flash";
    auto const pricing           = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::MouseSelectionHook hook;
    app::GlobalHotkey hotkey;
    llm::LlmClient client{{QUrl{"https://safe.invalid"}, "fake-test-token", "test-model"}};

    app::AppController controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};

    EXPECT_EQ(client.baseUrl(), QUrl{"https://api.deepseek.com"});
    EXPECT_EQ(controller.settings().value("url").toString(), "https://api.deepseek.com");
}
