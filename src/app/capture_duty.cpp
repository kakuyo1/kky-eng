/**
 * @file capture_duty.cpp
 * @brief Selection capture and local OCR acquisition, independent of explanation presentation.
 */

#include "capture_duty.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <thread>

#include "capture/capture_policy.h"
#include "capture/foreground_window.h"
#include "core/filter_core.h"
#include "core/log.h"
#include "mouse_selection_hook.h"

namespace lens::app {
namespace {

constexpr int kMaxSelectionChars = 1000;

std::size_t rankForLevel(int level)
{
    static constexpr std::size_t kRanks[] = {4000, 9000, 5000, 8000, 8000, 12000, 9000, 12000};
    constexpr int kLast                   = static_cast<int>(std::size(kRanks)) - 1;
    return kRanks[std::clamp(level, 0, kLast)];
}

}

CaptureDuty::CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent)
    : CaptureDuty(storage, hook, capture::makeTesseractOcr(), parent)
{}

CaptureDuty::CaptureDuty(StorageDuty& storage,
                         MouseSelectionHook& hook,
                         std::unique_ptr<capture::OcrEngine> ocr,
                         QObject* parent)
    : QObject(parent), storage_(storage), hook_(hook), grabber_(std::make_unique<SelectionTextGrabber>()),
      ocr_(std::move(ocr)), scan_(capture::makeScanPipeline())
{
    if (not ocr_) throw std::invalid_argument{"OCR backend must not be null"};
    connect(&hook_, &MouseSelectionHook::selectionReleased, this, &CaptureDuty::onSelectionReleased);
    connect(&hook_, &MouseSelectionHook::pointerPressed, this, &CaptureDuty::pointerPressed);

    auto const sensitivity = settings().value("dragSensitivity").toString();
    hook_.setDragThreshold(capture::dragThreshold(sensitivity == QLatin1String("sensitive")
                                                      ? capture::DragSensitivity::Sensitive
                                                  : sensitivity == QLatin1String("reluctant")
                                                      ? capture::DragSensitivity::Reluctant
                                                      : capture::DragSensitivity::Standard));
    auto const names = settings().value("scanWhitelist").toString().split(';', Qt::SkipEmptyParts);
    std::vector<std::string> whitelist;
    for (auto const& name : names)
        whitelist.push_back(name.toStdString());
    scan_->setWhitelist(std::move(whitelist));
    scanTimer_.setInterval(1000);
    connect(&scanTimer_, &QTimer::timeout, this, &CaptureDuty::scanTick);
    scanTimer_.start();
    pollTimer_.setInterval(25);
    connect(&pollTimer_, &QTimer::timeout, this, &CaptureDuty::pollOcr);
    probeOcr();
}

CaptureDuty::~CaptureDuty()
{
    scanTimer_.stop();
    pollTimer_.stop();
    if (job_) job_->cancelled->store(true, std::memory_order_relaxed);
    job_.reset();
    jobContext_.reset();
}

QVariantMap CaptureDuty::settings() const
{
    bool valid        = false;
    auto const length = storage_.documentString("minimumWordLength", QStringLiteral("3")).toInt(&valid);
    auto sensitivity  = storage_.documentString("dragSensitivity", QStringLiteral("standard"));
    if (sensitivity != QLatin1String("sensitive") and sensitivity != QLatin1String("reluctant"))
        sensitivity = QStringLiteral("standard");
    return {{"ocrCapture", storage_.documentString("ocrCapture", QStringLiteral("false")) == QLatin1String("true")},
            {"autoScan", storage_.documentString("autoScan", QStringLiteral("false")) == QLatin1String("true")},
            {"ocrAvailable", ocrState_ == capture::OcrStatus::Ready},
            {"minimumWordLength", valid ? capture::minimumWordLength(length) : 3},
            {"dragSensitivity", sensitivity},
            {"scanWhitelist", storage_.documentString("scanWhitelist", QStringLiteral("chrome.exe;msedge.exe;firefox.exe;AcroRd32.exe"))}};
}

