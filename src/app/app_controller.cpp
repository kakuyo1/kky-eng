/**
 * @file app_controller.cpp
 * @brief The selection pipeline, from a finished gesture to a surface.
 */

#include "app_controller.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QGuiApplication>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include "core/filter_core.h"
#include "core/log.h"
#include "mouse_selection_hook.h"

namespace lens::app {
namespace {

/// Time stamp format shared with StatsStore: local, minute resolution.
QString nowMinute()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/**
 * Difficulty cut-off per level (PHASE1.md section 4.1).
 *
 * ponytail: a frequency rank is what a word book roughly is, so this stands in until
 * TODO.md's eight word books land -- at which point a level becomes a set membership test
 * and this table goes away. Expect misjudgements in both directions meanwhile.
 */
std::size_t rankForLevel(int level)
{
    static constexpr std::size_t kRanks[] = {
        4000,  // B1-B2
        9000,  // C1-C2
        5000,  // CET-4
        8000,  // CET-6
        8000,  // TEM-4
        12000, // TEM-8
        9000,  // IELTS
        12000, // TOEFL
    };
    constexpr int kLast = static_cast<int>(std::size(kRanks)) - 1;
    return kRanks[std::clamp(level, 0, kLast)];
}

/// @brief The level list the settings dropdown draws, in UI.md 4.4's order.
QVariantList levelOptions()
{
    const struct {
        const char* label;
        const char* group;
        const char* note; // empty for all but CET-4
    } levels[] = {
        {"B1–B2", QT_TRANSLATE_NOOP("lens::app::AppController", "CEFR"), ""},
        {"C1–C2", QT_TRANSLATE_NOOP("lens::app::AppController", "CEFR"), ""},
        {"CET-4", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), "Minimum requirement"},
        {"CET-6", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"TEM-4", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"TEM-8", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"IELTS", QT_TRANSLATE_NOOP("lens::app::AppController", "Study-abroad exams"), ""},
        {"TOEFL", QT_TRANSLATE_NOOP("lens::app::AppController", "Study-abroad exams"), ""},
    };

    QVariantList out;
    int value = 0;
    for (const auto& level : levels) {
        out.append(QVariantMap{{"value", value++},
                               {"label", QString::fromUtf8(level.label)},
                               {"group", QCoreApplication::translate("lens::app::AppController", level.group)},
                               {"note", QString::fromUtf8(level.note)}});
    }
    return out;
}

/// @return The currency symbol for the price list's currency code.
QString currencySymbol(const QString& code)
{
    if (code == QLatin1String("USD"))
        return QStringLiteral("$");
    if (code == QLatin1String("CNY"))
        return QString::fromUtf8("¥");
    return code;
}

} // namespace

AppController::AppController(core::KnownStore& store, llm::LlmClient& llm, MouseSelectionHook& hook, const llm::Pricing& pricing, QObject* parent)
    : QObject(parent), store_(store), stats_(store.document()), llm_(llm), hook_(hook),
      pricing_(pricing), grabber_(std::make_unique<SelectionTextGrabber>())
{
    connect(&hook_, &MouseSelectionHook::selectionReleased, this, &AppController::onSelectionReleased);
    connect(&hook_, &MouseSelectionHook::pointerPressed, this, &AppController::pointerPressed);

    connect(&llm_, &llm::LlmClient::batchFinished, this, [this](const QVector<llm::WordExplanation>& results, llm::Usage usage) {
        busyLabel_.clear();
        emit busyChanged();

        stats_.recordUsage(nowMinute().toStdString(), usage.promptTokens, usage.completionTokens);
        if (results.isEmpty()) {
            LENS_WARN("the model returned no explanation");
            store_.save(); // the usage still counts, even with nothing to show
            emit statsChanged();
            return;
        }

        const llm::WordExplanation& first = results.front();
        store_.cachePut(first.word.toStdString(), {first.en.toStdString(), first.zh.toStdString()});
        store_.save();
        showBubble(first.word, first.en, first.zh, pending_.anchor);
        emit statsChanged();
    });

    connect(&llm_, &llm::LlmClient::failed, this, [this](const QString& message) {
        busyLabel_.clear();
        emit busyChanged();
        // No error surface is specified for phase 1, and a request that fails silently reads
        // as a broken app. The bubble carries the reason, with no status chip: it is not an
        // explanation, and nothing here pretends otherwise.
        showNotice(message, pending_);
    });

    llm_.setExplanationLang(QString::fromStdString(store_.explanationLang()));
    LENS_INFO("AppController ready: level={} explanation language '{}'",
              store_.level(),
              store_.explanationLang());
}

