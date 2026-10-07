#pragma once

#include <QObject>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <optional>

#include "capture_duty.h"
#include "llm/llm_client.h"

/**
 * @file explanation_duty.h
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

namespace lens::app {

class CostDuty;

class ExplanationDuty final : public QObject {
    Q_OBJECT
public:
    ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, QObject* parent = nullptr);
    void setSelection(PendingSelection selection);
    /// @brief Admit locally filtered scan candidates; cached words are consumed locally first.
    void enqueueScanCandidates(QVariantList candidates, QPoint anchor);
    /// @brief Retry queued scan work after a budget rollover or a raised cap.
    void resumeQueued();
    /// @brief Invalidate queued or in-flight results for a word deleted locally.
    void invalidateWord(QString const& lemma);
    void runSelectionAction(QString action, QString text);
    void mark(QString lemma, bool learned);
    /**
     * @brief Show what is already stored for a word the reader pointed at, and nothing else.
     *
     * No request is made on this path: a pointer crossing the word list is not a request to spend
     * money, and the stored copy is what the reader came to see again. A word with nothing stored in
     * the current explanation language gets a notice saying so, with the one button that does ask
     * the model, so the reader chooses to spend rather than hovering their way into it.
     *
     * @param lemma  The word, as the row draws it.
     * @param anchor Where the row is, in the physical pixels the other surfaces anchor with.
     */
    void reviewWord(QString lemma, QPoint anchor);
    /// @brief Take down a bubble raised by reviewWord(); one raised anywhere else is left alone.
    ///
    /// The row decides this and only the row does. The bubble's own hover state is not read here on
    /// purpose: it flaps (TODO.md, "定位气泡 hover 抖动"), and hanging this bubble's life off a
    /// signal that flaps is what made it appear and disappear while the pointer stood still. The
    /// word the pointer is on is the one thing that is not in doubt.
    void dismissReview();
    /// @brief Ask the service in force for its model list, and remember who was asked.
    void refreshModels();
    /// @brief Ask the model about a lemma the reader asked for by name, anchored where it was read.
    void explainLemma(QString lemma);
    void dismissBubble();
    void dismissNotice();
    /**
     * @brief Put a reason card up where no explanation can be.
     * @param title  What the notice is about.
     * @param body   Reader-facing reason, already translated.
     * @param kind   kNoticeInfo or kNoticeError.
     * @param action Label for the one thing the reader can do about it, empty for none.
     * @param lemma  The word that action would explain, when there is one.
     */
    void showNotice(QString const& title, QString const& body, QString const& kind, QString const& action = {}, QString const& lemma = {});
    QVariantMap bubble() const;
    QVariantMap notice() const;
    QString busyLabel() const;
    QVariantMap settings() const;
    /**
     * @brief The models the provider in force carries, as {value,label,group,note}.
     *
     * Read off a cache rebuilt where the answer can change rather than built on the read: this is
     * the settings page's most-read list and its longest by far -- a service that resells other
     * people's models answers with several hundred ids -- and the map per id that `settings()`
     * used to carry was rebuilt for every one of the settings page's bindings, on every change.
     */
    QVariantList models() const;
    /// @brief Point the client at a catalog service.
    ///
    /// The model follows the service (docs/adr/0017): a model name from another one is a 400 whose
    /// message names nothing, and the service is the only thing the reader meant to choose. The
    /// list the service itself gave last time wins over the catalog's, and asking for a fresh one
    /// happens here too -- it costs nothing and the answer is the only current list there is.
    /// @return False for an unknown provider.
    bool setProvider(QString const& provider);
    bool setModel(QString const& model);

signals:
    void bubbleChanged();
    /// @brief Something the settings page reads has changed: the service or the model.
    void settingsChanged();
    /// @brief The provider in force carries a different list of models than it did.
    ///
    /// Its own signal because it is a separate property: the list is the one thing here big enough
    /// that rebuilding it for every reader of settings() is what made a provider change feel slow.
    void modelsChanged();
    void noticeChanged();
    void busyChanged();
    void statsChanged();

private:
    void explain(PendingSelection const& pending);
    void requestExplanations(QVector<PendingSelection> batch, core::CacheContext context, bool fromScan);
    void startNext();
    /// @param countsDown Whether the bubble takes itself down after its countdown. A bubble the
    ///        word list raises belongs to the row under the pointer instead, and is taken down when
    ///        the pointer leaves it.
    void showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool countsDown = true);
    QString noticeTitle(PendingSelection const& pending) const;
    /// @return The model list known for @p provider: what the service said last time, else the
    ///         catalog's own seed, else empty -- a custom endpoint names no models at all.
    QStringList modelsFor(QString const& provider) const;
    /// @brief Keep the list the service just answered with, and use it for the provider on show.
    void noteModelsFetched(QStringList models);
    /// @brief Rebuild the cached model list from the provider in force, and say so if it moved.
    void rebuildModels();
    /// @return The provider in force, from the document.
    QString currentProvider() const;
    void clearBubble();
    void clearNotice();

    StorageDuty& storage_;
    llm::LlmClient& llm_;
    CostDuty& cost_;
    PendingSelection pending_;
    QVariantMap bubble_;
    QVariantMap notice_;
    QString busyLabel_;
    QVector<PendingSelection> requestedBatch_;
    core::CacheContext requestedContext_;
    QVector<PendingSelection> scanQueue_;
    std::optional<PendingSelection> queuedManual_;
    QPoint scanAnchor_;
    /// Where the row the reader is pointing at sits, so the explanation a notice's button asks for
    /// lands in the same place the review bubble did.
    QPoint reviewAnchor_;
    /// Whether the bubble standing is the one reviewWord() raised. Only that one is the pointer's
    /// to take down: a scan or a selection bubble outlives the pointer leaving a word-list row.
    bool reviewRaised_ = false;
    /// The provider an outstanding model-list request was made for. The answer names no service,
    /// so without this one arriving after the reader switched providers would be filed under the
    /// wrong one, and would replace a model belonging to neither.
    QString listingFor_;
    /// The provider in force's models, in the shape the settings page draws.
    QVariantList models_;
    bool requestedFromScan_       = false;
    bool inFlight_                = false;
    bool handlingResponse_        = false;
    unsigned selectionGeneration_ = 0;
    unsigned requestedGeneration_ = 0;
    QSet<QString> invalidatedWords_;
};

}
