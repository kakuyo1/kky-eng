/**
 * @file setup.cpp
 * @brief Build the singletons the surfaces bind to, the way main() builds them.
 *
 * Everything here is the application's own startup minus the three steps a test must not take:
 * the mouse hook is never installed (a WH_MOUSE_LL hook would make every case wait on the
 * pointer and would move it), the OCR trigger key is never registered (a global hotkey would take
 * that combination away from whatever else the machine is doing while the suite runs), and the tray
 * icon is never shown.
 */

#include "setup.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>

namespace {

void installUiFonts()
{
    // LENS_SYSTEM_FONTS is a machine path and arrives from config/paths.json through CMake, the
    // way LENS_DATA_DIR and LENS_QTEST_DIR already do; only the file names belong here. Adjacent
    // literals concatenate in the preprocessor, so this is still one QStringLiteral.
    const int latin          = QFontDatabase::addApplicationFont(QStringLiteral(LENS_SYSTEM_FONTS "/SegUIVar.ttf"));
    const int chinese        = QFontDatabase::addApplicationFont(QStringLiteral(LENS_SYSTEM_FONTS "/msyhl.ttc"));
    const int chineseRegular = QFontDatabase::addApplicationFont(QStringLiteral(LENS_SYSTEM_FONTS "/msyh.ttc"));
    const int noto           = QFontDatabase::addApplicationFont(QStringLiteral(LENS_SYSTEM_FONTS "/NotoSansSC-VF.ttf"));
    const int mono           = QFontDatabase::addApplicationFont(QStringLiteral(LENS_SYSTEM_FONTS "/CascadiaCode.ttf"));
    if (latin < 0 or chinese < 0 or chineseRegular < 0 or mono < 0)
        qFatal("could not load the Windows UI font files for the offscreen snapshot");

    const QString lightFamily   = QFontDatabase::applicationFontFamilies(chinese).value(0);
    const QString regularFamily = QFontDatabase::applicationFontFamilies(chineseRegular).value(0);
    const QString notoFamily    = noto >= 0 ? QFontDatabase::applicationFontFamilies(noto).value(0) : QString{};
    if (lightFamily.isEmpty() or regularFamily.isEmpty())
        qFatal("could not resolve the Windows Chinese font families for the offscreen snapshot");

    QFont uiFont;
    const QString mode = qEnvironmentVariable("LENS_QA_FONT_MODE", "current");
    if (mode == QLatin1String("yahei-light"))
        uiFont.setFamilies({lightFamily, QStringLiteral("Segoe UI Variable")});
    else if (mode == QLatin1String("yahei"))
        uiFont.setFamilies({regularFamily, QStringLiteral("Segoe UI Variable")});
    else if (mode == QLatin1String("noto-light") and not notoFamily.isEmpty()) {
        uiFont.setFamilies({notoFamily, QStringLiteral("Segoe UI Variable")});
        uiFont.setWeight(QFont::Light);
    } else if (mode == QLatin1String("noto") and not notoFamily.isEmpty())
        uiFont.setFamilies({notoFamily, QStringLiteral("Segoe UI Variable")});
    else if (not notoFamily.isEmpty())
        uiFont.setFamilies({notoFamily, QStringLiteral("Segoe UI Variable")});
    else
        uiFont.setFamilies({QStringLiteral("Segoe UI Variable"), QStringLiteral("Microsoft YaHei UI Light")});
    QGuiApplication::setFont(uiFont);
}

} // namespace

#ifdef LENS_QTEST_SINGLETONS

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlEngine>
#include <QTimer>

#include <filesystem>
#include <memory>

#include "app/app_controller.h"
#include "app/global_hotkey.h"
#include "app/mouse_selection_hook.h"
#include "app/tray.h"
#include "core/known_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"