AppController::~AppController() = default;

void AppController::onSelectionReleased(QPoint anchor)
{
    if (documentString("selectionCapture", QStringLiteral("true")) != QLatin1String("true")) {
        LENS_TRACE("selection at ({}, {}) ignored: selection capture is off", anchor.x(), anchor.y());
        return;
    }
    beginSelection(anchor);
}

void AppController::beginSelection(QPoint anchor)
{
    const auto grabbed = grabber_->grab();
    if (const auto* status = std::get_if<GrabStatus>(&grabbed)) {
        // Expected outcomes, not faults: a release inside our own surface, a terminal, or a
        // drag over something that does not copy. The grabber has already logged the reason.
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return;
    }

    const QString text = std::get<QString>(grabbed);
    // What the selection is gets decided here, once, and the two items on the bar only route
    // on the answer. That ordering is the specification, not a convenience: translate and
    // explain are two ways into the same decision (UI.md section 4.9).
    const core::Selection selection = core::classifySelection(text.toStdString(), store_.known(), minFreqRank());

    pending_ = Pending{anchor, text, QString(), QString(), QString()};
    switch (selection.kind) {
        case core::SelectionKind::Sentence:
            // Nothing worth explaining, but the bar still comes up: copying the selection is
            // still something the reader asked for. There is no channel behind a sentence yet,
            // so the other two items have nothing to send -- which explain() says out loud.
            pending_.kind = QStringLiteral("sentence");
            LENS_INFO("selection of {} character(s): no candidate, offering copy only", text.size());
            break;
        case core::SelectionKind::Word:
            pending_.kind = QStringLiteral("word");
            pending_.surface = QString::fromStdString(selection.candidates.front().surface);
            pending_.lemma = QString::fromStdString(selection.candidates.front().lemma);
            LENS_INFO("selection of {} character(s): word channel, '{}' -> '{}'", text.size(), pending_.surface.toStdString(), pending_.lemma.toStdString());
            break;
    }

    emit selectionBarRequested(QVariantMap{{"x", anchor.x()},
                                           {"y", anchor.y()},
                                           {"kind", pending_.kind},
                                           {"text", pending_.text}});
}

void AppController::runSelectionAction(QString action, QString text)
{
    if (pending_.text.isEmpty()) {
        LENS_WARN("action '{}' arrived with no selection pending", action.toStdString());
        return;
    }
    if (text != pending_.text) {
        // The surface echoes back what it was shown; a mismatch means the two have drifted,
        // and the selection the reader saw is the one that was analysed.
        LENS_WARN("the surface echoed {} character(s) back, the selection held {}; using the selection",
                  text.size(),
                  pending_.text.size());
    }

    if (action == QLatin1String("copy")) {
        // The grabber put the reader's own clipboard back; this is a fresh, deliberate copy.
        QGuiApplication::clipboard()->setText(pending_.text);
        LENS_INFO("selection copied back to the clipboard ({} character(s))", pending_.text.size());
        pending_ = Pending{};
        return;
    }

    if (action != QLatin1String("translate") && action != QLatin1String("explain")) {
        LENS_WARN("unknown action '{}'", action.toStdString());
        return;
    }

    // Both items take the same route on purpose: which channel a request uses was decided
    // when the selection was analysed, so picking one over the other cannot change the
    // answer (UI.md 4.9).
    explain(pending_);
}

