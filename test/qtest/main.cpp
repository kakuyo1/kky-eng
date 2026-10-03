/**
 * @file main.cpp
 * @brief The runner every QTest target shares: hand quick_test_main the suite's name and the
 *        directory its .qml cases live in, and let the setup put the singletons in place.
 *
 * All three come from the target's own compile definitions, so a second target is a second
 * add_executable on this file rather than a second runner. quick_test_main builds the
 * QGuiApplication itself, which is the reason the cases cannot live under gtest: that harness
 * has no window system to offer.
 */

#include <QtQuickTest/quicktest.h>

#include "setup.h"

int main(int argc, char* argv[])
{
    LensTestSetup setup;
    return quick_test_main_with_setup(argc, argv, LENS_QTEST_NAME, LENS_QTEST_DIR, &setup);
}
