#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <filesystem>

#include "capture_duty.h"
#include "cost_duty.h"
#include "explanation_duty.h"
#include "global_hotkey.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "mouse_selection_hook.h"
#include "storage_duty.h"
#include "update_duty.h"

class QQmlEngine;
class QJSEngine;

namespace lens::app {

/**
 * @file app_controller.h
 * @brief QML facade that wires the application duties to the surfaces.
 *
 * Capture, explanation, cost and storage behavior live behind their own boundaries. This class
 * keeps the QML singleton contract and forwards commands and projections between those duties.
 */
class AppController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Controller)
    QML_SINGLETON
public:
    /**
     * @brief Wire the QML facade to the application services.
     * @param store Settings and word marks, owned by main().
     * @param llm Explanation client, owned by main().
     * @param hook Desktop selection hook, owned by main().
     * @param hotkey System-wide capture trigger, owned by main().
     * @param pricing Loaded model price list, owned by main().
     * @param dataDir Directory `data/update.json` is read from, the same one `data/llm/` is.
     * @param parent QObject parent.
     */
    AppController(core::KnownStore& store,
                  llm::LlmClient& llm,
                  MouseSelectionHook& hook,
                  GlobalHotkey& hotkey,
                  const llm::Pricing& pricing,
                  const std::filesystem::path& dataDir,
                  QObject* parent = nullptr);
    ~AppController() override;

    /// @brief Hand the QML engine the one instance built by the composition root.
    static void provide(AppController* instance);

    /// @brief Factory used by the QML singleton registration.
    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /**
     * @brief Take the trigger key the reader chose, or the default, on the shell's registry.
     * @return True when the shell holds it. False costs the trigger, not the program, and the
     *         settings page says so.
     * @note Separate from the constructor, and called by main() after the surfaces are up, for the
     *       same reason the mouse hook's install() is: this touches the desktop, and a run that
     *       builds surfaces -- the test tree -- must not take a key away from the machine it runs on.
     */
    bool installHotkey();

    /// @brief Compatibility entry point for a completed selection gesture.
    void onSelectionReleased(QPoint anchor);

    Q_INVOKABLE QPoint cursorPos() const;
    Q_INVOKABLE void runSelectionAction(QString action, QString text);
    Q_INVOKABLE void mark(QString lemma, bool learned);
    Q_INVOKABLE void setAutoScan(bool on);
    Q_INVOKABLE void setOcrCapture(bool on);
    Q_INVOKABLE void setMinimumWordLength(int length);
    Q_INVOKABLE bool setScanWhitelist(QString processes);
    /// @param executable File the reader picked in the system's dialog; C++ takes the local path.
    Q_INVOKABLE void setTesseractExecutable(QUrl executable);
    /// @param directory Folder the reader picked in the system's dialog; C++ takes the local path.
    Q_INVOKABLE void setTesseractDataDirectory(QUrl directory);
    Q_INVOKABLE void restoreCaptureDefaults();
    Q_INVOKABLE void setLevel(int level);
    Q_INVOKABLE void setExplanationLang(QString lang);
    Q_INVOKABLE void setMultiSense(bool on);
    /// @brief Ask for a word's origin with its explanation, and draw it on the bubble.
    Q_INVOKABLE void setEtymology(bool on);
    Q_INVOKABLE void setTheme(QString theme);
    Q_INVOKABLE void setAnimationsEnabled(bool on);
    Q_INVOKABLE void setUiLanguage(QString lang);
    Q_INVOKABLE void setSelectionCapture(bool on);
    Q_INVOKABLE void setApiKey(QString key);
    Q_INVOKABLE void setClipboardPolicy(QString policy);
    Q_INVOKABLE void setProvider(QString provider);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void setModel(QString model);
    Q_INVOKABLE void setApiUrl(QString url);
    Q_INVOKABLE void setAutostart(bool on);
    Q_INVOKABLE void bubbleHoverChanged(bool hovering);
    Q_INVOKABLE void dismissBubble();
    Q_INVOKABLE void dismissNotice();
    /**
     * @brief Record the trigger combination the reader pressed in the settings field.
     * @param key       Qt key code of the key that was not a modifier.
     * @param modifiers The modifiers held when it went down.
     * @return Empty once it is stored and the shell has taken it; otherwise why it was refused, for
     *         the field to say so -- "unusable" (no Ctrl or Alt, so it would take a bare key from
     *         every other program) or "taken" (another program already holds it). A refused choice
     *         leaves the combination that was in force working.
     */
    Q_INVOKABLE QString setOcrHotkey(int key, int modifiers);
    /// @brief Show the explanation already stored for a word the reader pointed at, if there is one.
    /// @param x,y Where the row is, in the physical pixels the mouse hook reports.
    Q_INVOKABLE void reviewWord(QString lemma, int x, int y);
    /// @brief Take down a bubble the word list raised; a bubble from anywhere else is left alone.
    Q_INVOKABLE void dismissReview();
    /// @brief Ask the model about a lemma the reader asked for by name, anchored where it was read.
    Q_INVOKABLE void explainLemma(QString lemma);
    /// @brief Capture a region the reader dragged on the mask.
    /// @return Empty once the capture is under way; otherwise the reason, which is also reported.
    Q_INVOKABLE QString captureRegion(QRect region);
    Q_INVOKABLE QString exportWords(QString scope);
    Q_INVOKABLE bool saveWords(QUrl path, QString scope);
    Q_INVOKABLE bool removeWord(QString lemma);
    Q_INVOKABLE bool setDailyBudget(double amount);
    /// @brief Ask the release source now, and report whatever it really says.
    Q_INVOKABLE void checkForUpdates();
    /// @brief Stop the automatic prompt for the version now on offer.
    Q_INVOKABLE void skipUpdate();
    /// @brief Open the release page in the browser, and take the card down.
    Q_INVOKABLE void openReleasePage();
    /// @brief Take the update card down without changing anything stored.
    Q_INVOKABLE void closeUpdateCard();
    /// @brief Whether the update card is up; the card reads this beside the state.
    Q_INVOKABLE bool updateCardVisible() const;
    /// @brief Switch the automatic startup check on or off; read back by the settings page.
    Q_INVOKABLE void setAutoUpdateCheck(bool on);
    Q_INVOKABLE bool autoUpdateCheck() const;
    /// @brief Run the automatic check if today has not had one; called once at startup.
    void checkForUpdatesAtStartup();

    QVariantMap bubble() const;
    QVariantMap notice() const;
    QVariantMap settings() const;
    /// @brief The models the provider in force carries. Its own property, not a key of settings():
    ///        see ExplanationDuty::models() for why the list is kept out of that map.
    QVariantList models() const;
    QVariantMap stats() const;
    QVariantList words() const;
    /// @brief The last 365 local dates, including zero-use days, for the annual words surface.
    QVariantList yearDays() const;
    QVariantMap cost() const;
    /// @brief The update check's state, versions and release page, as one map.
    QVariantMap update() const;
    QString modeLabel() const;
    QString busyLabel() const;
    double dailyBudget() const;

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)
    Q_PROPERTY(QVariantList yearDays READ yearDays NOTIFY yearDaysChanged)
    Q_PROPERTY(QVariantMap cost READ cost NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap update READ update NOTIFY updateChanged)
    Q_PROPERTY(bool updateCardVisible READ updateCardVisible NOTIFY updateChanged)
    Q_PROPERTY(bool autoUpdateCheck READ autoUpdateCheck NOTIFY updateChanged)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    Q_PROPERTY(double dailyBudget READ dailyBudget NOTIFY dailyBudgetChanged)

