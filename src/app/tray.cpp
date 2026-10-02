/**
 * @file tray.cpp
 * @brief The tray icon, its menu, and the strings both show.
 */

#include "tray.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QVariantMap>

#include "app_controller.h"
#include "core/log.h"

namespace lens::app {
namespace {

/// Strings go through translate() with both the context and the text spelled out at the
/// call site: lupdate only reads literal arguments, so a helper taking the context as a
/// parameter would extract nothing.
QString tr(const char* text)
{
    return QCoreApplication::translate("lens::app::Tray", text);
}

/// @return The icon for one state, for a taskbar of the given lightness.
/// @note The name suffix says which taskbar the art is for: the glyph is a mid-tone ring and
///       a filled centre, and the grey it uses when switched off has to be dark on a light
///       taskbar and light on a dark one.
QIcon iconFor(const char* state, bool darkTaskbar)
{
    return QIcon(QStringLiteral(":/icons/tray-%1-%2.svg")
                     .arg(QLatin1String(state), QLatin1String(darkTaskbar ? "ondark" : "onlight")));
}

bool taskbarIsDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

} // namespace

Tray::Tray(AppController& controller, QObject* parent)
    : QObject(parent), controller_(controller), icon_(new QSystemTrayIcon(this)),
      menu_(new QMenu())
{
    modeAction_ = menu_->addAction(QString());
    modeAction_->setEnabled(false);
    menu_->addSeparator();

    statsAction_ = menu_->addAction(QString(), this, &Tray::statsRequested);

    languageAction_ = menu_->addAction(QString());
    auto* languages = new QMenu();
    auto* group = new QActionGroup(this);
    for (const auto& entry : {std::pair{QLatin1String("zh"), QString::fromUtf8("中文")},
                              {QLatin1String("en"), QStringLiteral("English")}}) {
        QAction* action = languages->addAction(entry.second);
        action->setCheckable(true);
        action->setData(entry.first);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code = QString(entry.first)] {
            controller_.setUiLanguage(code);
        });
    }
    languageAction_->setMenu(languages);

    settingsAction_ = menu_->addAction(QString(), this, &Tray::settingsRequested);
    menu_->addSeparator();
    quitAction_ = menu_->addAction(QString(), this, &Tray::quitRequested);

    icon_->setContextMenu(menu_);

    connect(icon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context)
            emit geometryChanged();
    });

    connect(&controller_, &AppController::settingsChanged, this, &Tray::refresh);
    connect(&controller_, &AppController::statsChanged, this, &Tray::refresh);
    connect(&controller_, &AppController::busyChanged, this, &Tray::refresh);
    connect(&controller_, &AppController::uiLanguageChanged, this, [this](const QString&) { retranslate(); });

    // The reader can switch Windows between light and dark while the app runs, and the icon
    // is drawn for one of the two.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { refresh(); });

    retranslate();
    refresh();
}

Tray::~Tray()
{
    delete menu_; // owned by us, not by the QSystemTrayIcon
}

bool Tray::show()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        LENS_CRITICAL("no system tray on this desktop; the app has no way to be reached");
        return false;
    }
    // An icon that failed to load leaves the tray showing a blank slot, which reads as a
    // missing app rather than a broken resource path.
    if (icon_->icon().isNull())
        LENS_ERROR("the tray icon did not load; check the :/icons resources");

    icon_->show();
    LENS_INFO("tray icon is up");
    return true;
}

QRect Tray::geometry() const
{
    return icon_->geometry();
}

Tray::State Tray::state() const
{
    if (!controller_.busyLabel().isEmpty())
        return State::Busy;
    return controller_.settings().value("selectionCapture").toBool() ? State::Auto : State::Off;
}

void Tray::refresh()
{
    const State current = state();
    switch (current) {
        case State::Busy:
            icon_->setIcon(iconFor("busy", taskbarIsDark()));
            break;
        case State::Auto:
            icon_->setIcon(iconFor("auto", taskbarIsDark()));
            break;
        case State::Off:
            icon_->setIcon(iconFor("off", taskbarIsDark()));
            break;
    }

    const QVariantMap stats = controller_.stats();
    const QString today = tr("Today %1 words")
                              .arg(QString::number(stats.value("todayPops").toInt()));
    icon_->setToolTip(QStringLiteral("Lens · %1 · %2").arg(controller_.modeLabel(), today));
    // A native menu has no second column, so UI.md 4.2's right-aligned sub-label rides along
    // in the item text instead.
    const QString figures = QStringLiteral("%1 %2 · %3%4")
                                .arg(QString::number(stats.value("todayPops").toInt()), tr("words"), stats.value("currency").toString(), QString::number(stats.value("todayCost").toDouble(), 'f', 2));
    statsAction_->setText(QStringLiteral("%1  ·  %2").arg(tr("Today's statistics"), figures));

    if (current == State::Off) {
        modeAction_->setText(tr("Selection capture is off"));
    } else {
        modeAction_->setText(controller_.modeLabel());
    }
}

void Tray::retranslate()
{
    statsAction_->setText(tr("Today's statistics"));
    languageAction_->setText(tr("Language"));
    settingsAction_->setText(tr("Settings"));
    quitAction_->setText(tr("Quit"));

    language_ = controller_.settings().value("uiLanguage").toString();
    for (QAction* action : languageAction_->menu()->actions())
        action->setChecked(action->data().toString() == language_);

    refresh();
}

}
