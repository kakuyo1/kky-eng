#pragma once

#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "capture_duty.h"
#include "cost_duty.h"
#include "explanation_duty.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "mouse_selection_hook.h"
#include "storage_duty.h"

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
     * @param pricing Loaded model price list, owned by main().
     * @param parent QObject parent.
     */
    AppController(core::KnownStore& store,
                  llm::LlmClient& llm,
                  MouseSelectionHook& hook,
                  const llm::Pricing& pricing,
                  QObject* parent = nullptr);
    ~AppController() override;

    /// @brief Hand the QML engine the one instance built by the composition root.
    static void provide(AppController* instance);

    /// @brief Factory used by the QML singleton registration.
    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /// @brief Compatibility entry point for a completed selection gesture.
    void onSelectionReleased(QPoint anchor);

    Q_INVOKABLE QPoint cursorPos() const;
    Q_INVOKABLE void runSelectionAction(QString action, QString text);
    Q_INVOKABLE void mark(QString lemma, bool learned);
    Q_INVOKABLE void setAutoScan(bool on);
    Q_INVOKABLE void setOcrCapture(bool on);
    Q_INVOKABLE void setMinimumWordLength(int length);
    Q_INVOKABLE void setDragSensitivity(QString sensitivity);
    Q_INVOKABLE bool setScanWhitelist(QString processes);
    Q_INVOKABLE void restoreCaptureDefaults();
    Q_INVOKABLE bool captureScreen();
    Q_INVOKABLE void setLevel(int level);
    Q_INVOKABLE void setExplanationLang(QString lang);
    Q_INVOKABLE void setMultiSense(bool on);
    Q_INVOKABLE void setTheme(QString theme);
    Q_INVOKABLE void setUiLanguage(QString lang);
    Q_INVOKABLE void setSelectionCapture(bool on);
    Q_INVOKABLE void setApiKey(QString key);
    Q_INVOKABLE void setClipboardPolicy(QString policy);
    Q_INVOKABLE void setPopupFrequency(QString frequency);
    Q_INVOKABLE void setProvider(QString provider);
    Q_INVOKABLE void setModel(QString model);
    Q_INVOKABLE void setApiUrl(QString url);
    Q_INVOKABLE void setAutostart(bool on);
    Q_INVOKABLE void bubbleHoverChanged(bool hovering);
    Q_INVOKABLE void dismissBubble();
    Q_INVOKABLE void dismissNotice();
    Q_INVOKABLE QString exportWords(QString scope);
    Q_INVOKABLE bool saveWords(QUrl path, QString scope);
    Q_INVOKABLE bool removeWord(QString lemma);
    Q_INVOKABLE bool setDailyBudget(double amount);

    QVariantMap bubble() const;
    QVariantMap notice() const;
    QVariantMap settings() const;
    QVariantMap stats() const;
    QVariantList words() const;
    QVariantMap cost() const;
    QString modeLabel() const;
    QString busyLabel() const;
    double dailyBudget() const;

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap cost READ cost NOTIFY statsChanged)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    Q_PROPERTY(double dailyBudget READ dailyBudget NOTIFY dailyBudgetChanged)

signals:
    void selectionBarRequested(QVariantMap payload);
    void pointerPressed(QPoint at);
    void bubbleChanged();
    void noticeChanged();
    void settingsChanged();
    void statsChanged();
    void busyChanged();
    void uiLanguageChanged(QString lang);
    void dailyBudgetChanged();

private:
    /// @brief Restore provider wire options and persisted connection choices without touching credentials.
    void restoreModelService();
    /// @brief Reconcile budget and in-flight request gates with the capture duty.
    void refreshCaptureGates();

    static AppController* instance_;

    StorageDuty storage_;
    CostDuty cost_;
    CaptureDuty capture_;
    ExplanationDuty explanation_;
    llm::LlmClient& llm_;
    QTimer gateTimer_;
};

}