QString CaptureDuty::ocrStatus() const
{
    if (jobContext_ and jobContext_->probe) return QStringLiteral("checking");
    switch (ocrState_) {
        case capture::OcrStatus::Ready: return QStringLiteral("ready");
        case capture::OcrStatus::RuntimeMissing: return QStringLiteral("runtime-missing");
        case capture::OcrStatus::LanguageMissing: return QStringLiteral("english-data-missing");
        case capture::OcrStatus::StartFailed: return QStringLiteral("start-failed");
        case capture::OcrStatus::TimedOut: return QStringLiteral("timeout");
        case capture::OcrStatus::InvalidImage: return QStringLiteral("invalid-image");
        case capture::OcrStatus::EmptyText: return QStringLiteral("empty-text");
        default: return QStringLiteral("failed");
    }
}

void CaptureDuty::invalidateScan()
{
    ++generation_;
    scanContext_.clear();
    scan_->reset();
}

void CaptureDuty::setOcrCapture(bool const on)
{
    storage_.writeDocument("ocrCapture", on ? QStringLiteral("true") : QStringLiteral("false"));
    ++screenshotGeneration_;
    emit settingsChanged();
}

void CaptureDuty::setAutoScan(bool const on)
{
    storage_.writeDocument("autoScan", on ? QStringLiteral("true") : QStringLiteral("false"));
    invalidateScan();
    emit settingsChanged();
}

void CaptureDuty::setMinimumWordLength(int const length)
{
    if (length < 2 or length > 5) return;
    storage_.writeDocument("minimumWordLength", QString::number(length));
    invalidateScan();
    emit settingsChanged();
}

void CaptureDuty::setDragSensitivity(QString sensitivity)
{
    if (sensitivity != QLatin1String("sensitive") and sensitivity != QLatin1String("standard") and
        sensitivity != QLatin1String("reluctant")) return;
    storage_.writeDocument("dragSensitivity", sensitivity);
    auto const tier = sensitivity == QLatin1String("sensitive")   ? capture::DragSensitivity::Sensitive
                      : sensitivity == QLatin1String("reluctant") ? capture::DragSensitivity::Reluctant
                                                                  : capture::DragSensitivity::Standard;
    hook_.setDragThreshold(capture::dragThreshold(tier));
    emit settingsChanged();
}

bool CaptureDuty::setScanWhitelist(QString processes)
{
    if (processes.size() > 4096) return false;
    auto const names = processes.split(QRegularExpression{QStringLiteral("[;,\\s]+")}, Qt::SkipEmptyParts);
    if (names.size() > 32) return false;
    static QRegularExpression const executable{QStringLiteral("^[A-Za-z0-9_.-]+\\.exe$"), QRegularExpression::CaseInsensitiveOption};
    QStringList normalized;
    std::vector<std::string> whitelist;
    for (auto const& name : names) {
        if (name.size() > 128 or not executable.match(name).hasMatch()) return false;
        if (normalized.contains(name.toLower())) continue;
        normalized.append(name.toLower());
        whitelist.push_back(name.toLower().toStdString());
    }
    storage_.writeDocument("scanWhitelist", normalized.join(';'));
    invalidateScan();
    scan_->setWhitelist(std::move(whitelist));
    emit settingsChanged();
    return true;
}

void CaptureDuty::restoreDefaults()
{
    setAutoScan(false);
    setOcrCapture(false);
    setMinimumWordLength(3);
    setDragSensitivity(QStringLiteral("standard"));
    setScanWhitelist(QStringLiteral("chrome.exe;msedge.exe;firefox.exe;AcroRd32.exe"));
}

void CaptureDuty::setScanPaused(bool const paused)
{
    if (scanPaused_ == paused) return;
    scanPaused_ = paused;
    invalidateScan();
}

void CaptureDuty::setBudgetPaused(bool const paused)
{
    if (budgetPaused_ == paused) return;
    budgetPaused_ = paused;
    ++screenshotGeneration_;
    invalidateScan();
}

void CaptureDuty::probeOcr()
{
    if (job_) return;
    jobContext_ = OcrJob{.probe = true};
    auto state  = std::make_shared<OcrJobState>();
    job_        = state;
    auto engine = ocr_;
    std::thread([state, engine = std::move(engine)] {
        capture::OcrResult result;
        try {
            result.status = engine->probe(state->cancelled);
        } catch (...) {
            result.status = capture::OcrStatus::Failed;
        }
        std::lock_guard lock{state->mutex};
        state->result = std::move(result);
        state->done   = true;
    }).detach();
    pollTimer_.start();
    emit settingsChanged();
}

