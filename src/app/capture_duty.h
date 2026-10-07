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
    /// @brief Point OCR at the reader's own Tesseract and probe it again; empty means the bundled one.
    Q_INVOKABLE void setTesseractExecutable(QString executable);
    /// @brief Point OCR at the reader's own traineddata directory; empty means the one beside the executable.
    Q_INVOKABLE void setTesseractDataDirectory(QString directory);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void probeOcr();

    /// @brief Pause automatic scanning while a request is in flight.
    void setScanPaused(bool paused);
    /// @brief Pause every acquisition path when the daily budget is exhausted.
    void setBudgetPaused(bool paused);
    /// @return False when OCR is disabled, unavailable, busy, or budget-paused.
    bool captureScreenshot(QImage image, QPoint anchor);
    /// @brief Capture a reader-selected logical region wholly inside one screen.
    /// @param region The dragged rectangle, in the device-independent desktop coordinates
    ///               QScreen geometry uses.
    /// @return Empty once the capture is under way; otherwise the reason it is not, which is what
    ///         the surface that asked reports to the reader. A trigger that does nothing at all is
    ///         the one outcome that cannot be told from a broken one.
    Q_INVOKABLE QString captureRegion(QRect region);

    /// @brief Why a capture cannot start this moment, before any pixels are read.
    /// @return Empty when it can, else one of: "screenshot-off" (the OCR capture switch is off),
    ///         "budget-paused", "busy" (a recognition is in flight), "checking" (the runtime is
    ///         still being probed), or the OCR status from ocrStatus() when the runtime is no good.
    /// @note The one gate both the screenshot and the region path go through, so a reader who is
    ///       told why hears the same reason whichever of the two asked.
    QString captureRefusalReason() const;
    /// @brief Handle a completed selection gesture.
    void onSelectionReleased(QPoint anchor);

signals:
    void selectionBarRequested(QVariantMap payload);
    /// @brief Text that arrived with its action already chosen -- the screenshot path, which asks
    ///        for the translation itself rather than for the bar.
    void selectionActionRequested(QString action, QString text);
    void selectionReady(PendingSelection selection);
    void pointerPressed(QPoint at);
    void settingsChanged();
    /// @brief Only filtered candidates, never an OCR snapshot, leave this duty.
    void scanCandidatesReady(QVariantList candidates, QPoint anchor);
    /// @brief Count-only report for entity-shaped text unsupported by the scan channel.
    void scanEntitiesUnsupported(int count);
    /// @brief Fixed OCR status without paths, stderr, or captured text.
    void ocrFailed(QString status);
    /// @brief A framed region produced no text, or an image OCR could not read.
    ///
    /// Separate from ocrFailed(), which is the diagnostic hook both paths share: a scan that
    /// hiccups is not worth a card, while a reader who framed a region by hand and got nothing
    /// back has to be told, or the gesture reads as a broken program.
    void screenshotFailed(QString status);

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

    /// @brief Rebuild the OCR engine around the stored paths and probe the new one.
    void rebuildOcr();
    void beginSelection(QPoint anchor);
    /// @param fromScreenshot True when the text came out of a framed region rather than a
    ///        selection. A screenshot is the reader saying "translate this": they framed it with a
    ///        gesture of their own, so the action bar's second question would be one they have
    ///        already answered. A selection keeps the bar -- there the gesture is only "show me
    ///        what this says", and the bar is where they say what to do with it.
    void classifyText(QString text, QPoint anchor, bool fromScreenshot = false);
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
