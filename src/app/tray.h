#pragma once

#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "app/tray_state.h"

class QSystemTrayIcon;
class QQmlEngine; // For the create() factory's signature; see app_controller.h.
class QJSEngine;

/**
 * @file tray.h
 * @brief The tray icon, and nothing else: the menu is a QML surface.
 *
 * The menu was a native QMenu because QML's Qt.labs.platform context menus do not come up on
 * Qt 6.9. That reason still holds, but the cost was a menu that could
 * not be the card UI.md 4.2 draws, so the menu is now one of the surfaces: a frameless window
 * like the action bar and the panels. What is left here is the icon the shell owns.
 */

namespace lens::app {

class AppController;

/**
 * @brief Keeps the tray icon in step with the controller, and says when it was clicked.
 *
 * @note The exhausted budget state is derived from the same CostDuty projection that gates
 *       network dispatch. This class only reports it in the shell.
 */
class Tray : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Tray)
    QML_SINGLETON
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
public:
    /// @param controller Supplies the mode, the day's tallies and the busy state.
    explicit Tray(AppController& controller, QObject* parent = nullptr);
    ~Tray() override;

    /// @brief Hand the QML engine the one instance main() built; see AppController::provide().
    static void provide(Tray* instance);

    /// @brief The factory QML_SINGLETON makes the engine call. Returns what provide() was given.
    static Tray* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /// @brief Put the icon up.
    /// @return False when the shell has no tray; the reason is logged.
    bool show();

    /// @return Where the icon sits on screen, for anchoring the menu and the panels.
    ///
    /// @note The last place the shell reported it, in device-independent pixels, and empty only
    ///       until the icon has been seen once. A live rectangle is empty whenever the taskbar
    ///       is down, and with the taskbar auto-hidden it is down by the time a panel is opened
    ///       from the menu -- which is how every panel was landing on the hard-coded corner
    ///       instead of beside the icon. Measured on this machine: (1313, 816, 32, 48) beside a
    ///       notification area whose left edge is 1641 physical, the 48 being the 60-pixel
    ///       taskbar. Device-independent, so nothing here is divided by the scale factor.
    QRect geometry() const;

signals:
    /// @brief The reader clicked the icon, left or right: put the menu up.
    void menuRequested();
    void geometryChanged();

private:
    /// @brief What main() handed to provide(); see AppController::provide().
    static Tray* instance_;

    /// @brief Recompute the state, the icon and the tooltip.
    void refresh();

    /// @return The state the controller's own values imply; the rule lives in tray_state.h.
    TrayState state() const;

    AppController& controller_;
    QSystemTrayIcon* icon_ = nullptr;
    QTimer budgetRefreshTimer_;
    /// The last non-empty rectangle the shell reported; see the note on geometry().
    mutable QRect lastGeometry_;
};

}
