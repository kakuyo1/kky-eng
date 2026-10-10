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

using lens::app::pet::Outfit;
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
    void previewShowsAnActionAndTheMachineKeepsItsOwn();
    void previewRunsTheClockOnlyWhileOpen();
    void aOneShotPreviewGoesBackToTheMachine();
    void accessoryCatalogueAndWornSlotsAreExposed();
    void restoresOnlyAccessoriesInTheirOwnSlot();
    void passthroughDefaultsOffAndIsStored();
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
    QCOMPARE(worn.value("zIndex").toInt(), 5);

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

void TstPetController::previewShowsAnActionAndTheMachineKeepsItsOwn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    rig.controller.handle(PetEvent::ExplanationShown);
    QCOMPARE(rig.controller.action(), QStringLiteral("study"));

    rig.controller.preview(QStringLiteral("celebrate"));
    QCOMPARE(rig.controller.action(), QStringLiteral("celebrate"));

    rig.controller.preview(QStringLiteral("no_such_action"));
    QCOMPARE(rig.controller.action(), QStringLiteral("celebrate"));

    // Ending the preview hands the dog back to the machine, which was reading the whole time.
    rig.controller.setPreviewing(false);
    QCOMPARE(rig.controller.action(), QStringLiteral("study"));
}

void TstPetController::previewRunsTheClockOnlyWhileOpen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    rig.controller.preview(QStringLiteral("celebrate"));
    QSignalSpy frames(&rig.controller, &PetController::frameChanged);

    rig.controller.setPreviewing(true);
    QVERIFY(frames.wait(1000));

    rig.controller.setPreviewing(false);
    QCOMPARE(rig.controller.action(), QStringLiteral("idle"));
}

void TstPetController::aOneShotPreviewGoesBackToTheMachine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    // Celebrate plays eight frames at 10 fps, so it has run through within about a second.
    rig.controller.preview(QStringLiteral("celebrate"));
    rig.controller.setPreviewing(true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.controller.action(), QStringLiteral("idle"), 2000);
    rig.controller.setPreviewing(false);
}

void TstPetController::accessoryCatalogueAndWornSlotsAreExposed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    QCOMPARE(rig.controller.accessories().size(), 7);
    QCOMPARE(rig.controller.actions().size(), 11);
    QCOMPARE(rig.controller.actions().first(), QStringLiteral("idle"));

    QSignalSpy outfit(&rig.controller, &PetController::outfitChanged);
    rig.controller.toggleAccessory(QStringLiteral("glasses"));
    QCOMPARE(outfit.count(), 1);
    QCOMPARE(rig.controller.wornIn().value("face").toString(), QStringLiteral("glasses"));
    QCOMPARE(rig.controller.wornIn().value("head").toString(), QString{});
}

void TstPetController::restoresOnlyAccessoriesInTheirOwnSlot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    // Glasses saved in the head slot is bad data: it must not be worn there, and the face slot's glasses still is.
    rig.petStore.setOutfit(Outfit{std::string{"glasses"}, std::string{"glasses"}, std::nullopt});
    PetController restored{shippedAssets(), rig.petStore};

    QVERIFY(restored.wornIn().value("head").toString().isEmpty());
    QCOMPARE(restored.wornIn().value("face").toString(), QStringLiteral("glasses"));
}

void TstPetController::passthroughDefaultsOffAndIsStored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig{settingsIn(dir), shippedAssets()};

    QVERIFY(!rig.controller.passthrough());
    rig.controller.setPassthrough(true);
    QVERIFY(rig.controller.passthrough());
    QVERIFY(rig.petStore.passthrough());
}

QTEST_GUILESS_MAIN(TstPetController)
#include "tst_pet_controller.moc"