void CaptureDuty::launchOcr(QImage image, QPoint const anchor, QString context)
{
    auto const generation = context.isEmpty() ? screenshotGeneration_ : generation_;
    jobContext_           = OcrJob{.anchor = anchor, .context = std::move(context), .generation = generation};
    auto state            = std::make_shared<OcrJobState>();
    job_                  = state;
    auto engine           = ocr_;
    std::thread([state, engine = std::move(engine), image = std::move(image)] {
        capture::OcrResult result;
        try {
            result = engine->recognize(image, state->cancelled);
        } catch (...) {
            result.status = capture::OcrStatus::Failed;
        }
        std::lock_guard lock{state->mutex};
        state->result = std::move(result);
        state->done   = true;
    }).detach();
    pollTimer_.start();
}

bool CaptureDuty::captureScreenshot(QImage image, QPoint const anchor)
{
    if (budgetPaused_ or job_ or not settings().value("ocrCapture").toBool() or ocrState_ != capture::OcrStatus::Ready)
        return false;
    launchOcr(std::move(image), anchor, {});
    return true;
}

bool CaptureDuty::captureRegion(QRect const region)
{
    if (budgetPaused_ or job_ or not settings().value("ocrCapture").toBool() or ocrState_ != capture::OcrStatus::Ready)
        return false;
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) return false;
    if (region.width() * screen->devicePixelRatio() > 8192 or region.height() * screen->devicePixelRatio() > 8192) return false;
    auto const nativeOrigin = capture::physicalScreenOrigin(screen->name());
    if (not nativeOrigin) return false;
    auto const local  = region.translated(-screen->geometry().topLeft());
    auto const image  = screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
    auto const anchor = *nativeOrigin + local.topLeft() * screen->devicePixelRatio();
    return captureScreenshot(image, anchor);
}

void CaptureDuty::scanTick()
{
    if (budgetPaused_ or scanPaused_ or not settings().value("autoScan").toBool() or ocrState_ != capture::OcrStatus::Ready) return;
    auto const foreground = capture::foregroundWindow();
    if (not foreground) {
        invalidateScan();
        return;
    }
    capture::ScanFrame frame{.context     = foreground->context.toStdString(),
                             .processName = foreground->processName.toStdString(),
                             .ownProcess  = foreground->ownProcess};
    if (not scan_->allows(frame)) {
        invalidateScan();
        return;
    }
    if (scanContext_ != foreground->context) {
        invalidateScan();
        scanContext_ = foreground->context;
    }
    if (job_) return;
    QScreen* screen = nullptr;
    for (auto* const candidate : QGuiApplication::screens())
        if (candidate->name() == foreground->screenName) {
            screen = candidate;
            break;
        }
    if (screen == nullptr) return;
    auto const image = screen->grabWindow(foreground->window).toImage();
    if (image.isNull()) {
        invalidateScan();
        return;
    }
    frame.pixels   = capture::pixelFingerprint(image);
    auto const now = std::chrono::steady_clock::now();
    scanAnchor_    = foreground->anchor;
    if (scan_->needsOcr(frame, now))
        launchOcr(image, scanAnchor_, scanContext_);
    else
        publishBatch(now, scanAnchor_);
}