namespace {

using lens::app::AppController;
using lens::app::GlobalHotkey;
using lens::app::MouseSelectionHook;
using lens::app::Tray;
using lens::core::KnownStore;

/// @brief The reply every request gets: a failure, delivered on the next event loop turn.
class OfflineReply final : public QNetworkReply {
public:
    explicit OfflineReply(QObject* parent)
        : QNetworkReply(parent)
    {
        setError(QNetworkReply::HostNotFoundError, QStringLiteral("this target is offline"));
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit finished();
        });
    }
    void abort() override
    {
    }
    qint64 readData(char*, qint64) override
    {
        return -1;
    }
};

/// @brief A manager that answers nothing, so this target never reaches the network.
///
/// Not a stub of some answer: every request fails at once, which is what "this machine has no
/// network" looks like to LlmClient -- the model-list fetch warns and leaves the caches alone.
class OfflineManager final : public QNetworkAccessManager {
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest&, QIODevice*) override
    {
        return new OfflineReply(this);
    }
};

/// The singletons one run hands to every engine it builds.
///
/// One instance for the process rather than one per test file: QuickTest builds a fresh engine
/// per file and each of them wants the same Controller, while AppController::create() returns
/// a pointer the engine is told not to own -- so whatever stands behind it has to outlive all
/// of them. Built on first use and never torn down; the process is the test run.
struct Singletons {
    Singletons()             = default;
    Singletons(Singletons&&) = default; ///< What lets the lambda below hand the built one out.
    ~Singletons()
    {
        QFile::remove(settingsPath);
    }

    QString settingsPath;
    std::unique_ptr<KnownStore> store;
    std::unique_ptr<lens::llm::LlmClient> llm;
    std::unique_ptr<MouseSelectionHook> hook;
    std::unique_ptr<GlobalHotkey> hotkey;
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
        built.store        = std::make_unique<KnownStore>(KnownStore::load(std::filesystem::path(built.settingsPath.toStdString())));
        // No key and no URL, and a manager that refuses everything: nothing in this target asks
        // the model anything, and a config that could reach the network is one a case could
        // accidentally spend money through. The manager is not belt-and-braces -- an empty key no
        // longer keeps this target off the net, because the model list comes from one
        // unauthenticated source (docs/adr/0020), so a startup refresh really did reach
        // openrouter.ai on every run, and the model that then followed the service was that
        // slice's first id rather than the catalog seed the cases assert. The model is named
        // anyway, the way main() defaults one -- AppController prices every cost row with it, and
        // a client built from a default Config reports an empty model, which made each of those
        // calls warn that it had no rate: 280 lines a run, none of them news.
        static OfflineManager offline;
        built.llm = std::make_unique<lens::llm::LlmClient>(
            lens::llm::Config{QUrl{}, QString{}, QStringLiteral("deepseek-flash")}, &offline);
        built.hook    = std::make_unique<MouseSelectionHook>();
        built.hotkey  = std::make_unique<GlobalHotkey>();
        built.pricing = std::make_unique<lens::llm::Pricing>(lens::llm::Pricing::load(std::filesystem::path(LENS_DATA_DIR) / "llm" / "pricing.json"));
        for (auto const channel : {lens::llm::Channel::Word, lens::llm::Channel::Entity, lens::llm::Channel::Sentence})
            lens::llm::loadLlmProtocol(channel, std::filesystem::path(LENS_DATA_DIR) / "llm");
        built.controller = std::make_unique<AppController>(*built.store, *built.llm, *built.hook, *built.hotkey, *built.pricing, std::filesystem::path(LENS_DATA_DIR));
        built.tray       = std::make_unique<Tray>(*built.controller);
        return built;
    }();
    return one;
}

} // namespace

void LensTestSetup::qmlEngineAvailable(QQmlEngine* engine)
{
    installUiFonts();

    engine->rootContext()->setContextProperty("lensQaSnapshotDir", qEnvironmentVariable("LENS_QA_SNAPSHOT_DIR"));

    Singletons& one = singletons();
    AppController::provide(one.controller.get());
    Tray::provide(one.tray.get());
}

#else

void LensTestSetup::qmlEngineAvailable(QQmlEngine* engine)
{
    installUiFonts();
    engine->rootContext()->setContextProperty("lensQaSnapshotDir", qEnvironmentVariable("LENS_QA_SNAPSHOT_DIR"));
}

#endif
