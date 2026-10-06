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
            Controller.setMultiSense(false);
            Controller.setTheme("light");
            Controller.setUiLanguage("zh");
            Controller.setSelectionCapture(true);
            Controller.setClipboardPolicy("topmost");
            Controller.setPopupFrequency("standard");
            Controller.setProvider("DeepSeek");
            Controller.setModel("deepseek-flash");
            Controller.setAutoScan(false);
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        function categoryRow(panel, title) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SettingsCategoryRow_QMLTYPE") === 0;
            });
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].title === title)
                    return rows[i];
            }
            return null;
        }

        function openCategory(panel, title) {
            const row = categoryRow(panel, title);
            verify(row, "the settings catalog has no " + title + " category");
            mouseClick(row, row.width / 2, row.height / 2);
            wait(20);
        }

        function back(panel) {
            const icons = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Icon_QMLTYPE") === 0 && o.source.indexOf("ui-back.svg") >= 0;
            });
            verify(icons.length > 0, "the settings page has no back control");
            mouseClick(icons[0], icons[0].width / 2, icons[0].height / 2);
            wait(20);
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
            openCategory(panel, "Model service");
            const apiLink = Util.ofType(panel, "SettingsLinkRow");
            verify(apiLink, "the model page has no API configuration link");
            mouseClick(apiLink, apiLink.width / 2, apiLink.height / 2);
            wait(20);
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

        function test_catalogUsesAStableFixedSurfaceAndReturnsFromAChildPage() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            compare(panel.width, 432);
            compare(panel.height, 532);

            openCategory(panel, "Capture & popups");
            const link = Util.ofType(panel, "SettingsLinkRow");
            verify(link, "the capture page has no popup link");
            mouseClick(link, link.width / 2, link.height / 2);
            wait(20);

            compare(panel.category, "capture");
            compare(panel.route, "clipboard");
            verify(panel.openChildRect === null, "a child page must not expose a dropdown window");

            const back = Util.textWith(Util.textsUnder(panel), ["Settings"]);
            verify(back, "the settings header disappeared on a child page");
        }

        function test_captureTheCatalogAndDetailStates() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the settings snapshots");
                return;
            }

            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            const settled = Tokens.motion.pop + 80;

            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-catalog"));

            openCategory(panel, "Reading & learning");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-learning"));
            back(panel);

            openCategory(panel, "General");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-general"));
            back(panel);

            openCategory(panel, "Capture & popups");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-capture"));
            const popupLink = Util.ofType(panel, "SettingsLinkRow");
            verify(popupLink);
            mouseClick(popupLink, popupLink.width / 2, popupLink.height / 2);
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-clipboard"));

            panel.visible = false;
            wait(20);
            main.showSettings();
            wait(settled);
            openCategory(panel, "Daily budget");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-budget"));
            back(panel);
            openCategory(panel, "Model service");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-model"));
            const apiLink = Util.ofType(panel, "SettingsLinkRow");
            verify(apiLink);
            mouseClick(apiLink, apiLink.width / 2, apiLink.height / 2);
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-connection"));
        }

        function test_captureSettingsInBothThemes() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the settings snapshots");
                return;
            }

            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);

            // The panel is a card that fades and lifts into place (ShadowCard.qml), so a grab
            // taken before that has finished photographs a half-transparent card.
            const settled = Tokens.motion.pop + 80;

            Controller.setTheme("light");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-light"));

            Controller.setTheme("dark");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-dark"));
        }
    }
}