void AppController::explain(const Pending& pending)
{
    if (pending.kind != QLatin1String("word")) {
        // Returning in silence made the bar look broken: it hides itself on the tap, so a
        // sentence selection answered the press with nothing happening at all, and the two
        // items read as dead while only copy worked. The bubble already carries a request
        // that failed; it carries this too, and by the same rules -- no status chip, because
        // nothing was explained.
        LENS_INFO("no channel for a '{}' selection; nothing was sent", pending.kind.toStdString());
        showNotice(tr("No word to explain in this selection"), pending);
        return;
    }

    if (const auto cached = store_.cacheGet(pending.lemma.toStdString())) {
        LENS_INFO("cache hit for '{}'; nothing was sent", pending.lemma.toStdString());
        stats_.recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
        store_.save();
        showBubble(pending.lemma, QString::fromStdString(cached->en), QString::fromStdString(cached->zh), pending.anchor);
        emit statsChanged();
        return;
    }

    requestExplanations({pending.lemma});
}

void AppController::requestExplanations(const QStringList& words)
{
#ifdef DEV_SEND_CONFIRM
    LENS_INFO("holding {} word(s) for the dev send confirmation", words.size());
    pendingSend_ = words;
    emit confirmSendRequest(words);
#else
    busyLabel_ = tr("Explaining…");
    emit busyChanged();
    stats_.recordPop(pending_.lemma.toStdString(), nowMinute().toStdString());
    store_.save();
    emit statsChanged();
    llm_.explainWords(words);
#endif
}

void AppController::confirmSend()
{
    if (pendingSend_.isEmpty())
        return;
    const QStringList words = pendingSend_;
    pendingSend_.clear();

    busyLabel_ = tr("Explaining…");
    emit busyChanged();
    stats_.recordPop(pending_.lemma.toStdString(), nowMinute().toStdString());
    store_.save();
    emit statsChanged();
    llm_.explainWords(words);
}

void AppController::cancelSend()
{
    LENS_INFO("the dev send confirmation was declined; nothing left the machine");
    pendingSend_.clear();
}

void AppController::showBubble(const QString& word, const QString& en, const QString& zh, const QPoint& anchor)
{
    // Only the language the reader asked for goes to the surface: UI.md section 4.3's two
    // lines made "explanation language" look like it did nothing, because both were always
    // on screen. The word's own margin is what decides, and a change to it lands on the next
    // bubble rather than on the one already up.
    const bool wantsChinese = store_.explanationLang() == "zh";
    // A word the reader is being asked about is by definition one they have not marked as
    // known, so a fresh bubble is always a new word; only a verdict can change that.
    bubble_ = QVariantMap{{"word", word},
                          {"en", wantsChinese ? QString() : en},
                          {"zh", wantsChinese ? zh : QString()},
                          {"status", store_.isKnown(word.toStdString()) ? QStringLiteral("known") : QStringLiteral("new")},
                          {"x", anchor.x()},
                          {"y", anchor.y()}};
    LENS_INFO("bubble up for '{}'", word.toStdString());
    emit bubbleChanged();
}

void AppController::showNotice(const QString& message, const Pending& pending)
{
    // The title line carries what the reader selected. A word selection names its word; a
    // sentence has no single word to name, and an empty title would leave the card a float of
    // text with nothing tying it back to the selection it is about.
    bubble_ = QVariantMap{{"word", pending.surface.isEmpty() ? pending.text : pending.surface},
                          {"en", QString()},
                          {"zh", message},
                          {"status", QString()},
                          {"x", pending.anchor.x()},
                          {"y", pending.anchor.y()}};
    emit bubbleChanged();
}

