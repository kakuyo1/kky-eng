/**
 * @file app_controller.cpp
 * @brief QML facade that wires the application duties to the surfaces.
 */

#include "app_controller.h"

#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QSaveFile>
#include <QSet>
#include <QQmlEngine>
#include <QJSEngine>

#include <utility>

#include "autostart.h"
#include "core/log.h"
#include "notice.h"

namespace lens::app {

AppController::AppController(core::KnownStore& store,
                             llm::LlmClient& llm,
                             MouseSelectionHook& hook,
                             const llm::Pricing& pricing,
                             QObject* parent)
    : QObject(parent),
      storage_(store),
      cost_(storage_.statsStore(), llm, pricing, [] { return QDate::currentDate(); }, this), capture_(storage_, hook), explanation_(storage_, llm, cost_, this), llm_(llm)
{
    connect(&capture_, &CaptureDuty::selectionBarRequested, this, &AppController::selectionBarRequested);
    connect(&capture_, &CaptureDuty::pointerPressed, this, &AppController::pointerPressed);
    connect(&capture_, &CaptureDuty::selectionReady, this, [this](PendingSelection selection) {
        explanation_.setSelection(std::move(selection));
    });
    connect(&capture_, &CaptureDuty::scanCandidatesReady, this, [this](QVariantList candidates, QPoint anchor) {
        explanation_.enqueueScanCandidates(std::move(candidates), anchor);
    });
    connect(&capture_, &CaptureDuty::settingsChanged, this, &AppController::settingsChanged);
    connect(&explanation_, &ExplanationDuty::bubbleChanged, this, &AppController::bubbleChanged);
    connect(&explanation_, &ExplanationDuty::noticeChanged, this, &AppController::noticeChanged);
    connect(&explanation_, &ExplanationDuty::busyChanged, this, [this] {
        refreshCaptureGates();
        emit busyChanged();
    });
    connect(&explanation_, &ExplanationDuty::statsChanged, this, &AppController::statsChanged);
    connect(&cost_, &CostDuty::stateChanged, this, [this] {
        refreshCaptureGates();
        explanation_.resumeQueued();
        emit statsChanged();
        emit settingsChanged();
        emit dailyBudgetChanged();
    });

    llm.setExplanationLang(QString::fromStdString(store.explanationLang()));
    restoreModelService();
    cost_.setLegacyModel(llm_.model());
    gateTimer_.setInterval(1000);
    connect(&gateTimer_, &QTimer::timeout, this, [this] {
        refreshCaptureGates();
        explanation_.resumeQueued();
    });
    gateTimer_.start();
    refreshCaptureGates();
    LENS_INFO("AppController ready: level={} explanation language '{}'", store.level(), store.explanationLang());
}

AppController::~AppController() = default;

void AppController::restoreModelService()
{
    const QString provider = storage_.documentString("PROVIDER", llm::serviceCatalog().value("defaultProvider").toString());
    if (!llm_.setProvider(provider))
        llm_.setProvider(QStringLiteral("custom"));
    const QString url = storage_.documentString("URL", QString{}).trimmed();
    if (!url.isEmpty()) {
        const QUrl savedUrl(url);
        if (savedUrl.isValid() && savedUrl.scheme() == QLatin1String("https") && !savedUrl.host().isEmpty())
            llm_.setBaseUrl(savedUrl);
        else
            LENS_WARN("ignored an invalid persisted model service URL");
    }
    const QString model = storage_.documentString("MODEL", QString{}).trimmed();
    if (!model.isEmpty())
        llm_.setModel(model);
}

AppController* AppController::instance_ = nullptr;

void AppController::provide(AppController* instance)
{
    instance_ = instance;
}

AppController* AppController::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    Q_ASSERT(instance_ != nullptr);
    QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
    return instance_;
}

void AppController::onSelectionReleased(QPoint anchor)
{
    capture_.onSelectionReleased(anchor);
}

QPoint AppController::cursorPos() const
{
    return QCursor::pos();
}

void AppController::runSelectionAction(QString action, QString text)
{
    explanation_.runSelectionAction(std::move(action), std::move(text));
}

void AppController::mark(QString lemma, bool learned)
{
    explanation_.mark(std::move(lemma), learned);
}

void AppController::setAutoScan(bool on)
{
    capture_.setAutoScan(on);
}

void AppController::setOcrCapture(bool on)
{
    capture_.setOcrCapture(on);
}

void AppController::setMinimumWordLength(int length)
{
    capture_.setMinimumWordLength(length);
}

void AppController::setDragSensitivity(QString sensitivity)
{
    capture_.setDragSensitivity(std::move(sensitivity));
}

bool AppController::setScanWhitelist(QString processes)
{
    return capture_.setScanWhitelist(std::move(processes));
}

