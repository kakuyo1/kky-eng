import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 380
    height: 720

    Component {
        id: pageComponent
        SettingsCapturePage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    TestCase {
        name: "CaptureSettings"
        when: windowShown

        function make() {
            const page = createTemporaryObject(pageComponent, root);
            verify(!!page, "Capture page was created");
            return page;
        }

        function child(page, name) {
            const control = findChild(page, name);
            verify(!!control, "Object exists: " + name);
            return control;
        }

        function cleanup() {
            Controller.restoreCaptureDefaults();
            Controller.setTheme("dark");
            Tokens.theme = "dark";
        }

        function test_dragTiersFollowTheControllerFacade() {
            const page = make();
            const segment = child(page, "dragSensitivity");
            mouseClick(segment, 20, segment.height / 2);
            tryVerify(() => Controller.settings.dragSensitivity === "sensitive");
            mouseClick(segment, segment.width - 20, segment.height / 2);
            tryVerify(() => Controller.settings.dragSensitivity === "reluctant");
        }

        function test_minimumLengthWritesTheSelectedBoundary() {
            const page = make();
            const field = child(page, "minimumWordLength");
            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null);
            const row = Util.textWith(Util.textsInNestedWindow(field), ["5"]);
            verify(!!row, "The five-letter option exists");
            mouseClick(row.parent, row.parent.width / 2, row.parent.height / 2);
            tryVerify(() => Controller.settings.minimumWordLength === 5);
        }

        function test_whitelistEditingNormalizesAndRejectsInvalidInput() {
            const page = make();
            const field = child(page, "scanWhitelist");
            field.forceActiveFocus();
            field.text = "CHROME.EXE, firefox.exe";
            root.forceActiveFocus();
            tryVerify(() => Controller.settings.scanWhitelist === "chrome.exe;firefox.exe");
            field.forceActiveFocus();
            field.text = "../invalid.exe";
            root.forceActiveFocus();
            tryCompare(field, "text", "chrome.exe;firefox.exe");
        }

        /// The two paths are the reader's own Tesseract: they persist through the controller and
        /// the engine is rebuilt around them, which the availability the switches read follows.
        function test_tesseractPathsReachTheController() {
            const page = make();
            const executable = child(page, "tesseractExecutable");
            executable.forceActiveFocus();
            executable.text = "no-such-directory/tesseract.exe";
            root.forceActiveFocus();
            tryVerify(() => Controller.settings.tesseractExecutable === "no-such-directory/tesseract.exe");

            const data = child(page, "tesseractDataDirectory");
            data.forceActiveFocus();
            data.text = "no-such-directory/tessdata";
            root.forceActiveFocus();
            tryVerify(() => Controller.settings.tesseractDataDirectory === "no-such-directory/tessdata");

            // Nothing is at either path, so the probe that follows the change cannot succeed and
            // the switches say so rather than offering a runtime that is not there.
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(Controller.settings.ocrAvailable, false);
        }

        function test_ocrControlsUseControllerAndAvailability() {
            const page = make();
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(child(page, "ocrCapture").interactive, Controller.settings.ocrAvailable);
            compare(child(page, "autoScan").interactive, Controller.settings.ocrAvailable);
        }
    }
}
