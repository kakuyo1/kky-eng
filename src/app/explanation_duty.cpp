/**
 * @file explanation_duty.cpp
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

#include "explanation_duty.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>

#include <algorithm>
#include <utility>

#include "core/log.h"
#include "cost_duty.h"
#include "notice.h"

namespace lens::app {
namespace {

constexpr int kMaximumScanBatch = 5;

QString nowMinute()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

llm::Explanation fromCache(core::WordCache const& cached, QString const& lemma)
{
    llm::Explanation result{lemma,
                            QString::fromStdString(cached.ipa),
                            QString::fromStdString(cached.en),
                            QString::fromStdString(cached.zh),
                            QString::fromStdString(cached.translation)};
    for (auto const& sense : cached.senses)
        result.senses.push_back({QString::fromStdString(sense.en), QString::fromStdString(sense.zh), QString::fromStdString(sense.translation)});
    return result;
}

}

ExplanationDuty::ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, QObject* parent)
    : QObject(parent), storage_(storage), llm_(llm), cost_(cost)
{
    connect(&llm_, &llm::LlmClient::batchFinished, this, [this](QVector<llm::Explanation> results, llm::Usage usage) {
        if (not inFlight_) return;
        inFlight_         = false;
        handlingResponse_ = true;
        busyLabel_.clear();
        emit busyChanged();
        cost_.recordUsage(nowMinute().toStdString(), usage);

        if (results.isEmpty()) {
            LENS_WARN("the model returned no explanation");
            storage_.save();
            emit statsChanged();
            handlingResponse_ = false;
            startNext();
            return;
        }

        for (const auto& result : results) {
            auto selection = std::find_if(requestedBatch_.cbegin(), requestedBatch_.cend(), [&](PendingSelection const& candidate) {
                return requestedFromScan_ || candidate.kind != QLatin1String("word")
                           ? (candidate.kind == QLatin1String("word")
                                  ? QString::compare(result.title, candidate.lemma, Qt::CaseInsensitive) == 0
                                  : candidate.text == requestedBatch_.front().text)
                           : QString::compare(result.title, candidate.lemma, Qt::CaseInsensitive) == 0;
            });
            if (selection == requestedBatch_.cend()) continue;
            if (invalidatedWords_.contains(selection->lemma)) continue;
            if (selection->kind == QLatin1String("word")) {
                core::WordCache cached{result.ipa.toStdString(), result.en.toStdString(), result.zh.toStdString(), result.translation.toStdString()};
                for (auto const& sense : result.senses)
                    cached.senses.push_back({sense.en.toStdString(), sense.zh.toStdString(), sense.translation.toStdString()});
                storage_.knownStore().cachePut(selection->lemma.toStdString(), std::move(cached), requestedContext_);
            }
        }

        if (requestedGeneration_ == selectionGeneration_ &&
            requestedContext_.language == storage_.knownStore().explanationLang() &&
            requestedContext_.multipleSenses == (requestedBatch_.front().kind == QLatin1String("word") &&
                                                 storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true"))) {
            const auto& first = results.front();
            if (requestedFromScan_) {
                const auto item = std::find_if(requestedBatch_.cbegin(), requestedBatch_.cend(), [&](PendingSelection const& candidate) {
                    return QString::compare(first.title, candidate.lemma, Qt::CaseInsensitive) == 0;
                });
                if (item != requestedBatch_.cend()) showBubble(first, *item, requestedContext_);
            } else {
                showBubble(first, requestedBatch_.front(), requestedContext_);
            }
        }
        storage_.save();
        emit statsChanged();
        for (auto const& selection : requestedBatch_)
            invalidatedWords_.remove(selection.lemma);
        handlingResponse_ = false;
        startNext();
    });

    connect(&llm_, &llm::LlmClient::failed, this, [this](QString const& message) {
        if (not inFlight_) return;
        inFlight_ = false;
        busyLabel_.clear();
        emit busyChanged();
        if (not requestedFromScan_ && requestedGeneration_ == selectionGeneration_)
            showNotice(noticeTitle(requestedBatch_.front()), message, kNoticeError);
        startNext();
    });
}

void ExplanationDuty::setSelection(PendingSelection selection)
{
    ++selectionGeneration_;
    pending_ = std::move(selection);
}

void ExplanationDuty::invalidateWord(QString const& lemma)
{
    const auto value = lemma.trimmed();
    if (value.isEmpty())
        return;
    invalidatedWords_.insert(value);
    scanQueue_.erase(std::remove_if(scanQueue_.begin(), scanQueue_.end(), [&](PendingSelection const& item) {
                         return QString::compare(item.lemma, value, Qt::CaseInsensitive) == 0;
                     }),
                     scanQueue_.end());
    if (queuedManual_ && QString::compare(queuedManual_->lemma, value, Qt::CaseInsensitive) == 0)
        queuedManual_.reset();
    if (QString::compare(bubble_.value("title").toString(), value, Qt::CaseInsensitive) == 0)
        clearBubble();
    ++selectionGeneration_;
}

void ExplanationDuty::enqueueScanCandidates(QVariantList candidates, QPoint anchor)
{
    scanAnchor_ = anchor;
    for (const auto& value : candidates) {
        const auto map = value.toMap();
        if (map.value("kind").toString() != QLatin1String("word")) continue;
        PendingSelection candidate{.anchor  = anchor,
                                   .kind    = QStringLiteral("word"),
                                   .surface = map.value("surface").toString(),
                                   .lemma   = map.value("lemma").toString()};
        if (candidate.lemma.isEmpty()) continue;
        const core::CacheContext context{storage_.knownStore().explanationLang(),
                                         storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true")};
        if (const auto cached = storage_.knownStore().cacheGet(candidate.lemma.toStdString(), context)) {
            storage_.statsStore().recordPop(candidate.lemma.toStdString(), nowMinute().toStdString());
            if (bubble_.isEmpty() && not inFlight_)
                showBubble(fromCache(*cached, candidate.lemma), candidate, context);
            continue;
        }
        if (std::none_of(scanQueue_.cbegin(), scanQueue_.cend(), [&](PendingSelection const& queued) {
                return queued.lemma == candidate.lemma;
            }))
            scanQueue_.push_back(std::move(candidate));
    }
    storage_.save();
    emit statsChanged();
    startNext();
}

void ExplanationDuty::resumeQueued()
{
    if (handlingResponse_)
        return;
    startNext();
}

void ExplanationDuty::runSelectionAction(QString action, QString text)
{
    if (pending_.text.isEmpty()) {
        LENS_WARN("action '{}' arrived with no selection pending", action.toStdString());
        return;
    }
    if (action == QLatin1String("copy")) {
        QGuiApplication::clipboard()->setText(pending_.text);
        pending_ = PendingSelection{};
        ++selectionGeneration_;
        return;
    }
    if (action != QLatin1String("translate") && action != QLatin1String("explain")) return;
    if (text != pending_.text)
        LENS_WARN("selection action text differed from the captured selection; using the captured text");
    if (pending_.kind == QLatin1String("sentence")) pending_.preset = action;
    if (inFlight_) {
        queuedManual_ = pending_;
        return;
    }
    explain(pending_);
}

void ExplanationDuty::explain(PendingSelection const& pending)
{
    if (inFlight_) {
        queuedManual_ = pending;
        return;
    }
    const core::CacheContext context{storage_.knownStore().explanationLang(),
                                     pending.kind == QLatin1String("word") && storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true")};
    if (pending.kind == QLatin1String("word")) {
        if (const auto cached = storage_.knownStore().cacheGet(pending.lemma.toStdString(), context)) {
            storage_.statsStore().recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
            storage_.save();
            showBubble(fromCache(*cached, pending.lemma), pending, context);
            emit statsChanged();
            return;
        }
    }
    if (not cost_.canRequest()) return;
    requestExplanations({pending}, context, false);
}

void ExplanationDuty::requestExplanations(QVector<PendingSelection> batch, core::CacheContext context, bool const fromScan)
{
    if (batch.isEmpty() || not cost_.canRequest()) return;
    for (auto const& item : batch)
        invalidatedWords_.remove(item.lemma);
    requestedBatch_      = std::move(batch);
    requestedContext_    = std::move(context);
    requestedFromScan_   = fromScan;
    requestedGeneration_ = selectionGeneration_;
    inFlight_            = true;
    const auto channel   = requestedBatch_.front().kind == QLatin1String("entity")     ? llm::Channel::Entity
                           : requestedBatch_.front().kind == QLatin1String("sentence") ? llm::Channel::Sentence
                                                                                       : llm::Channel::Word;
    llm_.setChannel(channel);
    llm_.setExplanationLang(QString::fromStdString(requestedContext_.language));
    llm_.setPreset(requestedContext_.multipleSenses ? QStringLiteral("multiple") : requestedBatch_.front().preset);
    QStringList inputs;
    for (const auto& item : requestedBatch_) {
        inputs << (item.kind == QLatin1String("word") ? item.lemma : item.text);
        if (item.kind == QLatin1String("word"))
            storage_.statsStore().recordPop(item.lemma.toStdString(), nowMinute().toStdString());
    }
    busyLabel_ = QCoreApplication::translate("lens::app::AppController", "Explaining…");
    emit busyChanged();
    storage_.save();
    emit statsChanged();
    llm_.explainWords(std::move(inputs));
}

void ExplanationDuty::startNext()
{
    if (inFlight_ || not cost_.canRequest()) return;
    if (queuedManual_) {
        auto next = std::move(*queuedManual_);
        queuedManual_.reset();
        explain(next);
        return;
    }
    if (scanQueue_.isEmpty()) return;
    QVector<PendingSelection> batch;
    while (not scanQueue_.isEmpty() && batch.size() < kMaximumScanBatch) {
        batch.push_back(std::move(scanQueue_.front()));
        scanQueue_.removeFirst();
    }
    requestExplanations(std::move(batch),
                        {storage_.knownStore().explanationLang(),
                         storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true")},
                        true);
}

void ExplanationDuty::showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context)
{
    auto const selected = [&](QString const& en, QString const& zh, QString const& translation) {
        return context.language == "en" ? en : context.language == "zh" ? zh
                                                                        : translation;
    };
    QVariantMap payload{{"title", explanation.title},
                        {"type", selection.kind},
                        {"en", context.language == "en" ? explanation.en : QString{}},
                        {"zh", context.language == "zh" ? explanation.zh : QString{}},
                        {"translation", context.language != "en" && context.language != "zh" ? explanation.translation : QString{}},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    if (selection.kind == QLatin1String("word")) {
        QVariantList senses;
        auto entries = explanation.senses;
        if (entries.isEmpty()) entries.push_back({explanation.en, explanation.zh, explanation.translation});
        for (const auto& sense : entries) {
            if (senses.size() >= (context.multipleSenses ? llm::kMaxSenses : 1)) break;
            senses.append(QVariantMap{{"en", context.multipleSenses || context.language == "en" ? sense.en : QString{}},
                                      {"text", context.multipleSenses ? (context.language == "en" ? sense.zh : selected(sense.en, sense.zh, sense.translation)) : (context.language == "en" ? QString{} : selected(sense.en, sense.zh, sense.translation))}});
        }
        payload.insert(QStringLiteral("senses"), senses);
        payload.insert(QStringLiteral("ipa"), explanation.ipa);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(explanation.title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    bubble_ = payload;
    clearNotice();
    emit bubbleChanged();
}

void ExplanationDuty::showNotice(QString const& title, QString const& body, QString const& kind)
{
    clearBubble();
    notice_ = noticePayload(title, body, kind);
    emit noticeChanged();
}

QString ExplanationDuty::noticeTitle(PendingSelection const& pending) const
{
    return pending.kind == QLatin1String("sentence") ? QCoreApplication::translate("lens::app::AppController", "Request failed")
                                                     : (pending.surface.isEmpty() ? pending.text : pending.surface);
}

void ExplanationDuty::clearNotice()
{
    if (notice_.isEmpty()) return;
    notice_.clear();
    emit noticeChanged();
}

void ExplanationDuty::clearBubble()
{
    if (bubble_.isEmpty()) return;
    bubble_.clear();
    emit bubbleChanged();
}

void ExplanationDuty::mark(QString lemma, bool learned)
{
    if (!bubble_.isEmpty() && bubble_.value("type").toString() != QLatin1String("word") && bubble_.value("title").toString() == lemma)
        return;
    const std::string key = lemma.toStdString();
    storage_.knownStore().mark(key, learned);
    storage_.statsStore().recordVerdict(key, nowMinute().toStdString(), learned ? "known" : "new");
    storage_.save();
    if (bubble_.value("title").toString() == lemma) bubble_["status"] = learned ? QStringLiteral("known") : QStringLiteral("new");
    emit bubbleChanged();
    emit statsChanged();
}

void ExplanationDuty::dismissBubble()
{
    clearBubble();
}

void ExplanationDuty::dismissNotice()
{
    clearNotice();
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

bool ExplanationDuty::setProvider(QString const& provider)
{
    auto const entry = llm::serviceProvider(provider);
    if (entry.isEmpty() || not llm_.setProvider(provider)) return false;
    auto& document       = storage_.knownStore().document();
    document["PROVIDER"] = provider.toStdString();
    document["URL"]      = llm_.baseUrl().toString().toStdString();
    document["MODEL"]    = llm_.model().toStdString();
    storage_.save();
    return true;
}

QVariantMap ExplanationDuty::settings() const
{
    auto const& catalog = llm::serviceCatalog();
    auto const provider = storage_.documentString("PROVIDER", catalog.value("defaultProvider").toString());
    auto const entry    = llm::serviceProvider(provider);
    auto const model    = llm_.model();
    QVariantList models;
    for (auto const& value : entry.value("models").toArray())
        models.append(QVariantMap{{"value", value.toString()}, {"label", value.toString()}, {"group", QString{}}, {"note", QString{}}});
    if (not entry.value("models").toArray().contains(model) and not model.isEmpty())
        models.append(QVariantMap{{"value", model}, {"label", model}, {"group", QString{}}, {"note", QString{}}});
    auto const pricing   = catalog.value("pricing").toObject();
    auto const price     = pricing.value("models").toObject().value(model).toObject();
    auto const priceText = price.isEmpty() ? tr("No listed price; recorded cost is zero.")
                                           : tr("Input %1 / Output %2 %3 per %4 tokens").arg(price.value("input").toDouble()).arg(price.value("output").toDouble()).arg(pricing.value("currency").toString()).arg(pricing.value("unit").toInteger());
    QVariantList providers;
    for (auto const& value : catalog.value("providers").toArray()) {
        auto option      = value.toObject().toVariantMap();
        auto const group = option.value("group").toString();
        option["group"]  = group == QLatin1String("Domestic")        ? QCoreApplication::translate("SettingsPopup", "Domestic")
                           : group == QLatin1String("International") ? QCoreApplication::translate("SettingsPopup", "International")
                                                                     : QCoreApplication::translate("SettingsPopup", "Other");
        if (option.value("value").toString() == QLatin1String("custom")) option["label"] = QCoreApplication::translate("SettingsPopup", "Custom service");
        providers.append(option);
    }
    return {{"languages", catalog.value("languages").toArray().toVariantList()}, {"providers", providers}, {"models", models}, {"modelPrice", priceText}, {"providerDefaultUrl", entry.value("baseUrl").toString()}};
}

bool ExplanationDuty::setModel(QString const& model)
{
    const auto value = model.trimmed();
    if (value.isEmpty()) return false;
    llm_.setModel(value);
    storage_.writeDocument("MODEL", value);
    return true;
}

}
