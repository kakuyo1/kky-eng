#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class QSystemTrayIcon;

/**
 * @file tray.h
 * @brief The tray icon, and nothing else: the menu is a QML surface.
 *
 * The menu was a native QMenu because QML's Qt.labs.platform context menus do not come up on
 * Qt 6.9 (PHASE1.md section 4.4). That reason still holds, but the cost was a menu that could
 * not be the card UI.md 4.2 draws, so the menu is now one of the surfaces: a frameless window
 * like the action bar and the panels. What is left here is the icon the shell owns.
 */

namespace lens::app {

class AppController;

/**
 * @brief Keeps the tray icon in step with the controller, and says when it was clicked.
 *
 * @note The fourth icon state, an exhausted daily budget, is not reachable in phase 1: the
 *       budget itself is a placeholder (PHASE1.md section 2), so nothing sets it and the
 *       branch is not written. Its art is in icons/ for when the budget lands.
 */
class Tray : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
public:
    /// @param controller Supplies the mode, the day's tallies and the busy state.
    explicit Tray(AppController& controller, QObject* parent = nullptr);
    ~Tray() override;

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
    /// @brief What the icon is saying. See the class note about the missing fourth state.
    enum class State {
        Auto, ///< Selection capture is on and nothing is in flight.
        Busy, ///< An explanation is being fetched.
        Off,  ///< Selection capture is off.
    };

    /// @brief Recompute the state, the icon and the tooltip.
    void refresh();

    /// @return The state the controller's own values imply.
    State state() const;

    AppController& controller_;
    QSystemTrayIcon* icon_ = nullptr;
    /// The last non-empty rectangle the shell reported; see the note on geometry().
    mutable QRect lastGeometry_;
};

}
