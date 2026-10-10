/**
 * @file tst_pet_controller.cpp
 * @brief The pet controller without a window: events move the state, the one clock moves the frame, and the
 *        worn accessories and the size are stored (PHASE3 3.5, 3.7).
 *
 * Guiless and window-free: no window is created and nothing is drawn, so the run touches nothing on the desktop.
 * Its settings document lives in a scratch directory.
 */

#include <filesystem>
#include <optional>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "app/pet/pet_assets.h"
#include "app/pet/pet_controller.h"
#include "app/pet/pet_store.h"
#include "core/known_store.h"

namespace {

using lens::app::pet::PetAssets;
using lens::app::pet::PetController;
using lens::app::pet::PetStore;
using lens::core::KnownStore;
using lens::core::pet::PetEvent;

/// The controller and the store it reads, built the way main() builds them.
struct Rig {
    KnownStore store;
    PetStore petStore;
    PetController controller;

    Rig(std::filesystem::path const& settings, std::optional<PetAssets> assets)
        : store(KnownStore::load(settings)), petStore(store), controller(std::move(assets), petStore)
    {
    }
};

std::optional<PetAssets> shippedAssets()
{
    return PetAssets::load(std::filesystem::path{LENS_DATA_DIR} / "pet");
}

std::filesystem::path settingsIn(QTemporaryDir const& dir)
{
    return std::filesystem::path{dir.path().toStdWString()} / "settings.json";
}

} // namespace

class TstPetController : public QObject {
    Q_OBJECT

private slots:
    void eventsDriveTheAction();
    void theClockAdvancesFramesOnlyWhileRunning();
    void wornAccessoriesFollowToggles();
    void sizeIsStoredAndAnnounced();
    void refusedDataLeavesThePetOff();
};

void TstPetController::eventsDriveTheAction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));

    rig.controller.dragStart();
    QCOMPARE(rig.controller.action(), QStringLiteral("pickup"));

    rig.controller.dragEnd();
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));

    rig.controller.handle(PetEvent::ExplanationShown);
    QCOMPARE(rig.controller.action(), QStringLiteral("study"));

    rig.controller.handle(PetEvent::ExplanationHidden);
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));

    rig.controller.handle(PetEvent::KnownMarked);
    QCOMPARE(rig.controller.action(), QStringLiteral("celebrate"));
}

void TstPetController::theClockAdvancesFramesOnlyWhileRunning()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    // Study loops at 4 fps, so a frame changes within a quarter of a second.
    rig.controller.handle(PetEvent::ExplanationShown);
    QSignalSpy frames(&rig.controller, &PetController::frameChanged);

    QTest::qWait(300);
    QCOMPARE(frames.count(), 0);

    rig.controller.setRunning(true);
    QVERIFY(frames.wait(1000));
    rig.controller.setRunning(false);

    QVERIFY(rig.controller.frameIndex() >= 0);
    QVERIFY(rig.controller.frameIndex() < 6);
}

void TstPetController::wornAccessoriesFollowToggles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    rig.controller.toggleAccessory(QStringLiteral("hat"));
    QCOMPARE(rig.controller.wornAccessories().size(), 1);
    QVERIFY(rig.petStore.outfit().head == std::optional<std::string>{"hat"});

    auto const worn = rig.controller.wornAccessories().first().toMap();
    QVERIFY(worn.value("asset").toString().startsWith("file:///"));
    QCOMPARE(worn.value("zIndex").toInt(), 4);

    rig.controller.toggleAccessory(QStringLiteral("hat"));
    QCOMPARE(rig.controller.wornAccessories().size(), 0);
    QVERIFY(!rig.petStore.outfit().head);
}

void TstPetController::sizeIsStoredAndAnnounced()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    QCOMPARE(rig.controller.scale(), 3);

    QSignalSpy changed(&rig.controller, &PetController::settingsChanged);
    rig.controller.setScale(9);
    QCOMPARE(rig.controller.scale(), rig.controller.maxScale());
    QCOMPARE(changed.count(), 1);

    rig.controller.setScale(2);
    QCOMPARE(rig.controller.scale(), 2);
}

void TstPetController::refusedDataLeavesThePetOff()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), std::nullopt};

    QVERIFY(!rig.controller.available());
    QVERIFY(!rig.controller.enabled());

    rig.controller.handle(PetEvent::Click);
    rig.controller.setRunning(true);
    QVERIFY(rig.controller.action().isEmpty());
    QCOMPARE(rig.controller.wornAccessories().size(), 0);
}

QTEST_GUILESS_MAIN(TstPetController)
#include "tst_pet_controller.moc"
