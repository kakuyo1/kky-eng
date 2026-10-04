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
#include <QHash>
#include <QJSEngine>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include "autostart.h"
#include "core/filter_core.h"
#include "core/log.h"
#include "mouse_selection_hook.h"
#include "notice.h"

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

    connect(&llm_, &llm::LlmClient::batchFinished, this, [this](const QVector<llm::Explanation>& results, llm::Usage usage) {
        busyLabel_.clear();
        emit busyChanged();

        stats_.recordUsage(nowMinute().toStdString(), usage.promptTokens, usage.completionTokens);
        if (results.isEmpty()) {
            LENS_WARN("the model returned no explanation");
            store_.save(); // the usage still counts, even with nothing to show
            emit statsChanged();
            return;
        }

        const llm::Explanation& first = results.front();
        const bool isWord = pending_.kind == QLatin1String("word");
        if (isWord)
            store_.cachePut(first.title.toStdString(), {first.ipa.toStdString(), first.en.toStdString(), first.zh.toStdString()});
        store_.save();
        showBubble(first.title,
                   pending_.kind,
                   isWord ? first.ipa : QString(),
                   first.en,
                   first.zh,
                   pending_.anchor);
        emit statsChanged();
    });

    connect(&llm_, &llm::LlmClient::failed, this, [this](const QString& message) {
        busyLabel_.clear();
        emit busyChanged();
        // A request that fails silently reads as a broken app, so the reason goes up as a
        // notice -- a surface of its own, carrying no verdict, because nothing was explained.
        showNotice(noticeTitle(pending_), message, kNoticeError);
    });

    llm_.setExplanationLang(QString::fromStdString(store_.explanationLang()));
    LENS_INFO("AppController ready: level={} explanation language '{}'",
              store_.level(),
              store_.explanationLang());
}

AppController::~AppController() = default;

// The QML singleton. The surfaces name this class as Controller, which is what lets a linter
// see the properties they bind to; see the note on provide().
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
        // Expected platform outcomes -- the reader's own surface, a terminal, a busy clipboard,
        // unreadable text. These drop the selection silently: a popup on every miss reads as a
        // nag, so the action bar simply never appears.
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return;
    }

    const GrabbedText& captured = std::get<GrabbedText>(grabbed);
    // Another process wrote the clipboard while the selection was being read, so the snapshot
    // was left in place and the reader's own clipboard is gone. The text is still the
    // selection, and whether that interrupts the pop is the reader's call: topmost (the
    // default) carries on -- the surfaces raise themselves on the way up, which is the point
    // of the name -- and silent drops this one without a surface.
    if (captured.clipboardReplaced && documentString("clipboardPolicy", QStringLiteral("topmost")) == QLatin1String("silent")) {
        LENS_INFO("the clipboard was rewritten during the grab; the policy is silent, so the selection is dropped");
        return;
    }

    const QString text = captured.text;
    // What the selection is gets decided here, once, and the two items on the bar only route
    // on the answer. That ordering is the specification, not a convenience: translate and
    // explain are two ways into the same decision (UI.md section 4.9).
    const core::Selection selection = core::classifySelection(text.toStdString(), store_.known(), minFreqRank());

    pending_ = Pending{anchor, text, QString(), QString(), QString(), QStringLiteral("default")};
    switch (selection.kind) {
        case core::SelectionKind::Entity:
            pending_.kind = QStringLiteral("entity");
            LENS_INFO("selection of {} character(s): entity channel", text.size());
            break;
        case core::SelectionKind::Sentence:
            pending_.kind = QStringLiteral("sentence");
            LENS_INFO("selection of {} character(s): sentence channel", text.size());
            break;
        case core::SelectionKind::Word: {
            // The bar carries one word, and it is the first candidate still new to the reader:
            // the list holds every word the excerpt had, the articles and the marked-known
            // ones among them, and sending the article a sentence opens with would be
            // nonsense. When every candidate is known or mastered the first one stands in --
            // a word the reader selected by hand is one they want explained, whatever they
            // once marked (TODO.md item 0).
            const auto& candidates = selection.candidates;
            const auto fresh = std::find_if(candidates.begin(), candidates.end(), [](const core::Candidate& candidate) {
                return candidate.state == core::CandidateState::New;
            });
            const core::Candidate& word = fresh != candidates.end() ? *fresh : candidates.front();

            pending_.kind = QStringLiteral("word");
            pending_.surface = QString::fromStdString(word.surface);
            pending_.lemma = QString::fromStdString(word.lemma);
            LENS_INFO("selection of {} character(s): word channel, '{}' -> '{}'", text.size(), pending_.surface.toStdString(), pending_.lemma.toStdString());
            break;
        }
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

    // Both items take the same route on purpose. The selection kind was decided before the bar
    // appeared; only the sentence channel uses the action to choose a prompt preset.
    if (pending_.kind == QLatin1String("sentence"))
        pending_.preset = action;
    explain(pending_);
}

