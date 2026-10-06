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
    bool usesTestToken = false;
    int posts          = 0;
    QNetworkReply* createRequest(Operation, QNetworkRequest const& request, QIODevice* device) override
    {
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
            core::WordCache entry{"/bank/", language, "meaning", language, {{language, "meaning", language}, {"second", "meaning 2", language}}};
            store.cachePut("bank", entry, {language, multiple});
        }
    }
    store.save();
    auto const restored = core::KnownStore::load(path);
    EXPECT_EQ(restored.document().at("untouched"), "sentinel");
    EXPECT_EQ(restored.cacheGet("bank", {"en", false})->en, "legacy");
    for (auto const* language : {"en", "zh", "es", "ja"}) {
        for (bool const multiple : {false, true}) {
            auto const entry = restored.cacheGet("bank", {language, multiple});
            ASSERT_TRUE(entry);
            EXPECT_EQ(entry->senses.size(), std::string{language} == "en" and not multiple ? 0u : multiple ? 2u
                                                                                                           : 1u);
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

TEST_F(LlmTest, Phase2ProviderSelectionAppliesEndpointModelWireOptionsAndPrices)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "deepseek-flash"}, &manager};
    auto const pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::CostDuty cost{storage.statsStore(), client, pricing};
    app::ExplanationDuty duty{storage, client, cost};
    for (auto const* provider : {"DeepSeek", "openai"}) {
        ASSERT_TRUE(duty.setProvider(provider));
        auto const defaults = llm::serviceProvider(provider);
        EXPECT_EQ(client.model(), defaults.value("defaultModel").toString());
        EXPECT_EQ(store.document().at("MODEL"), client.model().toStdString());
        duty.setSelection(selection());
        duty.dismissBubble();
        store.setExplanationLang("en");
        // A fresh lemma is not needed: clear the in-memory cache by using a different mode.
        store.document()["multiSense"] = provider == std::string{"DeepSeek"} ? "false" : "true";
        run(duty, client);
        EXPECT_EQ(manager.endpoint, QUrl{defaults.value("baseUrl").toString() + "/chat/completions"});
        auto const body = QJsonDocument::fromJson(manager.posted).object();
        EXPECT_EQ(body.contains("thinking"), provider == std::string{"DeepSeek"});
        EXPECT_GT(pricing.cost(client.model(), {1000, 1000}), 0.0);
        EXPECT_FALSE(duty.settings().value("modelPrice").toString().isEmpty());
    }
    EXPECT_FALSE(duty.setProvider("unknown"));
    auto const previous = client.model();
    EXPECT_TRUE(duty.setProvider("custom"));
    EXPECT_EQ(client.model(), previous);
    EXPECT_EQ(pricing.cost("unlisted-custom", {1000, 1000}), 0.0);
    auto const restored = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    EXPECT_EQ(restored.document().at("PROVIDER"), "custom");
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
    Manager manager;
    llm::LlmClient client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    app::AppController controller{store, client, hook, pricing};
    int settingsChanges = 0;
    QObject::connect(&controller, &app::AppController::settingsChanged, [&] { ++settingsChanges; });
    auto const initial = controller.settings();
    EXPECT_EQ(initial.value("languages").toList().size(), 4);
    EXPECT_EQ(initial.value("providers").toList().size(), 3);
    EXPECT_EQ(initial.value("provider").toString(), "DeepSeek");
    EXPECT_EQ(initial.value("model").toString(), "deepseek-flash");
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
    EXPECT_EQ(store.document().at("MODEL"), "deepseek-flash");
    EXPECT_EQ(store.document().at("URL"), "https://api.deepseek.com");
    EXPECT_FALSE(request(client).contains("thinking"));
    settingsChanges = 0;
    controller.setProvider("openai");
    controller.setModel(" gpt-4.1-nano ");
    EXPECT_EQ(settingsChanges, 2);
    auto const chosen = controller.settings();
    EXPECT_EQ(chosen.value("models").toList().size(), 2);
    EXPECT_EQ(chosen.value("model").toString(), "gpt-4.1-nano");
    EXPECT_EQ(chosen.value("url").toString(), "https://api.openai.com/v1");
    EXPECT_EQ(chosen.value("providerDefaultUrl").toString(), "https://api.openai.com/v1");
    EXPECT_FALSE(chosen.value("modelPrice").toString().isEmpty());
    auto const body = request(client);
    EXPECT_EQ(body.value("model").toString(), "gpt-4.1-nano");
    EXPECT_FALSE(body.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://api.openai.com/v1/chat/completions"});

    controller.setProvider("unknown");
    controller.setModel(" ");
    EXPECT_EQ(settingsChanges, 2);
    controller.setApiUrl("https://example.invalid/v2");
    auto restored = core::KnownStore::load(path);
    llm::LlmClient restarted{{QUrl{"https://wrong.invalid"}, "fake-test-token", "wrong-model"}, &manager};
    app::AppController restoredController{restored, restarted, hook, pricing};
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
    app::AppController customController{customStore, custom, hook, pricing};
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
    llm::LlmClient client{{QUrl{"https://safe.invalid"}, "fake-test-token", "test-model"}};

    app::AppController controller{store, client, hook, pricing};

    EXPECT_EQ(client.baseUrl(), QUrl{"https://api.deepseek.com"});
    EXPECT_EQ(controller.settings().value("url").toString(), "https://api.deepseek.com");
}