void AppController::restoreCaptureDefaults()
{
    capture_.restoreDefaults();
}

bool AppController::captureScreen()
{
    auto* const screen = QGuiApplication::screenAt(QCursor::pos());
    return screen != nullptr and capture_.captureRegion(screen->geometry());
}

void AppController::setLevel(int level)
{
    storage_.knownStore().setLevel(level);
    storage_.save();
    LENS_INFO("level set to {}", level);
    emit settingsChanged();
}

void AppController::setExplanationLang(QString lang)
{
    storage_.knownStore().setExplanationLang(lang.toStdString());
    llm_.setExplanationLang(lang);
    storage_.save();
    LENS_INFO("explanation language set to '{}'", lang.toStdString());
    emit settingsChanged();
    emit statsChanged();
}

void AppController::setMultiSense(bool on)
{
    storage_.writeDocument("multiSense", on ? QStringLiteral("true") : QStringLiteral("false"));
    emit settingsChanged();
}

void AppController::setTheme(QString theme)
{
    storage_.writeDocument("theme", theme);
    emit settingsChanged();
}

void AppController::setUiLanguage(QString lang)
{
    storage_.writeDocument("uiLanguage", lang);
    emit uiLanguageChanged(lang);
    emit settingsChanged();
    emit statsChanged();
}

void AppController::setSelectionCapture(bool on)
{
    storage_.writeDocument("selectionCapture", on ? QStringLiteral("true") : QStringLiteral("false"));
    LENS_INFO("selection capture {}", on ? "on" : "off");
    emit settingsChanged();
}

void AppController::setApiKey(QString key)
{
    llm_.setApiKey(key);
    storage_.writeDocument("API-KEY", key);
    LENS_INFO("API key {}", key.isEmpty() ? "cleared" : "updated");
    emit settingsChanged();
}

void AppController::setClipboardPolicy(QString policy)
{
    if (policy != QLatin1String("topmost") && policy != QLatin1String("silent")) {
        LENS_WARN("unknown clipboard policy '{}'; leaving the setting alone", policy.toStdString());
        return;
    }
    storage_.writeDocument("clipboardPolicy", policy);
    LENS_INFO("clipboard policy set to '{}'", policy.toStdString());
    emit settingsChanged();
}

void AppController::setPopupFrequency(QString frequency)
{
    if (frequency != QLatin1String("standard") && frequency != QLatin1String("less")) {
        LENS_WARN("unknown popup frequency '{}'; leaving the setting alone", frequency.toStdString());
        return;
    }
    storage_.writeDocument("popupFrequency", frequency);
    emit settingsChanged();
}

void AppController::setProvider(QString provider)
{
    if (explanation_.setProvider(provider))
        emit settingsChanged();
}

void AppController::setModel(QString model)
{
    if (explanation_.setModel(model))
        emit settingsChanged();
}

void AppController::setApiUrl(QString url)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https")) {
        LENS_WARN("refused a non-HTTPS model service URL");
        return;
    }
    llm_.setBaseUrl(parsed);
    storage_.writeDocument("URL", parsed.toString());
    emit settingsChanged();
}

void AppController::setAutostart(bool on)
{
    writeAutostart(on);
    emit settingsChanged();
}

void AppController::bubbleHoverChanged(bool hovering)
{
    LENS_TRACE("bubble hover {}", hovering);
}

void AppController::dismissBubble()
{
    explanation_.dismissBubble();
}

void AppController::dismissNotice()
{
    explanation_.dismissNotice();
}

QVariantMap AppController::bubble() const
{
    return explanation_.bubble();
}

QVariantMap AppController::notice() const
{
    return explanation_.notice();
}

