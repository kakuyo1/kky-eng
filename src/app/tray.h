#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

/**
 * @file tray.h
 * @brief The tray icon and its menu, in widgets rather than QML.
 *
 * The contract picked C++ QSystemTrayIcon + QMenu over QML's Qt.labs.platform, whose context
 * menus do not come up on Qt 6.9 (PHASE1.md section 4.4). The cost is that the menu is a
 * native one and cannot be the rounded card UI.md 4.2 draws; the benefit is a menu that
 * actually appears on Windows.
 */

namespace lens::app {

class AppController;

/**
 * @brief Keeps the tray icon in step with the controller, and routes menu picks back.
 *
 * @note The fourth icon state, an exhausted daily budget, is not reachable in phase 1: the
 *       budget itself is a placeholder (PHASE1.md section 2), so nothing sets it and the
 *       branch is not written. Its art is in icons/ for when the budget lands.
 */
class Tray : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
public:
    /**
     * @brief Build the icon and the menu.
     * @param controller Supplies the mode, the day's tallies, and the language list.
     */
    explicit Tray(AppController& controller, QObject* parent = nullptr);
    ~Tray() override;

    /// @brief Put the icon up.
    /// @return False when the shell has no tray; the reason is logged.
    bool show();

    /// @return Where the icon sits on screen, for anchoring the popups. May be null when the
    ///         shell does not report it, which the caller has to handle.
    QRect geometry() const;

signals:
    void statsRequested();
    void settingsRequested();
    void quitRequested();
    void geometryChanged();

private:
    /// @brief What the icon is saying. See the class note about the missing fourth state.
    enum class State {
        Auto, ///< Selection capture is on and nothing is in flight.
        Busy, ///< An explanation is being fetched.
        Off,  ///< Selection capture is off.
    };

    /// @brief Recompute the state, the icon, the tooltip and the menu labels.
    void refresh();

    /// @brief Rebuild every string, e.g. after the interface language changed.
    void retranslate();

    /// @return The geometry the shell reports, read fresh.
    State state() const;

    AppController& controller_;
    QSystemTrayIcon* icon_ = nullptr;
    QMenu* menu_ = nullptr;
    QAction* modeAction_ = nullptr;
    QAction* statsAction_ = nullptr;
    QAction* languageAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* quitAction_ = nullptr;
    QString language_;
};

}
