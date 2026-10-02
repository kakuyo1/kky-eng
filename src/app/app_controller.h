#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "core/known_store.h"
#include "core/stats_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "selection_text_grabber.h"

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

    /// @brief Told by the bubble when the pointer enters or leaves it.
    /// @note The five-second timer lives in QML, which owns the view. This only records the
    ///       state, because the controller has no reason to duplicate the countdown.
    Q_INVOKABLE void bubbleHoverChanged(bool hovering);

    /// @brief Drop the current bubble, e.g. when its timer expires.
    Q_INVOKABLE void dismissBubble();

    /// @brief Send the pending explanation; DEV_SEND_CONFIRM builds only.
    Q_INVOKABLE void confirmSend();

    /// @brief Drop the pending explanation; DEV_SEND_CONFIRM builds only.
    Q_INVOKABLE void cancelSend();

    /// @return The bubble's contents, or an empty map when no bubble is up.
    QVariantMap bubble() const;

    /// @return Levels, languages, theme, toggles: everything the settings popup binds to.
    QVariantMap settings() const;

    /// @return Today's tallies and the all-time total, for the statistics popup.
    QVariantMap stats() const;

    /// @return The words popup's rows, newest first.
    QVariantList words() const;

    /// @return The cost popup's figures.
    QVariantMap cost() const;

    /// @return "Manual" or "Auto", the mode name the tray menu and tooltip share.
    QString modeLabel() const;

    /// @return What the app is doing, or empty when idle. Drives the tray icon's busy state.
    QString busyLabel() const;

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap cost READ cost NOTIFY statsChanged)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)

signals:
    /// @brief Put the action bar up (or move it): {x, y, kind, text}.
    /// @param kind "word" when the selection yields a candidate, "sentence" otherwise. Phase
    ///             1 has no channel behind "sentence", so the bar's translate and explain
    ///             have nothing to send and only copy does anything.
    void selectionBarRequested(QVariantMap payload);

    /// @brief A button went down somewhere on the desktop; forwarded from the mouse hook.
    /// @param at Press position in physical screen pixels, as the hook reports it. The
    ///           surfaces are separate windows, so a press outside one is never delivered to
    ///           this process; this is what lets main.qml close the surface it missed.
    void pointerPressed(QPoint at);

    void bubbleChanged();
    void settingsChanged();
    void statsChanged();
    void busyChanged();

    /// @brief The interface language changed; the .qm has to be swapped.
    void uiLanguageChanged(QString lang);

    /// @brief DEV_SEND_CONFIRM: ask before this leaves the machine.
    void confirmSendRequest(QStringList words);

private:
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
    void showBubble(const QString& word, const QString& en, const QString& zh, const QPoint& anchor);

    /// @brief Ask the model, honouring DEV_SEND_CONFIRM.
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
    QString busyLabel_;
    bool autoScan_ = false;

    /// DEV_SEND_CONFIRM only: the words waiting for consent.
    QStringList pendingSend_;
};

}