QVariantMap AppController::settings() const
{
    const QString provider = storage_.documentString("PROVIDER", llm::serviceCatalog().value("defaultProvider").toString());
    QVariantMap result{{"level", storage_.knownStore().level()},
                       {"levels", StorageDuty::levelOptions()},
                       {"explanationLang", QString::fromStdString(storage_.knownStore().explanationLang())},
                       {"multiSense", storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true")},
                       {"theme", storage_.documentString("theme", QStringLiteral("light"))},
                       {"uiLanguage", storage_.documentString("uiLanguage", QStringLiteral("zh"))},
                       {"hasApiKey", !storage_.documentString("API-KEY", QString()).isEmpty()},
                       {"selectionCapture", storage_.documentString("selectionCapture", QStringLiteral("true")) == QLatin1String("true")},
                       {"clipboardPolicy", storage_.documentString("clipboardPolicy", QStringLiteral("topmost"))},
                       {"popupFrequency", storage_.documentString("popupFrequency", QStringLiteral("standard"))},
                       {"provider", provider},
                       {"model", llm_.model()},
                       {"url", llm_.baseUrl().toString()},
                       {"dailyBudget", dailyBudget()},
                       {"autostart", autostartEnabled()}};
    const auto captureSettings = capture_.settings();
    for (auto it = captureSettings.cbegin(); it != captureSettings.cend(); ++it)
        result.insert(it.key(), it.value());
    result.insert(QStringLiteral("ocrStatus"), capture_.ocrStatus());
    const auto explanationSettings = explanation_.settings();
    for (auto it = explanationSettings.cbegin(); it != explanationSettings.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QVariantMap AppController::stats() const
{
    return cost_.stats();
}

QVariantList AppController::words() const
{
    const QDate today = QDate::currentDate();
    QSet<QString> seen;
    QHash<QString, int> pops;
    const auto& history = storage_.statsStore().history();
    for (const core::HistoryEntry& entry : history)
        ++pops[QString::fromStdString(entry.lemma)];

    QVariantList out;
    out.reserve(static_cast<qsizetype>(history.size()));
    for (const core::HistoryEntry& entry : history) {
        const QString shown = QString::fromStdString(entry.lemma);
        if (seen.contains(shown))
            continue;
        seen.insert(shown);

        const QDateTime moment = QDateTime::fromString(QString::fromStdString(entry.minute), QStringLiteral("yyyy-MM-dd HH:mm"));
        QString when;
        if (moment.isValid()) {
            const qint64 daysAgo = moment.date().daysTo(today);
            if (daysAgo == 0)
                when = tr("Today %1").arg(moment.toString(QStringLiteral("HH:mm")));
            else if (daysAgo == 1)
                when = tr("Yesterday %1").arg(moment.toString(QStringLiteral("HH:mm")));
            else
                when = moment.toString(QStringLiteral("MM-dd HH:mm"));
        }

        QString status;
        if (entry.verdict == "known")
            status = tr("Known");
        else if (entry.verdict == "new")
            status = tr("New");

        out.append(QVariantMap{{"word", shown},
                               {"when", when},
                               {"verdict", QString::fromStdString(entry.verdict)},
                               {"status", status},
                               {"pops", pops.value(shown)}});
    }
    return out;
}

QString AppController::exportWords(QString scope)
{
    core::ExportScope which;
    if (scope == QLatin1String("all"))
        which = core::ExportScope::All;
    else if (scope == QLatin1String("known"))
        which = core::ExportScope::Known;
    else if (scope == QLatin1String("new"))
        which = core::ExportScope::New;
    else {
        LENS_WARN("export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return {};
    }
    const QString text = QString::fromStdString(core::exportLemmas(storage_.statsStore().history(), which));
    LENS_INFO("words export for '{}': {} character(s) ready", scope.toStdString(), text.size());
    return text;
}

bool AppController::saveWords(QUrl path, QString scope)
{
    if (scope != QLatin1String("all") && scope != QLatin1String("known") && scope != QLatin1String("new")) {
        LENS_WARN("words export asked for an unknown scope '{}'; nothing was written", scope.toStdString());
        return false;
    }
    const QString local = path.toLocalFile();
    if (local.isEmpty()) {
        LENS_WARN("words export was given '{}', which names no local file", path.toString().toStdString());
        return false;
    }
    const QString text = exportWords(scope);
    QSaveFile file(local);
    if (!file.open(QIODevice::WriteOnly)) {
        LENS_WARN("words export could not open '{}' for writing", local.toStdString());
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        LENS_WARN("words export failed to write '{}'", local.toStdString());
        return false;
    }
    LENS_INFO("words export for '{}': {} character(s) written to '{}'", scope.toStdString(), text.size(), local.toStdString());
    return true;
}

bool AppController::setDailyBudget(double amount)
{
    if (!cost_.setDailyBudget(amount))
        return false;
    storage_.save();
    return true;
}

bool AppController::removeWord(QString lemma)
{
    const auto value = lemma.trimmed();
    if (value.isEmpty())
        return false;
    explanation_.invalidateWord(value);
    const bool removed = storage_.removeWord(value);
    if (removed) {
        emit statsChanged();
        emit bubbleChanged();
    }
    return removed;
}

QVariantMap AppController::cost() const
{
    return cost_.cost();
}

QString AppController::modeLabel() const
{
    return capture_.settings().value("autoScan").toBool() ? tr("Auto") : tr("Manual");
}

QString AppController::busyLabel() const
{
    return explanation_.busyLabel();
}

double AppController::dailyBudget() const
{
    return storage_.statsStore().dailyBudget();
}

void AppController::refreshCaptureGates()
{
    const bool budgetPaused = !cost_.canScan();
    capture_.setBudgetPaused(budgetPaused);
    capture_.setScanPaused(budgetPaused || !explanation_.busyLabel().isEmpty());
}

}
