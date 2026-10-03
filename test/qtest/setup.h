#pragma once

#include <QObject>

class QQmlEngine;

/**
 * @brief The singletons the surfaces bind to, handed over before any case's QML is loaded.
 *
 * quick_test_main_with_setup() calls qmlEngineAvailable() once per test file: after that
 * file's engine exists and before its root object is built, which is the only window in which
 * a QML_SINGLETON can be provided. Without it every read off Controller is a TypeError on a
 * null -- and, worse, AppController::create() asserts, so a Debug build takes the process down
 * rather than failing the case.
 *
 * The body is compiled out when LENS_QTEST_SINGLETONS is undefined, which is how the
 * components target keeps running with no settings document and no price list behind it. See
 * TEST.md section 2.
 */
class LensTestSetup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine);
};
