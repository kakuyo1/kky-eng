/**
 * @file main.cpp
 * @brief The application: load the data the pipeline needs, wire the pieces, put the tray up.
 *
 * There is no main window. The QML root is a window that is never shown; the surfaces are
 * windows of their own, and the only thing the reader can see when nothing is happening is
 * the tray icon.
 */

#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTranslator>
#include <QUrl>

#include <filesystem>
#include <memory>
#include <stdexcept>

#include "app_controller.h"
#include "core/filter_core.h"
#include "core/known_store.h"
#include "core/log.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "llm/llm_protocol.h"
#include "llm/qt_log.h"
#include "mouse_selection_hook.h"
#include "tray.h"

using lens::app::AppController;
using lens::app::MouseSelectionHook;
using lens::app::Tray;
using lens::core::KnownStore;

namespace {

/**
 * Where the app reads its data from.
 *
 * ponytail: baked in at configure time, which is right for a development build and wrong for
 * a shipped one -- the data files and the word list will have to move into a Qt resource
 * before this can be handed to anyone.
 */
constexpr const char* kDataDir = LENS_DATA_DIR;
constexpr const char* kSettingsPath = LENS_SETTINGS_PATH;

/// @brief The setting's language code, and the locale whose .qm carries it.
///
/// The two are not the same string: the setting holds "zh", while the file is named for the
/// locale, lens_zh_CN.qm. Building the file name straight from the setting would look for
/// lens_zh.qm, which does not exist.
const char* translationFileFor(const QString& language)
{
    if (language == QLatin1String("zh"))
        return "zh_CN";
    if (language == QLatin1String("en"))
        return "en_US";
    return nullptr;
}

/// @return A translator for one of the .qm files built into the binary, or a null one.
std::unique_ptr<QTranslator> loadTranslation(const QString& language)
{
    const char* file = translationFileFor(language);
    if (file == nullptr) {
        LENS_WARN("'{}' names no translation file; the interface stays in English",
                  language.toStdString());
        return {};
    }

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/lens_%1.qm").arg(QLatin1String(file)))) {
        LENS_WARN("lens_{}.qm did not load; the interface stays in English", file);
        return {};
    }
    return translator;
}

/// @brief Keep exactly one interface translation installed.
class TranslationKeeper {
public:
    void select(const QString& language)
    {
        if (current_ == language)
            return;
        if (translator_)
            QCoreApplication::removeTranslator(translator_.get());
        translator_ = loadTranslation(language);
        if (translator_)
            QCoreApplication::installTranslator(translator_.get());
        current_ = language;
        LENS_INFO("interface language is now '{}'", language.toStdString());
    }

private:
    std::unique_ptr<QTranslator> translator_;
    QString current_;
};

std::string settingsValue(const KnownStore& store, const char* key)
{
    const nlohmann::json& doc = store.document();
    if (!doc.is_object() || !doc.contains(key) || !doc[key].is_string())
        return {};
    return doc[key].get<std::string>();
}

}

int main(int argc, char* argv[])
{
    // Widgets, not just Gui: the tray menu is a QMenu, which the contract chose over QML's
    // experimental platform menu types (PHASE1.md section 4.4).
    QApplication app(argc, argv);
    // A tray application has no window to keep open, so the default rule would quit as soon
    // as the first surface closed.
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Lens"));

    lens::log::init();
    lens::log::installQtMessageHandler();

    try {
        lens::core::loadWordlist(std::filesystem::path(kDataDir) / "wordlist.txt");
        lens::core::loadIrregulars(std::filesystem::path(kDataDir) / "irregulars.tsv");
        lens::llm::loadLlmProtocol(lens::llm::Channel::Word, std::filesystem::path(kDataDir) / "llm");
    } catch (const std::exception& e) {
        // Without the word list there is no pipeline at all, and without the protocol there is
        // nothing to send. Neither failure is recoverable at runtime.
        LENS_CRITICAL("startup failed: {}", e.what());
        return 1;
    }

    KnownStore store = KnownStore::load(std::filesystem::path(kSettingsPath));

    const auto pricing = [&] {
        try {
            return lens::llm::Pricing::load(std::filesystem::path(kDataDir) / "llm" / "pricing.json");
        } catch (const std::exception& e) {
            // A missing price list costs the cost surfaces their figures and nothing else.
            LENS_ERROR("costs will read as zero: {}", e.what());
            return lens::llm::Pricing{};
        }
    }();

    const QString apiKey = QString::fromStdString(settingsValue(store, "API-KEY"));
    const QString baseUrl = QString::fromStdString(settingsValue(store, "URL"));
    const QString model = QString::fromStdString(settingsValue(store, "MODEL"));
    if (apiKey.isEmpty())
        LENS_WARN("no API key in the settings; explanations will be refused until one is set");

    lens::llm::LlmClient llm(lens::llm::Config{QUrl(baseUrl.isEmpty() ? QStringLiteral("https://api.deepseek.com") : baseUrl),
                                               apiKey,
                                               model.isEmpty() ? QStringLiteral("deepseek-flash") : model});

    MouseSelectionHook hook;
    if (!hook.install())
        return 1;

    AppController controller(store, llm, hook, pricing);

    Tray tray(controller);
    if (!tray.show())
        return 1;

    QQmlApplicationEngine engine;
    // Context properties rather than registered singletons: two objects, one engine, and no
    // build-time type registration to keep in step. ponytail: move to
    // qmlRegisterSingletonInstance if the module ever grows past these two.
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("tray"), &tray);

    TranslationKeeper translations;
    translations.select(QString::fromStdString(settingsValue(store, "uiLanguage").empty()
                                                   ? std::string("zh")
                                                   : settingsValue(store, "uiLanguage")));
    // Installing a translator does not make QML's qsTr bindings re-evaluate -- the tray menu
    // follows along only because Tray::retranslate() listens for this signal, and the surfaces
    // had no equivalent. engine.retranslate() is the hook Qt 6.2 added for exactly this, and it
    // is why the engine is declared above the connect rather than below it.
    QObject::connect(&controller, &AppController::uiLanguageChanged, &app, [&translations, &engine](const QString& language) {
        translations.select(language);
        engine.retranslate();
    });

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { LENS_CRITICAL("the QML surfaces failed to load"); QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("Lens", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    LENS_INFO("Lens is up");
    return app.exec();
}
