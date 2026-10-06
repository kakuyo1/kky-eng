/** @file ocr_engine.cpp
 * @brief Tesseract process adapter with bundled runtime resolution and bounded I/O.
 */
#include "ocr_engine.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace lens::capture {
namespace {

constexpr qsizetype kMaxOutput = 1024 * 1024;
constexpr qsizetype kMaxError  = 64 * 1024;

struct TesseractOcr final : OcrEngine {
    explicit TesseractOcr(TesseractConfig const& config_)
        : config{resolveTesseract(config_)}
    {}

    OcrStatus resourceStatus() const
    {
        if (config.executable.isEmpty() or not QFileInfo{config.executable}.isFile()) return OcrStatus::RuntimeMissing;
        auto const data = QFileInfo{QDir{config.dataDirectory}.filePath(QStringLiteral("eng.traineddata"))};
        return data.isReadable() and data.size() > 0 ? OcrStatus::Ready : OcrStatus::LanguageMissing;
    }

    OcrStatus probe(std::shared_ptr<std::atomic_bool> cancel = {}) const override
    {
        if (resourceStatus() != OcrStatus::Ready) return resourceStatus();
        QImage blank{64, 64, QImage::Format_Grayscale8};
        blank.fill(Qt::white);
        auto const result = recognizeImage(blank, cancel);
        return result.status == OcrStatus::EmptyText or result.status == OcrStatus::Ready ? OcrStatus::Ready : result.status;
    }

    OcrResult recognize(QImage const& image, std::shared_ptr<std::atomic_bool> cancel = {}) const override
    {
        if (image.isNull() or image.width() > 8192 or image.height() > 8192 or image.sizeInBytes() > 128 * 1024 * 1024)
            return {.status = OcrStatus::InvalidImage};
        if (cancel and cancel->load(std::memory_order_relaxed)) return {.status = OcrStatus::Failed};
        auto const available = resourceStatus();
        if (available != OcrStatus::Ready) return {.status = available};
        return recognizeImage(image, std::move(cancel));
    }

    OcrResult recognizeImage(QImage const& image, std::shared_ptr<std::atomic_bool> const& cancel) const
    {
        QByteArray png;
        QBuffer buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not image.save(&buffer, "PNG")) return {.status = OcrStatus::InvalidImage};

        QProcess process;
        QElapsedTimer timer;
        timer.start();
        process.start(config.executable,
                      {QStringLiteral("stdin"), QStringLiteral("stdout"), QStringLiteral("--tessdata-dir"), config.dataDirectory, QStringLiteral("-l"), QStringLiteral("eng"), QStringLiteral("--psm"), QStringLiteral("3")});
        if (not process.waitForStarted(config.timeoutMs)) return {.status = OcrStatus::StartFailed};

        QByteArray output;
        qsizetype errorBytes = 0;
        auto drain           = [&] {
            const auto outputLimit = std::max<qsizetype>(1, kMaxOutput - output.size() + 1);
            process.setReadChannel(QProcess::StandardOutput);
            output += process.read(static_cast<qint64>(outputLimit));
            const auto errorLimit = std::max<qsizetype>(1, kMaxError - errorBytes + 1);
            process.setReadChannel(QProcess::StandardError);
            errorBytes += process.read(static_cast<qint64>(errorLimit)).size();
            process.setReadChannel(QProcess::StandardOutput);
            return output.size() <= kMaxOutput and errorBytes <= kMaxError;
        };
        qsizetype written = 0;
        while (written < png.size()) {
            if (cancel and cancel->load(std::memory_order_relaxed)) {
                process.kill();
                process.waitForFinished(1000);
                return {.status = OcrStatus::Failed};
            }
            auto const count = process.write(png.constData() + written, png.size() - written);
            if (count > 0) {
                written += count;
                if (not drain()) {
                    process.kill();
                    process.waitForFinished(1000);
                    return {.status = OcrStatus::Failed};
                }
                continue;
            }
            auto const remaining = std::max(1, config.timeoutMs - static_cast<int>(timer.elapsed()));
            if (not process.waitForBytesWritten(std::min(remaining, 50))) return {.status = OcrStatus::Failed};
        }
        process.closeWriteChannel();

        while (process.state() != QProcess::NotRunning) {
            if (cancel and cancel->load(std::memory_order_relaxed)) {
                process.kill();
                process.waitForFinished(1000);
                return {.status = OcrStatus::Failed};
            }
            if (not drain()) {
                process.kill();
                process.waitForFinished(1000);
                return {.status = OcrStatus::Failed};
            }
            auto const elapsed = static_cast<int>(timer.elapsed());
            if (elapsed >= config.timeoutMs) {
                process.kill();
                process.waitForFinished(1000);
                return {.status = OcrStatus::TimedOut};
            }
            process.setReadChannel(QProcess::StandardOutput);
            process.waitForReadyRead(std::min(config.timeoutMs - elapsed, 10));
            process.setReadChannel(QProcess::StandardError);
            process.waitForReadyRead(0);
            process.setReadChannel(QProcess::StandardOutput);
        }
        if (not drain() or process.exitStatus() != QProcess::NormalExit or process.exitCode() != 0)
            return {.status = OcrStatus::Failed};
        auto text = QString::fromUtf8(output).trimmed();
        if (text.isEmpty()) return {.status = OcrStatus::EmptyText};
        if (text.size() > 65536) return {.status = OcrStatus::Failed};
        return {.status = OcrStatus::Ready, .text = std::move(text)};
    }

    TesseractConfig config;
};

}

TesseractConfig resolveTesseract(TesseractConfig config)
{
    config.timeoutMs = std::clamp(config.timeoutMs, 100, 30000);
    if (config.executable.isEmpty()) {
        auto const bundled = QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("ocr/tesseract.exe"));
        config.executable  = QFileInfo::exists(bundled) ? bundled : QString{};
#ifdef LENS_OCR_DEV_PATH_FALLBACK
        if (config.executable.isEmpty())
            config.executable = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
#endif
    }
    if (config.dataDirectory.isEmpty() and not config.executable.isEmpty())
        config.dataDirectory = QFileInfo{config.executable}.dir().filePath(QStringLiteral("tessdata"));
    return config;
}

std::unique_ptr<OcrEngine> makeTesseractOcr(TesseractConfig const& config)
{
    return std::make_unique<TesseractOcr>(config);
}

std::uint64_t pixelFingerprint(QImage const& image)
{
    auto const pixels = image.convertToFormat(QImage::Format_Grayscale8);
    auto hash         = std::uint64_t{14695981039346656037ULL};
    for (int y = 0; y < pixels.height(); ++y) {
        auto const* row = pixels.constScanLine(y);
        for (int x = 0; x < pixels.width(); ++x) {
            hash ^= row[x];
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<std::uint64_t>(pixels.width());
    hash *= 1099511628211ULL;
    hash ^= static_cast<std::uint64_t>(pixels.height());
    return hash;
}

}
