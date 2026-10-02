/**
 * @file llm_smoke_test.cpp
 * @brief One word through the real model, to check the round trip end to end.
 *
 * This spends money and needs a key, so it is never run by CI: it reports a skip when
 * settings.local.json has no credentials, and a human reads the fields it prints. The word
 * comes from LENS_SMOKE_WORD, defaulting to "ubiquitous".
 *
 * The key is read into memory and never printed, logged, or echoed in a failure.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/log.h"
#include "llm/llm_client.h"
#include "llm/qt_log.h"
#include "llm_support.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using lens::llm::Config;
using lens::llm::LlmClient;
using lens::llm::WordExplanation;

/// How long to wait for the model before calling it a timeout.
constexpr int kTimeoutMs = 60'000;

std::optional<Config> readConfigFrom(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) return std::nullopt;

    const auto doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return std::nullopt;

    const std::string url = doc.value("URL", std::string());
    const std::string model = doc.value("MODEL", std::string());
    const std::string key = doc.value("API-KEY", std::string());
    if (url.empty() || model.empty() || key.empty()) return std::nullopt;

    return Config{QUrl(QString::fromStdString(url)), QString::fromStdString(key), QString::fromStdString(model)};
}

} // namespace

TEST(LlmSmoke, OneWordRoundTrip)
{
    lens::test::requireLlmProtocolLoaded();

    const auto config = readConfigFrom(lens::test::sourceDir() / "settings.local.json");
    if (!config)
        GTEST_SKIP() << "settings.local.json has no URL / MODEL / API-KEY: the smoke test "
                        "needs a key and spends money, so it is skipped rather than failed";

    const QString word = qEnvironmentVariable("LENS_SMOKE_WORD", QStringLiteral("ubiquitous"));
    std::cout << "smoke: '" << word.toStdString() << "' via " << config->model.toStdString()
              << " @ " << config->baseUrl.toString().toStdString()
              << " (key read into memory, never printed)\n";

    LlmClient client(*config);

    QEventLoop loop;
    std::optional<QVector<WordExplanation>> results;
    QString failure;

    QObject::connect(&client, &LlmClient::batchFinished, [&](const QVector<WordExplanation>& explained) {
        results = explained;
        loop.quit();
    });
    QObject::connect(&client, &LlmClient::failed, [&](const QString& message) {
        failure = message;
        loop.quit();
    });
    QTimer::singleShot(kTimeoutMs, &loop, &QEventLoop::quit);

    client.explainWords({word});
    loop.exec();

    ASSERT_TRUE(failure.isEmpty()) << "the request failed: " << failure.toStdString();
    ASSERT_TRUE(results.has_value()) << "no answer within " << kTimeoutMs << " ms";

    ASSERT_EQ(results->size(), 1);
    const auto& explanation = results->front();
    std::cout << "word: " << explanation.word.toStdString() << "\nen:   "
              << explanation.en.toStdString() << "\nzh:   " << explanation.zh.toStdString() << "\n";

    EXPECT_EQ(explanation.word, word);
    EXPECT_FALSE(explanation.en.isEmpty());
    EXPECT_FALSE(explanation.zh.isEmpty());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    QCoreApplication app(argc, argv); // LlmClient needs an event loop, not a window

    lens::log::init(spdlog::level::trace, lens::test::sourceDir() / "logs");
    lens::log::installQtMessageHandler();

    return RUN_ALL_TESTS();
}