void AppController::explain(const Pending& pending)
{
    if (pending.kind == QLatin1String("word")) {
        if (const auto cached = store_.cacheGet(pending.lemma.toStdString())) {
            LENS_INFO("cache hit for '{}'; nothing was sent", pending.lemma.toStdString());
            stats_.recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
            store_.save();
            showBubble(pending.lemma,
                       pending.kind,
                       QString::fromStdString(cached->ipa),
                       QString::fromStdString(cached->en),
                       QString::fromStdString(cached->zh),
                       pending.anchor);
            emit statsChanged();
            return;
        }
    }

    const llm::Channel channel = pending.kind == QLatin1String("entity")     ? llm::Channel::Entity
                                 : pending.kind == QLatin1String("sentence") ? llm::Channel::Sentence
                                                                             : llm::Channel::Word;
    llm_.setChannel(channel);
    llm_.setPreset(pending.preset);
    requestExplanations({pending.kind == QLatin1String("word") ? pending.lemma : pending.text});
}

void AppController::requestExplanations(const QStringList& words)
{
    // Only the terms themselves leave the machine: hard-filtered by FilterCore, and masked
    // once more in the request builder (PHASE1.md section 6).
    busyLabel_ = tr("Explaining…");
    emit busyChanged();
    if (pending_.kind == QLatin1String("word"))
        stats_.recordPop(pending_.lemma.toStdString(), nowMinute().toStdString());
    store_.save();
    emit statsChanged();
    llm_.explainWords(words);
}

