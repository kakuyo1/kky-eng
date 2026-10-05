#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVariantMap>

#include <memory>

#include "selection_text_grabber.h"
#include "storage_duty.h"

class QJSEngine;

namespace lens::app {

class MouseSelectionHook;

/**
 * @file capture_duty.h
 * @brief Selection capture and classification, independent of explanation and presentation.
 */

struct PendingSelection {
    QPoint anchor;
    QString text;
    QString kind;
    QString surface;
    QString lemma;
    QString preset = QStringLiteral("default");
};

class CaptureDuty final : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Wire selection capture to the shared storage boundary and mouse hook.
     * @param storage Storage boundary used for capture policy and vocabulary level.
     * @param hook The desktop selection hook.
     * @param parent QObject owner.
     */
    CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent = nullptr);

    /// @brief Handle a completed selection gesture.
    void onSelectionReleased(QPoint anchor);

signals:
    /// @brief Request the action bar for a classified selection.
    void selectionBarRequested(QVariantMap payload);

    /// @brief Deliver the classified selection to the explanation duty.
    void selectionReady(PendingSelection selection);

    /// @brief Forward a desktop pointer press to the QML surface owner.
    void pointerPressed(QPoint at);

private:
    void beginSelection(QPoint anchor);
    std::size_t minFreqRank() const;

    StorageDuty& storage_;
    MouseSelectionHook& hook_;
    std::unique_ptr<SelectionTextGrabber> grabber_;
};

}
