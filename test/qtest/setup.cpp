/**
 * @file setup.cpp
 * @brief Build the singletons the surfaces bind to, the way main() builds them.
 *
 * Everything here is the application's own startup minus the two steps a test must not take:
 * the mouse hook is never installed (a WH_MOUSE_LL hook would make every case wait on the
 * pointer and would move it), and the tray icon is never shown.
 */

#include "setup.h"

#ifdef LENS_QTEST_SINGLETONS

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QQmlEngine>

#include <filesystem>
#include <memory>

#include "app/app_controller.h"
#include "app/mouse_selection_hook.h"
#include "app/tray.h"
#include "core/known_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"

namespace {

using lens::app::AppController;
using lens::app::MouseSelectionHook;
using lens::app::Tray;
using lens::core::KnownStore;

/// The singletons one run hands to every engine it builds.
///
/// One instance for the process rather than one per test file: QuickTest builds a fresh engine
/// per file and each of them wants the same Controller, while AppController::create() returns
/// a pointer the engine is told not to own -- so whatever stands behind it has to outlive all
/// of them. Built on first use and never torn down; the process is the test run.
struct Singletons {
    Singletons() = default;
    Singletons(Singletons&&) = default; ///< What lets the lambda below hand the built one out.
    ~Singletons()
    {
        QFile::remove(settingsPath);
    }

    QString settingsPath;
    std::unique_ptr<KnownStore> store;
    std::unique_ptr<lens::llm::LlmClient> llm;
    std::unique_ptr<MouseSelectionHook> hook;
    std::unique_ptr<lens::llm::Pricing> pricing;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<Tray> tray;
};

/**
 * @brief Copy the checked-in settings fixture somewhere writable, and return that path.
 *
 * The cases run against a fixture rather than the developer's settings.local.json twice over:
 * an assertion has to see the same values on every machine, and the settings popup's setters
 * persist -- pointed at the repository's copy, one case that switched the theme would rewrite
 * a tracked file.
 */
QString writableSettingsCopy()
{
    const QString target = QDir::temp().filePath(
        QStringLiteral("lens-qtest-settings-%1.json").arg(QCoreApplication::applicationPid()));

    QFile::remove(target);
    if (!QFile::copy(QStringLiteral(LENS_SETTINGS_FIXTURE), target))
        qFatal("could not copy %s to %s", LENS_SETTINGS_FIXTURE, qPrintable(target));

    return target;
}

Singletons& singletons()
{
    static Singletons one = [] {
        Singletons built;
        built.settingsPath = writableSettingsCopy();
        built.store = std::make_unique<KnownStore>(KnownStore::load(std::filesystem::path(built.settingsPath.toStdString())));
        // No key and no URL: nothing in this target asks the model anything, and a config that
        // could reach the network is one a case could accidentally spend money through.
        built.llm = std::make_unique<lens::llm::LlmClient>(lens::llm::Config{});
        built.hook = std::make_unique<MouseSelectionHook>();
        built.pricing = std::make_unique<lens::llm::Pricing>(lens::llm::Pricing::load(std::filesystem::path(LENS_DATA_DIR) / "llm" / "pricing.json"));
        built.controller = std::make_unique<AppController>(*built.store, *built.llm, *built.hook, *built.pricing);
        built.tray = std::make_unique<Tray>(*built.controller);
        return built;
    }();
    return one;
}

} // namespace

void LensTestSetup::qmlEngineAvailable(QQmlEngine* engine)
{
    Q_UNUSED(engine)

    Singletons& one = singletons();
    AppController::provide(one.controller.get());
    Tray::provide(one.tray.get());
}

#else

void LensTestSetup::qmlEngineAvailable(QQmlEngine* engine)
{
    Q_UNUSED(engine)
}

#endif
