/**
 * @file llm_mock_transport_test.cpp
 * @brief Offline HTTP round trips for the channel-aware LlmClient.
 *
 * QTcpServer stays on loopback, so this exercises request preparation, channel selection,
 * response parsing, and the asynchronous signal without a real service or API key.
 */

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <gtest/gtest.h>

#include <utility>

#include "llm/llm_client.h"
#include "llm_support.h"

namespace {

using lens::llm::Channel;
using lens::llm::Config;
using lens::llm::Explanation;
using lens::llm::LlmClient;
using lens::llm::Usage;
using lens::test::LlmTest;

QByteArray entityResponse()
{
    const QString content = QStringLiteral(
        R"({"results":[{"en":"A major city in the United States.","zh":"美国的一座大城市。"}]})");
    return QJsonDocument(QJsonObject{
                             {"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}},
                             {"usage", QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}},
                         })
        .toJson(QJsonDocument::Compact);
}

} // namespace

TEST_F(LlmTest, CompletesEntityAgainstAnOfflineHttpServer)
{
    int argc = 1;
    char executable[] = "lens_gtest_unit";
    char* argv[] = {executable, nullptr};
    QCoreApplication application(argc, argv);

    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    QByteArray requestBytes;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket* socket = server.nextPendingConnection();
        ASSERT_NE(socket, nullptr);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestBytes] {
            requestBytes += socket->readAll();
            const QByteArray body = entityResponse();
            const QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
            socket->write(headers + body);
            socket->disconnectFromHost();
        });
    });

    LlmClient client(Config{QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())),
                            QStringLiteral("test-key"),
                            QStringLiteral("deepseek-flash")});
    client.setChannel(Channel::Entity);
    client.setPreset(QStringLiteral("default"));

    QEventLoop loop;
    QVector<Explanation> results;
    Usage usage;
    QString failure;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(2000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&client, &LlmClient::batchFinished, &loop, [&](QVector<Explanation> received, Usage receivedUsage) {
        results = std::move(received);
        usage = receivedUsage;
        loop.quit();
    });
    QObject::connect(&client, &LlmClient::failed, &loop, [&](QString message) {
        failure = std::move(message);
        loop.quit();
    });

    client.explainWords({QStringLiteral("New York")});
    timeout.start();
    loop.exec();

    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().title, QStringLiteral("New York"));
    EXPECT_TRUE(results.front().ipa.isEmpty());
    EXPECT_EQ(usage.promptTokens, 12);
    EXPECT_EQ(usage.completionTokens, 9);
    EXPECT_TRUE(requestBytes.contains("New York"));
    EXPECT_TRUE(requestBytes.contains("zh"));
}