void AppController::mark(QString lemma, bool learned)
{
    const std::string key = lemma.toStdString();
    store_.mark(key, learned);
    stats_.recordVerdict(key, nowMinute().toStdString(), learned ? "known" : "new");
    store_.save(); // one write: the mark and the verdict live in the same document

    LENS_INFO("'{}' marked as {}", key, learned ? "known" : "a new word");
    if (bubble_.value("word").toString() == lemma)
        bubble_["status"] = learned ? QStringLiteral("known") : QStringLiteral("new");
    emit bubbleChanged();
    emit statsChanged();
}

void AppController::setAutoScan(bool on)
{
    autoScan_ = on;
    LENS_INFO("automatic scanning {}", on ? "on" : "off");
    emit settingsChanged();
}

void AppController::setLevel(int level)
{
    store_.setLevel(level);
    store_.save();
    LENS_INFO("level set to {}", level);
    emit settingsChanged();
}

void AppController::setExplanationLang(QString lang)
{
    store_.setExplanationLang(lang.toStdString());
    llm_.setExplanationLang(lang);
    store_.save();
    LENS_INFO("explanation language set to '{}'", lang.toStdString());
    emit settingsChanged();
    emit statsChanged(); // the cached explanations behind the words popup changed language
}

void AppController::setTheme(QString theme)
{
    writeDocument("theme", theme);
    emit settingsChanged();
}

void AppController::setUiLanguage(QString lang)
{
    writeDocument("uiLanguage", lang);
    // Order matters, and only this way round works. A few of the values the surfaces read are
    // strings this side builds -- "Today 14:03", "Known", the level group names -- so the
    // payloads have to be asked for again after a language change. Asking before the translator
    // is swapped hands back the same old language, which is exactly what the words popup did:
    // its qsTr labels changed and its rows did not.
    emit uiLanguageChanged(lang);
    emit settingsChanged();
    emit statsChanged();
}

void AppController::setSelectionCapture(bool on)
{
    writeDocument("selectionCapture", on ? QStringLiteral("true") : QStringLiteral("false"));
    LENS_INFO("selection capture {}", on ? "on" : "off");
    emit settingsChanged();
}

void AppController::setApiKey(QString key)
{
    // Write-only: the key goes to disk and nowhere else, and no signal echoes it back.
    writeDocument("API-KEY", key);
    LENS_INFO("API key {}", key.isEmpty() ? "cleared" : "updated");
}

void AppController::bubbleHoverChanged(bool hovering)
{
    // The countdown and its pause are the bubble's own business; this exists so the
    // controller can be told, and so a surface that wants to report the state has somewhere
    // to report it to.
    LENS_TRACE("bubble hover {}", hovering);
}

void AppController::dismissBubble()
{
    if (bubble_.isEmpty())
        return;
    bubble_.clear();
    LENS_DEBUG("bubble dismissed");
    emit bubbleChanged();
}

QVariantMap AppController::bubble() const
{
    return bubble_;
}

QVariantMap AppController::settings() const
{
    return QVariantMap{
        {"level", store_.level()},
        {"levels", levelOptions()},
        {"explanationLang", QString::fromStdString(store_.explanationLang())},
        {"theme", documentString("theme", QStringLiteral("light"))},
        {"uiLanguage", documentString("uiLanguage", QStringLiteral("zh"))},
        {"hasApiKey", !documentString("API-KEY", QString()).isEmpty()},
        {"selectionCapture", documentString("selectionCapture", QStringLiteral("true")) == QLatin1String("true")},
        {"autoScan", autoScan_},
    };
}

QVariantMap AppController::stats() const
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const auto& daily = stats_.daily();

    int todayPops = 0;
    int todayLearned = 0;
    int todayFresh = 0;
    double todayCost = 0.0;

    for (const auto& [date, usage] : daily) {
        if (date != today.toStdString())
            continue;
        todayPops = usage.pops;
        todayLearned = usage.learned;
        todayFresh = usage.fresh;
        todayCost = amountOf(usage);
    }

    // No all-time total here: the statistics panel's last row counts the words the list holds,
    // and that list is deduplicated, so a tally of pops published alongside it was a second
    // number for the same words. The pop count itself is still in daily(), and in todayPops.
    return QVariantMap{{"todayPops", todayPops},
                       {"todayLearned", todayLearned},
                       {"todayFresh", todayFresh},
                       {"todayCost", todayCost},
                       {"currency", currencySymbol(pricing_.currency())}};
}