void CaptureDuty::pollOcr()
{
    if (not job_) return;
    auto state = job_;
    capture::OcrResult result;
    {
        std::lock_guard lock{state->mutex};
        if (not state->done) return;
        result = state->result;
    }
    auto const context = *jobContext_;
    job_.reset();
    jobContext_.reset();
    pollTimer_.stop();
    if (context.probe) {
        ocrState_ = result.status;
        emit settingsChanged();
        if (result.status != capture::OcrStatus::Ready) {
            LENS_WARN("OCR probe failed (status {})", ocrStatus().toStdString());
            emit ocrFailed(ocrStatus());
        }
        return;
    }
    if (context.generation != (context.context.isEmpty() ? screenshotGeneration_ : generation_)) return;
    if (not context.context.isEmpty()) {
        auto const foreground = capture::foregroundWindow();
        if (budgetPaused_ or scanPaused_ or not foreground or foreground->context != context.context or
            not settings().value("autoScan").toBool()) {
            invalidateScan();
            return;
        }
    }
    if (result.status != capture::OcrStatus::Ready) {
        scan_->reset();
        if (result.status != capture::OcrStatus::EmptyText and result.status != capture::OcrStatus::InvalidImage) {
            ocrState_ = result.status;
            emit settingsChanged();
            LENS_WARN("OCR worker failed (status {})", ocrStatus().toStdString());
        }
        emit ocrFailed(result.status == capture::OcrStatus::EmptyText      ? QStringLiteral("empty-text")
                       : result.status == capture::OcrStatus::InvalidImage ? QStringLiteral("invalid-image")
                                                                           : ocrStatus());
        return;
    }
    if (context.context.isEmpty()) {
        classifyText(result.text, context.anchor);
        return;
    }
    auto const unsupported = capture::unsupportedEntityCount(result.text.toStdString());
    if (unsupported > 0) {
        LENS_INFO("automatic scan retained {} unsupported entity candidate(s); entity channel is not enabled", unsupported);
        emit scanEntitiesUnsupported(static_cast<int>(unsupported));
    }
    auto const now = std::chrono::steady_clock::now();
    scan_->acceptText(result.text.toStdString(),
                      {.minimumLength = settings().value("minimumWordLength").toInt(),
                       .knownLemmas   = storage_.knownStore().known(),
                       .minimumRank   = minFreqRank()},
                      now);
    publishBatch(now, context.anchor);
}

void CaptureDuty::publishBatch(capture::ScanTime const now, QPoint const anchor)
{
    const auto batch = scan_->takeBatch(now);
    QVariantList words;
    for (auto const& word : batch)
        words.append(QVariantMap{{"kind", QStringLiteral("word")},
                                 {"surface", QString::fromStdString(word.surface)},
                                 {"lemma", QString::fromStdString(word.lemma)}});
    if (not words.isEmpty()) emit scanCandidatesReady(words, anchor);
}

void CaptureDuty::onSelectionReleased(QPoint const anchor)
{
    if (budgetPaused_) return;
    if (storage_.documentString("selectionCapture", QStringLiteral("true")) != QLatin1String("true")) {
        LENS_TRACE("selection at ({}, {}) ignored: selection capture is off", anchor.x(), anchor.y());
        return;
    }
    beginSelection(anchor);
}

void CaptureDuty::beginSelection(QPoint const anchor)
{
    const auto grabbed = grabber_->grab();
    if (const auto* status = std::get_if<GrabStatus>(&grabbed)) {
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return;
    }
    const GrabbedText& captured = std::get<GrabbedText>(grabbed);
    if (captured.clipboardReplaced and storage_.documentString("clipboardPolicy", QStringLiteral("topmost")) == QLatin1String("silent")) {
        LENS_INFO("the clipboard was rewritten during the grab; the policy is silent, so the selection is dropped");
        return;
    }
    classifyText(captured.text, anchor);
}

void CaptureDuty::classifyText(QString text, QPoint const anchor)
{
    if (text.size() > kMaxSelectionChars) text = text.left(kMaxSelectionChars) + QStringLiteral("…");
    const core::Selection selection = core::classifySelection(text.toStdString(), storage_.knownStore().known(), minFreqRank());
    PendingSelection pending{.anchor = anchor, .text = text};
    switch (selection.kind) {
        case core::SelectionKind::Entity:
            pending.kind = QStringLiteral("entity");
            break;
        case core::SelectionKind::Sentence:
            pending.kind = QStringLiteral("sentence");
            break;
        case core::SelectionKind::Word: {
            const auto& candidates = selection.candidates;
            const auto fresh       = std::find_if(candidates.begin(), candidates.end(), [](const core::Candidate& candidate) {
                return candidate.state == core::CandidateState::New;
            });
            if (candidates.empty()) return;
            const core::Candidate& word = fresh != candidates.end() ? *fresh : candidates.front();
            pending.kind                = QStringLiteral("word");
            pending.surface             = QString::fromStdString(word.surface);
            pending.lemma               = QString::fromStdString(word.lemma);
            break;
        }
    }
    emit selectionReady(pending);
    emit selectionBarRequested(QVariantMap{{"x", anchor.x()}, {"y", anchor.y()}, {"kind", pending.kind}, {"text", pending.text}});
}

std::size_t CaptureDuty::minFreqRank() const
{
    return rankForLevel(storage_.knownStore().level());
}

}
