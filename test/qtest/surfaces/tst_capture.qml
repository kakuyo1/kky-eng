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

        function test_ocrControlsUseControllerAndAvailability() {
            const page = make();
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(child(page, "ocrCapture").interactive, Controller.settings.ocrAvailable);
            compare(child(page, "autoScan").interactive, Controller.settings.ocrAvailable);
        }
    }
}
