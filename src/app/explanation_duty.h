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
    void dismissBubble();
    void dismissNotice();
    QVariantMap bubble() const;
    QVariantMap notice() const;
    QString busyLabel() const;
    QVariantMap settings() const;
    bool setProvider(QString const& provider);
    bool setModel(QString const& model);

signals:
    void bubbleChanged();
    void noticeChanged();
    void busyChanged();
    void statsChanged();

private:
    void explain(PendingSelection const& pending);
    void requestExplanations(QVector<PendingSelection> batch, core::CacheContext context, bool fromScan);
    void startNext();
    void showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context);
    void showNotice(QString const& title, QString const& body, QString const& kind);
    QString noticeTitle(PendingSelection const& pending) const;
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
    bool requestedFromScan_       = false;
    bool inFlight_                = false;
    bool handlingResponse_        = false;
    unsigned selectionGeneration_ = 0;
    unsigned requestedGeneration_ = 0;
    QSet<QString> invalidatedWords_;
};

}
