/**
 * @file app_controller.cpp
 * @brief QML facade that wires the application duties to the surfaces.
 */

#include "app_controller.h"

#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QHash>
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
      capture_(storage_, hook),
      explanation_(storage_, llm),
      cost_(storage_.statsStore(), llm, pricing),
      llm_(llm)
{
    connect(&capture_, &CaptureDuty::selectionBarRequested, this, &AppController::selectionBarRequested);
    connect(&capture_, &CaptureDuty::pointerPressed, this, &AppController::pointerPressed);
    connect(&capture_, &CaptureDuty::selectionReady, this, [this](PendingSelection selection) {
        explanation_.setSelection(std::move(selection));
    });
    connect(&explanation_, &ExplanationDuty::bubbleChanged, this, &AppController::bubbleChanged);
    connect(&explanation_, &ExplanationDuty::noticeChanged, this, &AppController::noticeChanged);
    connect(&explanation_, &ExplanationDuty::busyChanged, this, &AppController::busyChanged);
    connect(&explanation_, &ExplanationDuty::statsChanged, this, &AppController::statsChanged);

    llm.setExplanationLang(QString::fromStdString(store.explanationLang()));
    LENS_INFO("AppController ready: level={} explanation language '{}'", store.level(), store.explanationLang());
}

AppController::~AppController() = default;

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
    autoScan_ = on;
    LENS_INFO("automatic scanning {}", on ? "on" : "off");
    emit settingsChanged();
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
    storage_.writeDocument("PROVIDER", provider);
    emit settingsChanged();
}

void AppController::setModel(QString model)
{
    const QString value = model.trimmed();
    if (value.isEmpty())
        return;
    llm_.setModel(value);
    storage_.writeDocument("MODEL", value);
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
    return QVariantMap{{"level", storage_.knownStore().level()},
                       {"levels", StorageDuty::levelOptions()},
                       {"explanationLang", QString::fromStdString(storage_.knownStore().explanationLang())},
                       {"multiSense", storage_.documentString("multiSense", QStringLiteral("false")) == QLatin1String("true")},
                       {"theme", storage_.documentString("theme", QStringLiteral("light"))},
                       {"uiLanguage", storage_.documentString("uiLanguage", QStringLiteral("zh"))},
                       {"hasApiKey", !storage_.documentString("API-KEY", QString()).isEmpty()},
                       {"selectionCapture", storage_.documentString("selectionCapture", QStringLiteral("true")) == QLatin1String("true")},
                       {"clipboardPolicy", storage_.documentString("clipboardPolicy", QStringLiteral("topmost"))},
                       {"popupFrequency", storage_.documentString("popupFrequency", QStringLiteral("standard"))},
                       {"provider", storage_.documentString("PROVIDER", QStringLiteral("DeepSeek"))},
                       {"model", storage_.documentString("MODEL", QStringLiteral("deepseek-flash"))},
                       {"url", storage_.documentString("URL", QStringLiteral("https://api.deepseek.com"))},
                       {"autostart", autostartEnabled()},
                       {"autoScan", autoScan_}};
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

QVariantMap AppController::cost() const
{
    return cost_.cost();
}

QString AppController::modeLabel() const
{
    return autoScan_ ? tr("Auto") : tr("Manual");
}

QString AppController::busyLabel() const
{
    return explanation_.busyLabel();
}

}