void AppController::showBubble(const QString& title,
                               const QString& type,
                               const QString& ipa,
                               const QString& en,
                               const QString& zh,
                               const QPoint& anchor)
{
    // Only the language the reader asked for goes to the surface: UI.md section 4.3's two
    // lines made "explanation language" look like it did nothing, because both were always
    // on screen. The word's own margin is what decides, and a change to it lands on the next
    // bubble rather than on the one already up. The pronunciation is not a definition, so it
    // goes out whichever language is chosen.
    const bool wantsChinese = store_.explanationLang() == "zh";
    QVariantMap payload{{"title", title},
                        {"type", type},
                        {"en", wantsChinese ? QString() : en},
                        {"zh", wantsChinese ? zh : QString()},
                        {"x", anchor.x()},
                        {"y", anchor.y()}};
    if (type == QLatin1String("word")) {
        // The mark is read here rather than carried in from the selection: a word selected by
        // hand can be one the reader marked known long ago, and the chip says so from the start
        // rather than only after the next verdict.
        payload.insert(QStringLiteral("ipa"), ipa);
        payload.insert(QStringLiteral("status"), store_.isKnown(title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    bubble_ = payload;
    // An explanation answers whatever the last notice was about, so the two never sit side by
    // side saying different things.
    clearNotice();
    LENS_INFO("bubble up for '{}' on '{}' channel", title.toStdString(), type.toStdString());
    emit bubbleChanged();
}

void AppController::showNotice(const QString& title, const QString& body, const QString& kind)
{
    // One pop at a time, the way an explanation also takes the place of a notice: the two
    // answer different gestures, and a stale card beside a fresh one reads as a stuck surface.
    clearBubble();
    notice_ = noticePayload(title, body, kind);
    LENS_INFO("notice ({}): '{}'", kind.toStdString(), title.toStdString());
    emit noticeChanged();
}

QString AppController::noticeTitle(const Pending& pending)
{
    // The word when the selection had one, the selection itself otherwise. A sentence has no
    // single word to name, and an untitled card floats with nothing tying it to what it is
    // about.
    return pending.surface.isEmpty() ? pending.text : pending.surface;
}

void AppController::clearNotice()
{
    if (notice_.isEmpty())
        return;
    notice_.clear();
    emit noticeChanged();
}

void AppController::clearBubble()
{
    if (bubble_.isEmpty())
        return;
    bubble_.clear();
    emit bubbleChanged();
}

void AppController::mark(QString lemma, bool learned)
{
    const std::string key = lemma.toStdString();
    if (!bubble_.isEmpty() && bubble_.value("type").toString() != QLatin1String("word") &&
        bubble_.value("title").toString() == lemma) {
        LENS_WARN("ignored a verdict for a non-word bubble title");
        return;
    }
    store_.mark(key, learned);
    stats_.recordVerdict(key, nowMinute().toStdString(), learned ? "known" : "new");
    store_.save(); // one write: the mark and the verdict live in the same document

    LENS_INFO("'{}' marked as {}", key, learned ? "known" : "a new word");
    if (bubble_.value("title").toString() == lemma)
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

void AppController::setClipboardPolicy(QString policy)
{
    // Only the two values mean anything. Anything else would sit in the document behaving as
    // topmost -- a setting that silently does not exist -- so it is refused here.
    if (policy != QLatin1String("topmost") && policy != QLatin1String("silent")) {
        LENS_WARN("unknown clipboard policy '{}'; leaving the setting alone", policy.toStdString());
        return;
    }
    writeDocument("clipboardPolicy", policy);
    LENS_INFO("clipboard policy set to '{}'", policy.toStdString());
    emit settingsChanged();
}

void AppController::setAutostart(bool on)
{
    // The registry is the state, so the surface is told to read it back rather than promised
    // the change landed: a policy that forbids the Run key leaves the switch off.
    writeAutostart(on);
    emit settingsChanged();
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
    clearBubble();
    LENS_DEBUG("the current explanation was dismissed");
}

void AppController::dismissNotice()
{
    if (notice_.isEmpty())
        return;
    clearNotice();
    LENS_DEBUG("the current notice was dismissed");
}

QVariantMap AppController::bubble() const
{
    return bubble_;
}

QVariantMap AppController::notice() const
{
    return notice_;
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
        {"clipboardPolicy", documentString("clipboardPolicy", QStringLiteral("topmost"))},
        {"autostart", autostartEnabled()},
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

    // How many times each lemma was popped, which the row carries beside it. Counted out of
    // the history rather than stored: the stored shape is a locked decision (PRODUCT.md
    // "存储形状"), and the per-day tally is the only count it keeps. It needs its own pass --
    // a row is written at its lemma's newest entry, before the older ones have been seen.
    // ponytail: the history holds the newest 2000 entries (PHASE1.md section 4.2), so these
    // counts saturate at whatever the window still holds once it starts truncating.
    QHash<QString, int> pops;
    for (const core::HistoryEntry& entry : stats_.history())
        ++pops[QString::fromStdString(entry.lemma)];

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
        return QString();
    }

    const QString text = QString::fromStdString(core::exportLemmas(stats_.history(), which));
    if (text.isEmpty())
        // Not a fault: a scope can match no row, and "nothing to export" is the right answer
        // to report either way.
        LENS_INFO("words export for '{}': no row matches", scope.toStdString());
    else
        LENS_INFO("words export for '{}': {} character(s) ready", scope.toStdString(), text.size());
    return text;
}

bool AppController::saveWords(QUrl path, QString scope)
{
    // The scope is checked before the file is opened, not by the empty string exportWords()
    // returns for one: an unknown scope would otherwise leave an empty file where the reader
    // asked for words.
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
    // Not QIODevice::Text: that flag rewrites the line endings to the platform's, and the
    // export's shape is LF wherever it is written.
    file.write(text.toUtf8());
    if (!file.commit()) {
        LENS_WARN("words export failed to write '{}'", local.toStdString());
        return false;
    }

    LENS_INFO("words export for '{}': {} character(s) written to '{}'",
              scope.toStdString(),
              text.size(),
              local.toStdString());
    return true;
}

QVariantMap AppController::cost() const
{
    const QDate today = QDate::currentDate();
    const auto& daily = stats_.daily();

    double month = 0.0;
    double todayAmount = 0.0;
    double yesterday = 0.0;
    double week = 0.0;

    // The tokens the amounts were priced from, per bucket, so a surface can show what was
    // bought as well as what it cost. Tokens are what is stored and the amounts are what the
    // current price list makes of them (PRODUCT.md "存储形状"), so the two travel together.
    long long monthTokens = 0;
    long long todayTokens = 0;
    long long yesterdayTokens = 0;
    long long weekTokens = 0;

    for (const auto& [date, usage] : daily) {
        const QDate when = QDate::fromString(QString::fromStdString(date), Qt::ISODate);
        if (!when.isValid())
            continue;

        const double amount = amountOf(usage);
        const long long tokens = usage.promptTokens + usage.completionTokens;
        if (when.year() == today.year() && when.month() == today.month()) {
            month += amount;
            monthTokens += tokens;
        }
        if (when == today) {
            todayAmount = amount;
            todayTokens = tokens;
        }
        if (when.daysTo(today) == 1) {
            yesterday = amount;
            yesterdayTokens = tokens;
        }
        // ISO weeks start on Monday, which is what "this week" means on the calendar the
        // reader is looking at.
        if (when.year() == today.year() && when.weekNumber() == today.weekNumber()) {
            week += amount;
            weekTokens += tokens;
        }
    }

    // Averaged over the days of the month that have happened, not over the days that happen
    // to have a record: a quiet month is not a cheap one.
    const double dailyAverage = today.day() > 0 ? month / today.day() : 0.0;

    return QVariantMap{{"month", month},
                       {"today", todayAmount},
                       {"yesterday", yesterday},
                       {"week", week},
                       {"dailyAverage", dailyAverage},
                       {"monthTokens", monthTokens},
                       {"todayTokens", todayTokens},
                       {"yesterdayTokens", yesterdayTokens},
                       {"weekTokens", weekTokens},
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
    return autoScan_ ? tr("Auto") : tr("Manual");
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
