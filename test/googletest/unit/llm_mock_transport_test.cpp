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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
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

namespace {

/// How long a stub case waits for the client to report back before it gives up.
constexpr int kStubTimeoutMs = 2000;

/// @brief The event-loop plumbing a stub case needs: one QCoreApplication for the test's
///        duration, because Qt allows one at a time and the reply fires from a timer.
struct QtApplication {
    QtApplication()
        : application(argc, argv)
    {}

    int argc = 1;
    char name[20] = "lens_gtest_unit";
    char* argv[2] = {name, nullptr};
    QCoreApplication application;
};

/// @brief A reply that answers from memory, so the client's status, empty-body and
///        transport-failure branches are reachable without a socket.
class StubReply : public QNetworkReply {
public:
    StubReply(int status, QByteArray body, QNetworkReply::NetworkError error)
        : body_(std::move(body))
    {
        // No status attribute at all is how "the transfer never completed" looks to the client:
        // attribute() comes back invalid and toInt() is zero.
        if (status != 0) setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setError(error, error == QNetworkReply::NoError ? QString() : QStringLiteral("stub failure"));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit finished();
        });
    }

    void abort() override
    {}

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (body_.isEmpty()) return -1; // end of the stub body
        const qint64 count = std::min(maxSize, static_cast<qint64>(body_.size()));
        std::memcpy(data, body_.constData(), static_cast<std::size_t>(count));
        body_.remove(0, count);
        return count;
    }

private:
    QByteArray body_;
};

/// @brief A manager whose every request is answered by a StubReply with the given status.
///
/// The posted body is kept so a case can assert on it: the request bytes are written to the
/// device createRequest() is handed.
class StubManager : public QNetworkAccessManager {
public:
    StubManager(int status, QByteArray body, QNetworkReply::NetworkError error = QNetworkReply::NoError)
        : status_(status), body_(std::move(body)), error_(error)
    {}

    QByteArray postedBody;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* device) override
    {
        Q_UNUSED(operation)
        Q_UNUSED(request)
        if (device != nullptr) postedBody = device->readAll();
        auto* reply = new StubReply(status_, body_, error_);
        reply->setParent(this);
        return reply;
    }

private:
    int status_;
    QByteArray body_;
    QNetworkReply::NetworkError error_;
};

Config stubConfig(const QString& key = QStringLiteral("test-key"))
{
    return Config{QUrl(QStringLiteral("http://example.invalid")), key, QStringLiteral("deepseek-flash")};
}

/// @brief Run one explainWords() to completion and return the failure it reported, if any.
QString runToFailure(LlmClient& client, const QStringList& words)
{
    QString failure;
    QEventLoop loop;
    QObject::connect(&client, &LlmClient::failed, &loop, [&](QString message) {
        failure = std::move(message);
        loop.quit();
    });
    QTimer::singleShot(kStubTimeoutMs, &loop, &QEventLoop::quit);
    client.explainWords(words);
    loop.exec();
    return failure;
}

} // namespace

/// A status the service answered with is turned into its own reader-facing message, not the
/// generic transport one. These are the branches the httpErrorFor table exists for.
TEST_F(LlmTest, ReportsANonSuccessStatusWithItsOwnMessage)
{
    QtApplication qt;
    StubManager manager(402, QByteArray());
    LlmClient client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("credit")) << failure.toStdString();
}

/// A transfer that never reached the service has no status code at all; that is a different
/// failure from a service that answered with an error, and the message says so.
TEST_F(LlmTest, ReportsATransferThatNeverReachedTheService)
{
    QtApplication qt;
    StubManager manager(0, QByteArray(), QNetworkReply::ConnectionRefusedError);
    LlmClient client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("could not reach the service")) << failure.toStdString();
}

/// A 200 with an empty body still has to fail the batch: an empty response is not an answer.
TEST_F(LlmTest, RejectsAnEmptyBodyFromASuccessStatus)
{
    QtApplication qt;
    StubManager manager(200, QByteArray());
    LlmClient client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("JSON object")) << failure.toStdString();
}

/// More than the contract's twenty entries are truncated before the request is built, so the
/// model never sees the extra ones.
TEST_F(LlmTest, TruncatesAnOverlongBatchToTheContractCap)
{
    QtApplication qt;
    StubManager manager(402, QByteArray());
    LlmClient client(stubConfig(), &manager);

    QStringList words;
    for (int i = 0; i < 21; ++i)
        words << QStringLiteral("word%1").arg(i, 2, 10, QLatin1Char('0'));

    runToFailure(client, words);

    EXPECT_TRUE(manager.postedBody.contains("word19")) << "the twentieth word was dropped too";
    EXPECT_FALSE(manager.postedBody.contains("word20")) << "the twenty-first word was sent";
}

/// An empty batch is refused before the network is touched -- there is nothing to ask about.
TEST_F(LlmTest, RefusesAnEmptyBatchWithoutPosting)
{
    QtApplication qt;
    StubManager manager(200, QByteArray());
    LlmClient client(stubConfig(), &manager);

    QString failure;
    QObject::connect(&client, &LlmClient::failed, [&](QString message) { failure = message; });
    client.explainWords({});

    EXPECT_TRUE(failure.contains("nothing to look up")) << failure.toStdString();
    EXPECT_TRUE(manager.postedBody.isEmpty());
}

/// No key is the one failure the reader has to fix in settings, and it is caught before the
/// request goes out rather than spending a round trip to be told 401.
TEST_F(LlmTest, RefusesToPostWithoutAnApiKey)
{
    QtApplication qt;
    StubManager manager(200, QByteArray());
    LlmClient client(stubConfig(QString()), &manager);

    QString failure;
    QObject::connect(&client, &LlmClient::failed, [&](QString message) { failure = message; });
    client.explainWords({QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("API key is missing")) << failure.toStdString();
    EXPECT_TRUE(manager.postedBody.isEmpty());
}
