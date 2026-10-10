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

#include "cost_duty.h"
#include "llm/llm_pure.h"
#include "notice.h"
#include "util/log.h"

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
    result.etymology = QString::fromStdString(cached.etymology);
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
                // Named rather than positional: five of these six fields are strings, and the one
                // that must not be dropped is the one that arrived last.
                core::WordCache cached{.ipa         = result.ipa.toStdString(),
                                       .en          = result.en.toStdString(),
                                       .zh          = result.zh.toStdString(),
                                       .translation = result.translation.toStdString(),
                                       .etymology   = result.etymology.toStdString()};
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

    connect(&llm_, &llm::LlmClient::modelsFetched, this, &ExplanationDuty::noteModelsFetched);

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

void ExplanationDuty::reviewWord(QString lemma, QPoint const anchor)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    reviewAnchor_ = anchor;
    const core::CacheContext context{storage_.knownStore().explanationLang(),
                                     storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true")};
    if (const auto cached = storage_.knownStore().cacheGet(value.toStdString(), context)) {
        PendingSelection pending{.anchor = anchor, .kind = QStringLiteral("word"), .surface = value, .lemma = value};
        showBubble(fromCache(*cached, value), pending, context, false);
        reviewRaised_ = true;
        return;
    }
    // The one thing on this path that reaches the model, and it is a button rather than the hover
    // itself: a pointer crossing the list must not spend the reader's money. The two strings are
    // spelled in the controller's context, where this file's other reader-facing ones already are;
    // the context is written out at each call because lupdate reads literal arguments only.
    showNotice(value,
               QCoreApplication::translate("lens::app::AppController", "No explanation is stored for this word in the current language."),
               QString::fromLatin1(kNoticeInfo),
               QCoreApplication::translate("lens::app::AppController", "Explain now"),
               value);
}

void ExplanationDuty::dismissReview()
{
    if (not reviewRaised_) return;
    clearBubble();
}

void ExplanationDuty::explainLemma(QString lemma)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    PendingSelection pending{.anchor = reviewAnchor_, .kind = QStringLiteral("word"), .surface = value, .lemma = value};
    explain(pending);
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
    llm_.setEtymology(requestedBatch_.front().kind == QLatin1String("word") && etymologyEnabled());
    QStringList inputs;
    for (const auto& item : requestedBatch_) {
        inputs << (item.kind == QLatin1String("word") ? item.lemma : item.text);
        if (item.kind == QLatin1String("word"))
            storage_.statsStore().recordPop(item.lemma.toStdString(), nowMinute().toStdString());
    }
    busyLabel_ = QCoreApplication::translate("lens::app::AppController", "Explaining…");
    emit busyChanged();
    emit petEvent(core::pet::PetEvent::ExplanationRequested);
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

void ExplanationDuty::showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool const countsDown)
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
                        {"countsDown", countsDown},
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
        // Read off the setting as the card is drawn rather than as the request was made: turning
        // the setting off puts the origin away on the next bubble, and the entry it came from is
        // left whole for whoever asks for it later.
        if (etymologyEnabled())
            payload.insert(QStringLiteral("etymology"), explanation.etymology);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(explanation.title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    bubble_ = payload;
    // Whatever raised this bubble, the one the pointer's row put up is no longer standing: a scan
    // bubble must not be taken down by the pointer leaving a word the reader has moved on from.
    // The held exit goes with it -- this bubble is the reading now.
    reviewRaised_ = false;
    clearNotice();
    emit bubbleChanged();
    emit petEvent(core::pet::PetEvent::ExplanationShown);
}

void ExplanationDuty::showNotice(QString const& title, QString const& body, QString const& kind, QString const& action, QString const& lemma)
{
    clearBubble();
    notice_ = noticePayload(title, body, kind, action, lemma);
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
    // Every way a bubble goes -- the countdown, the cross, a deletion, a notice replacing it --
    // leaves nothing for the pointer to be holding afterwards.
    reviewRaised_ = false;
    if (bubble_.isEmpty()) return;
    bubble_.clear();
    emit bubbleChanged();
    emit petEvent(core::pet::PetEvent::ExplanationHidden);
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
    emit petEvent(learned ? core::pet::PetEvent::KnownMarked : core::pet::PetEvent::NewWordMarked);
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
    // Whether the reader is actually changing service, read before the document below is written:
    // that is the whole condition the correction is for. Re-picking the provider already in force is
    // a tap on the dropdown row, not a change, and with a shared model list that tap would replace a
    // valid model with whichever id the source happens to list first (docs/adr/0020). A document
    // that has never chosen one counts as a change -- the first pick is one.
    const bool changed   = storage_.documentString("PROVIDER", QString{}) != provider;
    auto& document       = storage_.knownStore().document();
    document["PROVIDER"] = provider.toStdString();
    document["URL"]      = llm_.baseUrl().toString().toStdString();

    // The model follows the service when the service changes (docs/adr/0017, kept by docs/adr/0020
    // only here). A name from another service is the 400 that says "malformed" and nothing else, and
    // the reader changed the service, not the model. A model that this service does list is left
    // alone: switching back and forth between two services must not lose a choice that was still
    // valid.
    const QStringList known = modelsFor(provider);
    if (changed and not known.isEmpty() and not known.contains(llm_.model())) {
        LENS_INFO("model follows the provider: '{}' -> '{}'", llm_.model().toStdString(), known.front().toStdString());
        llm_.setModel(known.front());
        document["MODEL"] = known.front().toStdString();
    }
    rebuildModels();
    storage_.save();
    emit settingsChanged();
    llm_.fetchModels();
    return true;
}

