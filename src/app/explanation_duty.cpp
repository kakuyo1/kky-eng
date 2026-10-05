/**
 * @file explanation_duty.cpp
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

#include "explanation_duty.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>

#include <utility>

#include "core/log.h"
#include "notice.h"

namespace lens::app {
namespace {

QString nowMinute()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

}

ExplanationDuty::ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, QObject* parent)
    : QObject(parent), storage_(storage), llm_(llm)
{
    connect(&llm_, &llm::LlmClient::batchFinished, this, [this](const QVector<llm::Explanation>& results, llm::Usage usage) {
        busyLabel_.clear();
        emit busyChanged();

        storage_.statsStore().recordUsage(nowMinute().toStdString(), usage.promptTokens, usage.completionTokens);
        if (results.isEmpty()) {
            LENS_WARN("the model returned no explanation");
            storage_.save();
            emit statsChanged();
            return;
        }

        const llm::Explanation& first = results.front();
        const bool isWord             = pending_.kind == QLatin1String("word");
        const bool matches            = isWord ? (QString::compare(first.title, pending_.lemma, Qt::CaseInsensitive) == 0)
                                               : (first.title == pending_.text);
        if (!matches) {
            LENS_DEBUG("dropping a stale response for '{}'", first.title.toStdString());
            storage_.save();
            emit statsChanged();
            return;
        }
        if (isWord)
            storage_.knownStore().cachePut(pending_.lemma.toStdString(), {first.ipa.toStdString(), first.en.toStdString(), first.zh.toStdString()});
        storage_.save();
        showBubble(first.title,
                   pending_.kind,
                   isWord ? first.ipa : QString(),
                   first.en,
                   first.zh,
                   pending_.anchor);
        emit statsChanged();
    });

    connect(&llm_, &llm::LlmClient::failed, this, [this](const QString& message) {
        busyLabel_.clear();
        emit busyChanged();
        showNotice(noticeTitle(pending_), message, kNoticeError);
    });
}

void ExplanationDuty::setSelection(PendingSelection selection)
{
    pending_ = std::move(selection);
}

void ExplanationDuty::runSelectionAction(QString action, QString text)
{
    if (pending_.text.isEmpty()) {
        LENS_WARN("action '{}' arrived with no selection pending", action.toStdString());
        return;
    }
    if (text != pending_.text) {
        LENS_WARN("the surface echoed {} character(s) back, the selection held {}; using the selection",
                  text.size(),
                  pending_.text.size());
    }

    if (action == QLatin1String("copy")) {
        QGuiApplication::clipboard()->setText(pending_.text);
        LENS_INFO("selection copied back to the clipboard ({} character(s))", pending_.text.size());
        pending_ = PendingSelection{};
        return;
    }
    if (action != QLatin1String("translate") && action != QLatin1String("explain")) {
        LENS_WARN("unknown action '{}'", action.toStdString());
        return;
    }
    if (pending_.kind == QLatin1String("sentence"))
        pending_.preset = action;
    explain(pending_);
}

void ExplanationDuty::explain(const PendingSelection& pending)
{
    if (pending.kind == QLatin1String("word")) {
        if (const auto cached = storage_.knownStore().cacheGet(pending.lemma.toStdString())) {
            LENS_INFO("cache hit for '{}'; nothing was sent", pending.lemma.toStdString());
            storage_.statsStore().recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
            storage_.save();
            showBubble(pending.lemma,
                       pending.kind,
                       QString::fromStdString(cached->ipa),
                       QString::fromStdString(cached->en),
                       QString::fromStdString(cached->zh),
                       pending.anchor);
            emit statsChanged();
            return;
        }
    }

    const llm::Channel channel = pending.kind == QLatin1String("entity")     ? llm::Channel::Entity
                                 : pending.kind == QLatin1String("sentence") ? llm::Channel::Sentence
                                                                             : llm::Channel::Word;
    llm_.setChannel(channel);
    llm_.setPreset(pending.preset);
    requestExplanations({pending.kind == QLatin1String("word") ? pending.lemma : pending.text});
}

void ExplanationDuty::requestExplanations(const QStringList& words)
{
    busyLabel_ = QCoreApplication::translate("lens::app::AppController", "Explaining…");
    emit busyChanged();
    if (pending_.kind == QLatin1String("word"))
        storage_.statsStore().recordPop(pending_.lemma.toStdString(), nowMinute().toStdString());
    storage_.save();
    emit statsChanged();
    llm_.explainWords(words);
}

void ExplanationDuty::showBubble(const QString& title,
                                 const QString& type,
                                 const QString& ipa,
                                 const QString& en,
                                 const QString& zh,
                                 const QPoint& anchor)
{
    const bool wantsChinese = storage_.knownStore().explanationLang() == "zh";
    QVariantMap payload{{"title", title},
                        {"type", type},
                        {"en", wantsChinese ? QString() : en},
                        {"zh", wantsChinese ? zh : QString()},
                        {"x", anchor.x()},
                        {"y", anchor.y()}};
    if (type == QLatin1String("word")) {
        payload.insert(QStringLiteral("ipa"), ipa);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    bubble_ = payload;
    clearNotice();
    LENS_INFO("bubble up for '{}' on '{}' channel", title.toStdString(), type.toStdString());
    emit bubbleChanged();
}

void ExplanationDuty::showNotice(const QString& title, const QString& body, const QString& kind)
{
    clearBubble();
    notice_ = noticePayload(title, body, kind);
    LENS_INFO("notice ({}): '{}'", kind.toStdString(), title.toStdString());
    emit noticeChanged();
}

QString ExplanationDuty::noticeTitle(const PendingSelection& pending) const
{
    if (pending.kind == QLatin1String("sentence"))
        return QCoreApplication::translate("lens::app::AppController", "Request failed");
    return pending.surface.isEmpty() ? pending.text : pending.surface;
}

void ExplanationDuty::clearNotice()
{
    if (notice_.isEmpty())
        return;
    notice_.clear();
    emit noticeChanged();
}

void ExplanationDuty::clearBubble()
{
    if (bubble_.isEmpty())
        return;
    bubble_.clear();
    emit bubbleChanged();
}

void ExplanationDuty::mark(QString lemma, bool learned)
{
    const std::string key = lemma.toStdString();
    if (!bubble_.isEmpty() && bubble_.value("type").toString() != QLatin1String("word") &&
        bubble_.value("title").toString() == lemma) {
        LENS_WARN("ignored a verdict for a non-word bubble title");
        return;
    }
    storage_.knownStore().mark(key, learned);
    storage_.statsStore().recordVerdict(key, nowMinute().toStdString(), learned ? "known" : "new");
    storage_.save();
    LENS_INFO("'{}' marked as {}", key, learned ? "known" : "a new word");
    if (bubble_.value("title").toString() == lemma)
        bubble_["status"] = learned ? QStringLiteral("known") : QStringLiteral("new");
    emit bubbleChanged();
    emit statsChanged();
}

void ExplanationDuty::dismissBubble()
{
    if (bubble_.isEmpty())
        return;
    clearBubble();
    LENS_DEBUG("the current explanation was dismissed");
}

void ExplanationDuty::dismissNotice()
{
    if (notice_.isEmpty())
        return;
    clearNotice();
    LENS_DEBUG("the current notice was dismissed");
}

QVariantMap ExplanationDuty::bubble() const
{
    return bubble_;
}

QVariantMap ExplanationDuty::notice() const
{
    return notice_;
}

QString ExplanationDuty::busyLabel() const
{
    return busyLabel_;
}

}
