#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "core/known_store.h"
#include "core/stats_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "selection_text_grabber.h"

// For the create() factory's signature. Declared here rather than included: the header of a
// class QML_SINGLETON registers should not drag the whole QML engine into every includer.
class QQmlEngine;
class QJSEngine;

namespace lens::app {

class MouseSelectionHook;

/**
 * @file app_controller.h
 * @brief The QML back end: turns a finished selection into a surface, and a surface's
 *        answer back into a request.
 *
 * The pieces exist already -- the mouse hook notices a selection, the grabber borrows the
 * clipboard to copy it out, FilterCore decides whether it is a word. What lives here is the
 * wiring, the decisions that belong to none of them, and the properties the surfaces bind to.
 * Nothing here draws: every value QML shows arrives through one of the read-only properties,
 * and every write comes back through a Q_INVOKABLE.
 *
 * @note Windows only, like the rest of src/app.
 */
class AppController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Controller)
    QML_SINGLETON
public:
    /**
     * @brief Wire the controller to the components it drives.
     * @param store   Settings and word marks. Must outlive this object: the statistics view
     *                shares its document, and saving is the store's job.
     * @param llm     Explanation client; requests leave through it.
     * @param hook    The mouse hook whose selectionReleased() is this class's entry point.
     * @param pricing Price list, for the cost surfaces.
     */
    AppController(core::KnownStore& store, llm::LlmClient& llm, MouseSelectionHook& hook, const llm::Pricing& pricing, QObject* parent = nullptr);
    ~AppController() override;

    /// @brief Hand the QML engine the one instance main() built.
    ///
    /// The surfaces reach this class as the QML singleton Controller, which is what lets a
    /// linter see the properties they read. A singleton cannot be built by the engine -- the
    /// constructor wants the store, the client and the hook, all of which live in main() -- so
    /// the instance is parked here just before the QML loads and create() hands it over.
    static void provide(AppController* instance);

    /// @brief The factory QML_SINGLETON makes the engine call. Returns what provide() was given.
    /// @note Ownership is set back to C++: without that the engine deletes a singleton it did
    ///       not build, which here would be the second delete of a stack object.
    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /**
     * @brief Called when the mouse hook judges a selection finished.
     *
     * Copies the selection out, decides what kind of text it is, and asks the overlay for the
     * action bar -- it does not request an explanation. Which channel a request takes is
     * decided here, before the reader picks between translate and explain, so the pick cannot
     * change the answer.
     *
     * @param anchor Release position in physical screen pixels, straight from the hook.
     */
    void onSelectionReleased(QPoint anchor);

    /**
     * @brief Where the pointer is on screen, in device-independent pixels.
     *
     * For dragging a surface by hand, and it is the only source a drag can use: the translation
     * a QML pointer handler reports is measured *inside* the window, so moving that window
     * changes the translation, and feeding it back diverges -- measured on the real window, a
     * drag at hand speed threw the panel from x=1116 to x=-3688 in a few dozen events.
     */
    Q_INVOKABLE QPoint cursorPos() const;

    /// @brief The action bar's answer: "translate", "explain", or "copy".
    /// @param text  The selection, echoed back by the surface.
    Q_INVOKABLE void runSelectionAction(QString action, QString text);

    /// @brief Record the reader's verdict on a word and persist it.
    Q_INVOKABLE void mark(QString lemma, bool learned);

    /// @brief Turn automatic scanning on or off. A placeholder in phase 1: the settings
    ///        switch and the F8 hotkey are both disabled, so nothing calls this yet.
    Q_INVOKABLE void setAutoScan(bool on);

    Q_INVOKABLE void setLevel(int level);
    Q_INVOKABLE void setExplanationLang(QString lang);
    Q_INVOKABLE void setTheme(QString theme);
    Q_INVOKABLE void setUiLanguage(QString lang);

    /// @brief Turn selection capture on or off. The only trigger phase 1 has, so it is the
    ///        only switch in the settings popup that does something.
    Q_INVOKABLE void setSelectionCapture(bool on);

    /// @brief Store a new API key. Write-only: nothing reads it back to a surface.
    Q_INVOKABLE void setApiKey(QString key);

    /// @brief Set what happens when another process writes the clipboard during a grab.
    /// @param policy "topmost" to raise a notice about it, "silent" to let the gesture pass
    ///        unanswered. Anything else is refused and logged.
    Q_INVOKABLE void setClipboardPolicy(QString policy);

    /// @brief Start Lens with the reader's session, or stop doing so.
    /// @param on Written to the registry under HKCU; see autostart.h.
    Q_INVOKABLE void setAutostart(bool on);

    /// @brief Told by the bubble when the pointer enters or leaves it.
    /// @note The five-second timer lives in QML, which owns the view. This only records the
    ///       state, because the controller has no reason to duplicate the countdown.
    Q_INVOKABLE void bubbleHoverChanged(bool hovering);

    /// @brief Drop the current pop, e.g. when the bubble's timer expires.
    Q_INVOKABLE void dismissBubble();

    /// @return The bubble's contents, or an empty map when no bubble is up.
    QVariantMap bubble() const;

    /**
     * @brief The reason a pop had nothing to explain: {title, body, kind}.
     *
     * The bubble is not an error reporter any more. A request that failed and a selection with
     * nothing to look up both arrive here instead, and no verdict is claimed -- nothing was
     * explained. The keys are defined in notice.h.
     *
     * @return The current notice, or an empty map when there is none. It is cleared by an
     *         explanation going up, by dismissBubble(), and by the next notice replacing it.
     */
    QVariantMap notice() const;

    /// @return Levels, languages, theme, toggles: everything the settings popup binds to.
    QVariantMap settings() const;

    /// @return Today's tallies and the all-time total, for the statistics popup.
    QVariantMap stats() const;

    /// @return The words popup's rows, newest first.
    QVariantList words() const;

    /**
     * @brief The words list as plain text, one lemma per line, for the reader to save.
     *
     * The rows are words()'s, and the scopes are the words popup's own filter. Nothing is
     * picked or written here: the surface takes the path from a file dialog and writes what
     * this returns, which is what keeps the text itself assertable without Qt.
     *
     * @param scope "all", "known", or "new"; anything else is logged and exports nothing.
     * @return The text, or empty when no row matches the scope.
     */
    Q_INVOKABLE QString exportWords(QString scope);

    /// @return The cost popup's figures. Amounts are computed from the current price list and
    ///         the token counts are the stored ones (PRODUCT.md "存储形状").
    QVariantMap cost() const;

    /// @return "Manual" or "Auto", the mode name the tray menu and tooltip share.
    QString modeLabel() const;

    /// @return What the app is doing, or empty when idle. Drives the tray icon's busy state.
    QString busyLabel() const;

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap cost READ cost NOTIFY statsChanged)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)

