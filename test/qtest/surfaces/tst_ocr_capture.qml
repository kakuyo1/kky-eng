import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Framing a region to read, which is what the trigger key is for.
 *
 * The two halves are checked apart, because they meet in exactly one place: the mask hands a
 * rectangle to the controller, and the controller says whether a capture can start and why not.
 * Neither half is a window the offscreen platform can put on a screen, so what a case can hold is
 * the rectangle that comes back and the reason that goes out -- the same boundary the capture
 * settings are held to (`tst_capture.qml`).
 *
 * What cannot be checked here at all: whether a screen grab taken a moment after the mask hides
 * still has the mask in it. That is the one thing about this feature that needs a real desktop and
 * a real eye, and `Mask.qml` says so where the wait is.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: maskComponent
        Mask {}
    }

    Component {
        id: pageComponent
        SettingsCapturePage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "OcrCapture"
        when: windowShown

        function cleanup() {
            Controller.restoreCaptureDefaults();
        }

        /// @return The mask, opened.
        function openedMask() {
            const mask = createTemporaryObject(maskComponent, root);
            verify(mask);
            mask.open();
            wait(50);
            return mask;
        }

        /// @return Every sheet the mask built -- one per screen, which is what the mask is for.
        ///         They are Windows, so they are not among the mask's visual children: what holds
        ///         them is the Instantiator, and this asks it.
        function sheetsOf(mask) {
            const instantiator = Util.findAll(mask, function (o) { return o.toString().indexOf("Instantiator") >= 0; })[0];
            const built = [];
            for (let i = 0; instantiator && i < instantiator.count; ++i)
                built.push(instantiator.objectAt(i));
            return built;
        }

        /// @return The one sheet of a mask built against a single-screen desktop.
        function sheetOf(mask) {
            const sheets = sheetsOf(mask);
            verify(sheets.length > 0, "the mask built no sheet");
            return sheets[0];
        }

        /// The mask is up over every screen while it is open, and one window per screen is what
        /// makes that true on a desktop whose monitors do not share a scale.
        function test_theMaskCoversEveryScreenOnlyWhileItIsOpen() {
            const mask = openedMask();
            compare(mask.shown, true);

            const sheets = sheetsOf(mask);
            verify(sheets.length > 0, "the mask built no sheet");
            compare(sheets.length, Qt.application.screens.length, "one sheet per screen");
            for (let i = 0; i < sheets.length; ++i)
                compare(sheets[i].visible, true, "a sheet was left hidden while the mask is up");

            mask.close();
            wait(50);
            compare(mask.shown, false);
            for (let i = 0; i < sheets.length; ++i)
                compare(sheets[i].visible, false, "a sheet outlived the mask");
        }

        /// Escape is the way out of a mask the reader opened by mistake. The shortcut is
        /// application-wide, which is the only scope the offscreen platform can reach -- the same
        /// reason the removal question's is.
        function test_escapeTakesTheMaskDown() {
            const mask = openedMask();

            keyClick(Qt.Key_Escape);
            tryVerify(function () { return mask.shown === false; }, 1000, "Escape left the mask up");
        }

        /// A drag hands back the rectangle it framed, in the desktop coordinates the capture takes,
        /// and the mask is already off the screen when it does -- the shot cannot contain the mask.
        function test_aDragHandsBackTheRectangleItFramed() {
            const mask = openedMask();
            const sheet = sheetOf(mask);
            verify(sheet, "the mask has no sheet to drag in");

            let chosen = null;
            mask.regionChosen.connect(function (region) { chosen = region; });

            mousePress(sheet, 30, 40);
            mouseMove(sheet, 130, 90);
            mouseRelease(sheet, 130, 90);

            // The rectangle comes back after the settle delay, which is the point of it.
            compare(chosen, null, "the rectangle arrived before the mask had time to leave");
            tryVerify(function () { return chosen !== null; }, 1000, "the drag produced no rectangle");
            compare(chosen.width, 100);
            compare(chosen.height, 50);
            compare(chosen.x, sheet.x + 30);
            compare(chosen.y, sheet.y + 40);
            compare(mask.shown, false, "the mask stayed up after the drag");
        }

        /// A press that did not travel is a reader changing their mind: no rectangle, no capture.
        function test_aPressWithoutADragAsksForNothing() {
            const mask = openedMask();
            const sheet = sheetOf(mask);
            verify(sheet);

            let chosen = null;
            mask.regionChosen.connect(function (region) { chosen = region; });

            mouseClick(sheet, 40, 40);
            wait(300);

            compare(chosen, null, "a click was taken for a capture");
            compare(mask.shown, false, "the mask stayed up after the click");
        }

        /// The two halves meet at exactly one wire -- the mask's rectangle, handed to the
        /// controller by the root -- and this is that wire. Both halves pass their own cases while
        /// it is missing, which is what it was: a drag framed a region, the mask went, and nothing
        /// was asked for, with no error anywhere because nobody was listening.
        ///
        /// What the case reads is the answer the controller gives back. The fixture has OCR capture
        /// off, which is the one refusal that reads the same on every machine; the rest depend on
        /// the runtime this runs on, and `test_aCaptureThatCannotStartSaysWhy` is where that line
        /// is drawn.
        function test_theFramedRegionReachesTheControllerThroughTheRoot() {
            compare(Controller.settings.ocrCapture, false, "the fixture should have OCR capture off");
            Controller.dismissNotice();

            const main = createTemporaryObject(mainComponent, root);
            verify(main, "the root did not build");
            wait(50);
            const mask = Util.ofType(main, "Mask");
            verify(mask, "the root builds no capture mask");
            mask.open();
            wait(50);
            const sheet = sheetOf(mask);
            verify(sheet, "the mask has no sheet to drag in");

            mousePress(sheet, 30, 40);
            mouseMove(sheet, 130, 90);
            mouseRelease(sheet, 130, 90);

            tryVerify(function () { return Controller.notice.title !== undefined; }, 2000,
                      "the framed region never reached the controller");
            compare(Controller.notice.title, qsTranslate("SettingsPopup", "OCR"));
            compare(Controller.notice.body, qsTranslate("lens::app::AppController", "Screenshot capture is off."));

            Controller.dismissNotice();
        }

        /// The reason a capture cannot start, at the controller. The fixture has OCR capture off, so
        /// this is the one refusal that is the same on every machine: what the switch is set to.
        /// The rest read the runtime's own state, which is a question for the machine it runs on.
        function test_aCaptureThatCannotStartSaysWhy() {
            compare(Controller.settings.ocrCapture, false, "the fixture should have OCR capture off");

            const refusal = Controller.captureRegion(Qt.rect(0, 0, 100, 50));
            compare(refusal, "screenshot-off");
            compare(Controller.notice.title, qsTranslate("SettingsPopup", "OCR"));
            compare(Controller.notice.body, qsTranslate("lens::app::AppController", "Screenshot capture is off."));
            compare(Controller.notice.kind, "error");

            Controller.dismissNotice();
        }

        // The key itself, on the settings page it is set on.

        /// @return The field the page draws the trigger key in.
        function hotkeyField() {
            const page = createTemporaryObject(pageComponent, root);
            verify(page);
            const field = findChild(page, "ocrHotkey");
            verify(field, "the capture page draws no trigger key");
            return field;
        }

        /// @return The capsules the field draws the combination in, in order.
        function capsulesOf(field) {
            return Util.findAll(field, function (o) {
                return typeof o.text === "string" && o.parent && o.parent.radius === Tokens.radiusPill && o.parent.color === Tokens.panel;
            }).map(function (o) { return o.text; });
        }

        /// A combination is read key by key: what the controller stores is one string, and what the
        /// reader sees is one capsule per key.
        function test_theFieldDrawsTheCombinationOneKeyAtATime() {
            const field = hotkeyField();

            compare(field.combination, Controller.settings.ocrHotkey);
            const parts = field.combination.split("+");
            compare(capsulesOf(field).join(), parts.join());
            compare(parts.length, 3, "the default trigger is three keys");
        }

        /// The field hands the key over as it arrived; the combination the reader reads back is the
        /// controller's spelling of it, not the field's.
        function test_pressingACombinationStoresIt() {
            const field = hotkeyField();

            field.keyChosen(Qt.Key_K, Qt.ControlModifier | Qt.AltModifier);
            tryVerify(function () { return Controller.settings.ocrHotkey === "Ctrl+Alt+K"; }, 1000);
            compare(field.combination, "Ctrl+Alt+K");
            compare(capsulesOf(field).join(), "Ctrl,Alt,K");
        }

        /// A key without Ctrl or Alt would be taken from every other program on the machine, so it
        /// is refused where the rule lives -- and the field says so rather than showing a
        /// combination that will not fire.
        function test_aCombinationWithoutCtrlOrAltIsRefused() {
            const field = hotkeyField();
            const before = Controller.settings.ocrHotkey;

            compare(Controller.setOcrHotkey(Qt.Key_K, Qt.NoModifier), "unusable");
            compare(Controller.settings.ocrHotkey, before, "a refused choice changed the setting");

            field.keyChosen(Qt.Key_K, Qt.NoModifier);
            compare(field.refused, true, "the field did not say the combination was refused");
        }

        /// The row's own drawing is checked against the snapshot the settings case already takes of
        /// this page (`tst_settings_wave3.qml` → `settings-capture.png`): a second still of the same
        /// row from a nested item was a shifted render, and one picture of a surface is enough.
    }
}
