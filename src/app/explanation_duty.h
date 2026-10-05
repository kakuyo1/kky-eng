#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "capture_duty.h"
#include "llm/llm_client.h"

/**
 * @file explanation_duty.h
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

namespace lens::app {

class ExplanationDuty final : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Wire explanation behavior to the shared storage boundary and LLM client.
     * @param storage Storage boundary for cache, marks, history and persistence.
     * @param llm Asynchronous explanation client.
     * @param parent QObject owner.
     */
    ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, QObject* parent = nullptr);

    /// @brief Replace the selection whose action bar is currently visible.
    void setSelection(PendingSelection selection);

    /// @brief Route an action-bar answer through copy, cache or the model.
    void runSelectionAction(QString action, QString text);

    /// @brief Record the verdict for the current word bubble.
    void mark(QString lemma, bool learned);

    /// @brief Remove the current explanation surface.
    void dismissBubble();

    /// @brief Remove the current notice surface.
    void dismissNotice();

    /// @return The current explanation payload, or an empty map.
    QVariantMap bubble() const;

    /// @return The current notice payload, or an empty map.
    QVariantMap notice() const;

    /// @return What the tray and QML surfaces show while a request is active.
    QString busyLabel() const;

signals:
    void bubbleChanged();
    void noticeChanged();
    void busyChanged();
    void statsChanged();

private:
    void explain(const PendingSelection& pending);
    void requestExplanations(const QStringList& words);
    void showBubble(const QString& title,
                    const QString& type,
                    const QString& ipa,
                    const QString& en,
                    const QString& zh,
                    const QPoint& anchor);
    void showNotice(const QString& title, const QString& body, const QString& kind);
    QString noticeTitle(const PendingSelection& pending) const;
    void clearBubble();
    void clearNotice();

    StorageDuty& storage_;
    llm::LlmClient& llm_;
    PendingSelection pending_;
    QVariantMap bubble_;
    QVariantMap notice_;
    QString busyLabel_;
};

}