QPoint AppController::cursorPos() const
{
    // Already device-independent: Qt 6's global screen coordinates are, in the same units the
    // surfaces are placed in. Dividing by the scale factor as though it were the hook's raw
    // pixel report made every drag travel 1/ratio of the way it should.
    return QCursor::pos();
}

QVariantList AppController::words() const
{
    const QDate today = QDate::currentDate();

    // One row per word. The history keeps a line for every pop, so a word shown twice listed
    // twice with the same verdict and the two lines read as a bug rather than as a record.
    // The newest one wins -- the history is newest first, so it is the first one seen. What is
    // dropped is the display of a repeat, not the pop: todayPops still counts both.
    QSet<QString> seen;

    QVariantList out;
    out.reserve(static_cast<qsizetype>(stats_.history().size()));
    for (const core::HistoryEntry& entry : stats_.history()) {
        const QString shown = QString::fromStdString(entry.lemma);
        if (seen.contains(shown))
            continue;
        seen.insert(shown);

        const QDateTime moment = QDateTime::fromString(QString::fromStdString(entry.minute),
                                                       QStringLiteral("yyyy-MM-dd HH:mm"));
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
                               {"status", status}});
    }
    return out;
}

QVariantMap AppController::cost() const
{
    const QDate today = QDate::currentDate();
    const auto& daily = stats_.daily();

    double month = 0.0;
    double todayAmount = 0.0;
    double yesterday = 0.0;
    double week = 0.0;

    for (const auto& [date, usage] : daily) {
        const QDate when = QDate::fromString(QString::fromStdString(date), Qt::ISODate);
        if (!when.isValid())
            continue;

        const double amount = amountOf(usage);
        if (when.year() == today.year() && when.month() == today.month())
            month += amount;
        if (when == today)
            todayAmount = amount;
        if (when.daysTo(today) == 1)
            yesterday = amount;
        // ISO weeks start on Monday, which is what "this week" means on the calendar the
        // reader is looking at.
        if (when.year() == today.year() && when.weekNumber() == today.weekNumber())
            week += amount;
    }

    // Averaged over the days of the month that have happened, not over the days that happen
    // to have a record: a quiet month is not a cheap one.
    const double dailyAverage = today.day() > 0 ? month / today.day() : 0.0;

    return QVariantMap{{"month", month},
                       {"today", todayAmount},
                       {"yesterday", yesterday},
                       {"week", week},
                       {"dailyAverage", dailyAverage},
                       {"currency", currencySymbol(pricing_.currency())}};
}

double AppController::amountOf(const core::DailyUsage& usage) const
{
    return pricing_.cost(llm_.model(), llm::Usage{static_cast<int>(usage.promptTokens), static_cast<int>(usage.completionTokens)});
}

QString AppController::modeLabel() const
{
    // Automatic scanning is a phase-1 placeholder, so this is always the manual name; the
    // branch stays because the tray menu and tooltip both read the label from here and must
    // never disagree (PHASE1.md section 4.4).
    return autoScan_ ? tr("Auto mode") : tr("Manual mode");
}

QString AppController::busyLabel() const
{
    return busyLabel_;
}

std::size_t AppController::minFreqRank() const
{
    return rankForLevel(store_.level());
}

QString AppController::documentString(const char* key, const QString& fallback) const
{
    const nlohmann::json& doc = store_.document();
    if (!doc.is_object() || !doc.contains(key) || !doc[key].is_string())
        return fallback;
    return QString::fromStdString(doc[key].get<std::string>());
}

void AppController::writeDocument(const char* key, const QVariant& value)
{
    store_.document()[key] = value.toString().toStdString();
    store_.save();
}

}