QStringList ExplanationDuty::modelsFor(QString const& provider) const
{
    // What the source listed for this provider last time wins: the catalog's list is a seed for a
    // machine that has never reached it, and the ids a service carries change under the app.
    const nlohmann::json& document = storage_.knownStore().document();
    if (document.contains("MODELS") and document["MODELS"].is_object()) {
        const auto& served = document["MODELS"];
        const auto found   = served.find(provider.toStdString());
        if (found != served.end() and found->is_array()) {
            QStringList ids;
            for (const auto& id : *found)
                if (id.is_string()) ids.append(QString::fromStdString(id.get<std::string>()));
            if (not ids.isEmpty()) return ids;
        }
    }
    QStringList seeded;
    for (const auto& id : llm::serviceProvider(provider).value("models").toArray())
        if (id.isString() and not id.toString().isEmpty()) seeded.append(id.toString());
    return seeded;
}

QString ExplanationDuty::currentProvider() const
{
    return storage_.documentString("PROVIDER", llm::serviceCatalog().value("defaultProvider").toString());
}

void ExplanationDuty::rebuildModels()
{
    QVariantList rebuilt;
    for (const QString& id : modelsFor(currentProvider()))
        rebuilt.append(QVariantMap{{"value", id}, {"label", id}, {"group", ""}, {"note", ""}});
    if (rebuilt == models_) return;
    models_ = std::move(rebuilt);
    emit modelsChanged();
}

void ExplanationDuty::refreshModels()
{
    rebuildModels();
    llm_.fetchModels();
}

void ExplanationDuty::noteModelsFetched(QStringList models)
{
    if (models.isEmpty()) return;
    // One answer, several caches: each provider takes the ids its own vendor prefix names, and a
    // provider with no prefix gets no entry at all.
    auto& document = storage_.knownStore().document();
    int filled     = 0;
    int total      = 0;
    for (const auto& value : llm::serviceCatalog().value("providers").toArray()) {
        const auto provider = value.toObject();
        if (not provider.contains("openRouterPrefix")) continue;
        const QStringList ids = llm::idsForPrefix(models, provider.value("openRouterPrefix").toString());
        if (ids.isEmpty()) continue;
        ++filled;
        total += ids.size();
        nlohmann::json written = nlohmann::json::array();
        for (const QString& id : ids)
            written.push_back(id.toStdString());
        document["MODELS"][provider.value("value").toString().toStdString()] = std::move(written);
    }
    if (filled == 0) return;
    LENS_INFO("the model source lists {} model(s) across {} provider(s)", total, filled);

    // The model in force is not touched here. This answer describes the whole industry, not the
    // one service standing, so "not in the list" says nothing about what that service carries --
    // the correction belongs to the reader changing the service (docs/adr/0020).
    rebuildModels();
    storage_.save();
}

QVariantMap ExplanationDuty::settings() const
{
    auto const& catalog  = llm::serviceCatalog();
    auto const model     = llm_.model();
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
    // The model list is deliberately not in here. It is the one long thing the settings page
    // reads -- hundreds of ids from a service that resells models -- and this map is rebuilt for
    // each of the page's ~40 bindings on every change, so carrying it meant rebuilding hundreds of
    // maps dozens of times per change. It has its own property and its own cache (models()).
    return {{"languages", catalog.value("languages").toArray().toVariantList()},
            {"providers", providers},
            {"model", model},
            {"modelPrice", priceText}};
}

QVariantList ExplanationDuty::models() const
{
    return models_;
}

bool ExplanationDuty::etymologyEnabled() const
{
    return storage_.documentString("etymology", QStringLiteral("false")) == QLatin1String("true");
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