signals:
    void selectionBarRequested(QVariantMap payload);
    void pointerPressed(QPoint at);
    /// @brief The trigger key was pressed and OCR can run: put the capture mask up.
    void captureMaskRequested();
    void bubbleChanged();
    void noticeChanged();
    void settingsChanged();
    /// @brief The provider in force carries a different list of models than it did.
    void modelsChanged();
    void statsChanged();
    void yearDaysChanged();
    void busyChanged();
    void uiLanguageChanged(QString lang);
    void dailyBudgetChanged();
    /// @brief The update check's state, or its card's visibility, changed.
    void updateChanged();

private:
    /// @brief Restore provider wire options and persisted connection choices without touching credentials.
    void restoreModelService();
    /// @brief Reconcile budget and in-flight request gates with the capture duty.
    void refreshCaptureGates();
    /// @brief The trigger key went down: raise the mask, or say why it cannot be raised.
    void onTriggerHotkey();
    /// @brief Report a capture that will not start, in the reader's words rather than a code.
    void reportCaptureRefusal(QString const& reason);

    static AppController* instance_;

    StorageDuty storage_;
    CostDuty cost_;
    CaptureDuty capture_;
    ExplanationDuty explanation_;
    update::UpdateClient updateClient_;
    UpdateDuty updateDuty_;
    llm::LlmClient& llm_;
    GlobalHotkey& hotkey_;
    QTimer gateTimer_;
};

}
