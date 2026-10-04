import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 480
    height: 720

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "SettingsWave3"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        function cleanup() {
            Controller.setLevel(2);
            Controller.setExplanationLang("en");
            Controller.setTheme("light");
            Controller.setSelectionCapture(true);
            Controller.setClipboardPolicy("topmost");
            Controller.setAutoScan(false);
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        function test_defaultsRestoreAllLocalSettingsWithoutTouchingProtectedState() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            const hasApiKey = Controller.settings.hasApiKey;
            const autostart = Controller.settings.autostart;

            Controller.setLevel(4);
            Controller.setExplanationLang("zh");
            Controller.setTheme("dark");
            Controller.setSelectionCapture(false);
            Controller.setClipboardPolicy("silent");
            Controller.setAutoScan(true);
            wait(20);

            const defaults = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaults, "the settings panel has no Defaults action");
            mouseClick(defaults.parent, defaults.parent.width / 2, defaults.parent.height / 2);
            tryCompare(Controller.settings, "level", 2);
            compare(Controller.settings.explanationLang, "en");
            compare(Controller.settings.theme, "light");
            compare(Controller.settings.selectionCapture, true);
            compare(Controller.settings.clipboardPolicy, "topmost");
            compare(Controller.settings.autoScan, false);
            compare(Controller.settings.hasApiKey, hasApiKey);
            compare(Controller.settings.autostart, autostart);
        }

        function test_apiKeyFieldUsesOneCursorAcrossItsFullInputArea() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            const inputs = Util.findAll(panel, function (o) {
                return typeof o.text === "string" && o.echoMode === TextInput.Password;
            });
            verify(inputs.length > 0, "the settings panel has no API key input");

            const handlers = Util.findAll(inputs[0], function (o) {
                return o.cursorShape === Qt.PointingHandCursor;
            });
            verify(handlers.length > 0, "the API key input has no full-field hover handler");
            compare(handlers[0].cursorShape, Qt.PointingHandCursor);
        }

        function test_captureSettingsInBothThemes() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the settings snapshots");
                return;
            }

            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);

            Controller.setTheme("light");
            wait(100);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-light"));

            Controller.setTheme("dark");
            wait(100);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-dark"));
        }
    }
}
