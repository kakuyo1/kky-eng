#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "capture/ocr_engine.h"
#include "capture/scan_pipeline.h"
#include "selection_text_grabber.h"
#include "storage_duty.h"

namespace lens::app {

class MouseSelectionHook;

/**
 * @file capture_duty.h
 * @brief Selection capture and local OCR acquisition, independent of explanation presentation.
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
    CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent = nullptr);
    CaptureDuty(StorageDuty& storage,
                MouseSelectionHook& hook,
                std::unique_ptr<capture::OcrEngine> ocr,
                QObject* parent = nullptr);
    ~CaptureDuty() override;

    QVariantMap settings() const;
    QString ocrStatus() const;
    Q_INVOKABLE void setOcrCapture(bool on);
    Q_INVOKABLE void setAutoScan(bool on);
    Q_INVOKABLE void setMinimumWordLength(int length);
    Q_INVOKABLE void setDragSensitivity(QString sensitivity);
    Q_INVOKABLE bool setScanWhitelist(QString processes);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void probeOcr();

    /// @brief Pause automatic scanning while a request is in flight.
    void setScanPaused(bool paused);
    /// @brief Pause every acquisition path when the daily budget is exhausted.
    void setBudgetPaused(bool paused);
    /// @return False when OCR is disabled, unavailable, busy, or budget-paused.
    bool captureScreenshot(QImage image, QPoint anchor);
    /// @brief Capture a reader-selected logical region wholly inside one screen.
    Q_INVOKABLE bool captureRegion(QRect region);
    /// @brief Handle a completed selection gesture.
    void onSelectionReleased(QPoint anchor);

signals:
    void selectionBarRequested(QVariantMap payload);
    void selectionReady(PendingSelection selection);
    void pointerPressed(QPoint at);
    void settingsChanged();
    /// @brief Only filtered candidates, never an OCR snapshot, leave this duty.
    void scanCandidatesReady(QVariantList candidates, QPoint anchor);
    /// @brief Count-only report for entity-shaped text unsupported by the scan channel.
    void scanEntitiesUnsupported(int count);
    /// @brief Fixed OCR status without paths, stderr, or captured text.
    void ocrFailed(QString status);

private:
    struct OcrJob {
        QPoint anchor;
        QString context;
        std::uint64_t generation = 0;
        bool probe               = false;
    };
    struct OcrJobState {
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        std::mutex mutex;
        capture::OcrResult result;
        bool done = false;
    };

    void beginSelection(QPoint anchor);
    void classifyText(QString text, QPoint anchor);
    void scanTick();
    void pollOcr();
    void launchOcr(QImage image, QPoint anchor, QString context);
    void invalidateScan();
    void publishBatch(capture::ScanTime now, QPoint anchor);
    std::size_t minFreqRank() const;

    StorageDuty& storage_;
    MouseSelectionHook& hook_;
    std::unique_ptr<SelectionTextGrabber> grabber_;
    std::shared_ptr<capture::OcrEngine> ocr_;
    std::unique_ptr<capture::ScanPipeline> scan_;
    QTimer scanTimer_;
    QTimer pollTimer_;
    std::shared_ptr<OcrJobState> job_;
    std::optional<OcrJob> jobContext_;
    capture::OcrStatus ocrState_        = capture::OcrStatus::RuntimeMissing;
    std::uint64_t generation_           = 0;
    std::uint64_t screenshotGeneration_ = 0;
    QString scanContext_;
    QPoint scanAnchor_;
    bool scanPaused_   = false;
    bool budgetPaused_ = false;
};

}