signals:
    /// @brief Put the action bar up (or move it): {x, y, kind, text}.
    /// @param kind "word" when the selection yields a candidate, "sentence" otherwise. The bar
    ///             draws the same three items either way -- UI.md section 4.9 keeps them
    ///             ungreyed -- but under "sentence" only copy has a channel to send to, and a
    ///             tap on the other two answers with a bubble saying so rather than nothing.
    void selectionBarRequested(QVariantMap payload);

    /// @brief A button went down somewhere on the desktop; forwarded from the mouse hook.
    /// @param at Press position in physical screen pixels, as the hook reports it. The
    ///           surfaces are separate windows, so a press outside one is never delivered to
    ///           this process; this is what lets main.qml close the surface it missed.
    void pointerPressed(QPoint at);

    void bubbleChanged();
    void noticeChanged();
    void settingsChanged();
    void statsChanged();
    void busyChanged();

    /// @brief The interface language changed; the .qm has to be swapped.
    void uiLanguageChanged(QString lang);

private:
    /// @brief What main() handed to provide(); see the note there.
    static AppController* instance_;

    /// @brief What a finished selection turned out to be, held until an action arrives.
    struct Pending {
        QPoint anchor;
        QString text;    ///< The selection as copied, verbatim.
        QString kind;    ///< "word" or "sentence".
        QString surface; ///< The word as written, e.g. "running".
        QString lemma;   ///< Its dictionary form, e.g. "run". Requests and the cache use this.
    };

    /// @brief Copy the selection out and decide what it is; empty when nothing is up.
    void beginSelection(QPoint anchor);

    /// @brief Look the word up: cache first, then the model.
    void explain(const Pending& pending);

    /// @brief Put an explanation up at the pending anchor.
    /// @param ipa Pronunciation in slashes, beside the word rather than behind the language
    ///            switch: UI.md section 4.3 draws it next to the word itself.
    void showBubble(const QString& word, const QString& ipa, const QString& en, const QString& zh, const QPoint& anchor);

    /// @brief Put a notice up where an explanation would have gone: no verdict, just the reason.
    /// @param title What the notice is about; see noticeTitle().
    /// @param body  Reader-facing reason.
    /// @param kind  kNoticeInfo or kNoticeError, from notice.h.
    void showNotice(const QString& title, const QString& body, const QString& kind);

    /// @return What a notice calls the selection it answers: the word when there is one, the
    ///         selection text otherwise. A sentence has no single word to name, and an untitled
    ///         card floats with nothing tying it to the selection it is about.
    static QString noticeTitle(const Pending& pending);

    /// @brief Drop the notice, telling the surface only when there was one.
    void clearNotice();

    /// @brief Drop the bubble, telling the surface only when there was one.
    void clearBubble();

    /// @brief Ask the model for the explanations of a batch of words.
    void requestExplanations(const QStringList& words);

    /// @return What one day's tokens come to at the current price list.
    double amountOf(const core::DailyUsage& usage) const;

    /// @return The difficulty cut-off for the current level, as filterWords takes it.
    std::size_t minFreqRank() const;

    /// @return The key's value from the shared document, or @p fallback.
    QString documentString(const char* key, const QString& fallback) const;

    /// @brief Write a key into the shared document and persist the whole thing.
    void writeDocument(const char* key, const QVariant& value);

    core::KnownStore& store_;
    core::StatsStore stats_;
    llm::LlmClient& llm_;
    MouseSelectionHook& hook_;
    const llm::Pricing& pricing_;
    std::unique_ptr<SelectionTextGrabber> grabber_;

    Pending pending_;    ///< Valid while an action bar is up.
    QVariantMap bubble_; ///< Empty when nothing is up.
    QVariantMap notice_; ///< Empty when there is nothing to report.
    QString busyLabel_;
    bool autoScan_ = false;
};

}
