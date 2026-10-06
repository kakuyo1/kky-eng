import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The tray menu: the state line, the language row and the figures row, all of which are
 * derived from the controller rather than from anything the menu holds.
 *
 * The menu is one of the two surfaces TEST.md section 5 says cannot be photographed on this
 * machine at all, so everything below had no automated check of any kind. The cases that go
 * through a setter are the ones worth having: they prove the whole loop -- write, settings
 * document, settingsChanged, rebinding -- which is what a person was checking by hand.
 */
Item {
    id: root
    width: 400
    height: 400

    Component {
        id: menuComponent
        TrayMenu {}
    }

    /// The two the cases below flip, in the fixture's own values, so a case that throws
    /// halfway through does not leave the next one asserting against its leftovers.
    function restore() {
        Controller.setSelectionCapture(false);
        Controller.setUiLanguage("en");
    }

    TestCase {
        name: "TrayMenu"
        when: windowShown

        function make() {
            const menu = createTemporaryObject(menuComponent, root);
            verify(menu);
            menu.contentActive = true;
            wait(50);
            return menu;
        }

        /// @return The menu's own texts, keyed by what they say about the controller.
        function stateLine(menu) {
            return Util.textWith(Util.textsUnder(menu), [Controller.modeLabel, qsTr("Selection capture is off")]);
        }

        /// @return The menu's rows, in declaration order.
        function rowsOf(menu) {
            return Util.findAll(menu, function (o) {
                return o.toString().indexOf("MenuRow_QMLTYPE") === 0;
            });
        }

        /// @return The row carrying @p label.
        ///
        /// By label rather than by position: the language list unfolding or a row being added
        /// would silently move a case onto the wrong row.
        function rowWith(menu, label) {
            const rows = rowsOf(menu).filter(function (r) { return r.label === label; });
            verify(rows.length === 1, "expected exactly one " + label + " row, found " + rows.length);
            return rows[0];
        }

        /// @return The option rows of the unfolded language list.
        function languageOptions(menu) {
            return Util.findAll(menu, function (o) {
                return o.modelData !== undefined && o.modelData.value !== undefined;
            });
        }

        function test_rowsAreCreatedOnFirstOpen() {
            const menu = createTemporaryObject(menuComponent, root);
            verify(menu);
            compare(menu.contentActive, false);
            compare(Util.textsUnder(menu).length, 0);
            const initialHeight = menu.height;

            menu.contentActive = true;
            tryCompare(menu, "contentActive", true);
            verify(Util.textWith(Util.textsUnder(menu), [qsTr("Statistics")]));
            verify(menu.height > initialHeight, "the opened menu has no room for its rows");
        }

        function test_theStateLineSaysWhetherCaptureIsOn() {
            const menu = make();
            compare(menu.capturing, false, "the fixture should have capture off");
            compare(stateLine(menu).text, qsTr("Selection capture is off"));

            Controller.setSelectionCapture(true);
            wait(50);
            compare(menu.capturing, true);
            compare(stateLine(menu).text, Controller.modeLabel);

            restore();
        }

        function test_theLanguageRowNamesTheInterfaceLanguage() {
            const menu = make();
            compare(menu.language, "en", "the fixture should have the interface in English");
            compare(menu.languageName, "English");

            Controller.setUiLanguage("zh");
            wait(50);
            compare(menu.languageName, "中文");

            restore();
        }

        /// The row's figure is built by the menu, so a mistyped key in it reads as
        /// "undefined words · undefinedundefined" rather than as a missing value.
        function test_theFiguresRowIsTodaysTally() {
            const menu = make();

            verify(/^\d+ words · .\d+\.\d\d$/.test(menu.todayFigures),
                   "the figures row is malformed: " + menu.todayFigures);
            compare(menu.todayFigures.indexOf(String(Controller.stats.todayPops)), 0,
                    "the figures row does not start with the day's pop count");
        }

        /// The list is the one card whose appearance does not follow its window: the menu is
        /// already up by the time this loader builds it, so the card follows the loader's own
        /// status instead. A card left at zero opacity is invisible while every geometric
        /// assertion beside this one still passes.
        function test_theUnfoldedListEndsUpDrawn() {
            const menu = make();
            compare(menu.listVisible, false);

            // The list's loader, told from the body's by what it holds: the body's item is a
            // Column, the list's is the card, and only the card answers to `shown`.
            menu.listVisible = true;
            const loader = Util.findAll(menu, function (o) {
                return o.item !== undefined && o.item !== null && o.item.shown !== undefined;
            })[0];
            verify(loader, "the language list is not a loader holding a card");

            tryVerify(function () { return loader.item.opacity === 1; },
                      1000, "the unfolded language list never finished appearing");
        }

        /// The two panels the menu opens. The menu asks for one rather than calling into
        /// Main.qml, so the request is what a case can catch, and the signal has to carry.
        function test_pickingTheStatisticsRowAsksForThePanelAndClosesTheMenu() {
            const menu = make();
            let asked = 0;
            menu.statsRequested.connect(function () { asked += 1; });

            const row = rowWith(menu, qsTr("Statistics"));
            mouseClick(row, row.width / 2, row.height / 2);

            compare(asked, 1, "the Statistics row did not ask for the panel");
            tryCompare(menu, "visible", false, 1000, "the menu stayed up after a row was picked");
        }

        function test_pickingTheSettingsRowAsksForThePanelAndClosesTheMenu() {
            const menu = make();
            let asked = 0;
            menu.settingsRequested.connect(function () { asked += 1; });

            const row = rowWith(menu, qsTr("Settings"));
            mouseClick(row, row.width / 2, row.height / 2);

            compare(asked, 1, "the Settings row did not ask for the panel");
            tryCompare(menu, "visible", false, 1000, "the menu stayed up after a row was picked");
        }

        /// The language list is the menu's own control, not a signal out to Main: picking a
        /// language has to reach the controller and come back through the settings document.
        function test_pickingALanguageFromTheUnfoldedListSwitchesIt() {
            const menu = make();
            menu.listVisible = true;
            tryCompare(menu, "listVisible", true);

            const options = languageOptions(menu);
            compare(options.length, Controller.settings.languages.length,
                    "the language list should offer every language the catalog carries");
            verify(options.length > 2, "the catalog should carry more than the two written interfaces");
            const chinese = options.filter(function (o) { return o.modelData.value === "zh"; })[0];
            verify(chinese, "the language list has no Chinese option");

            mouseClick(chinese, chinese.width / 2, chinese.height / 2);

            compare(menu.language, "zh", "the pick did not reach the controller");
            compare(menu.languageName, "中文");
            tryCompare(menu, "visible", false, 1000, "the menu stayed up after a language was picked");

            restore();
        }

        /// The list unfolds when the pointer reaches its row and folds again a moment after it
        /// leaves -- the delay is what lets the pointer cross the gap between the two cards.
        function test_hoveringTheLanguageRowUnfoldsTheListAndLeavingFoldsItAgain() {
            const menu = make();
            const row = rowWith(menu, qsTr("Language"));
            compare(menu.listVisible, false);

            mouseMove(row, row.width / 2, row.height / 2);
            tryCompare(menu, "listVisible", true, 1000, "hovering the Language row did not unfold the list");

            // Somewhere in the menu that is not the Language row.
            const elsewhere = rowWith(menu, qsTr("Statistics"));
            mouseMove(elsewhere, elsewhere.width / 2, elsewhere.height / 2);
            tryCompare(menu, "listVisible", false, 1000, "the list stayed unfolded after the pointer left");
        }

        /// Quit is the one row whose handler leaves the menu: it asks the application to stop,
        /// and in this runner there is no application loop listening, so what the case can check
        /// is that the tap reaches the row without taking the run down with it.
        function test_theQuitRowCarriesTheDangerStyling() {
            const menu = make();
            const row = rowWith(menu, qsTr("Quit"));
            compare(row.danger, true, "Quit should be the one row drawn in the danger colour");

            mouseClick(row, row.width / 2, row.height / 2);
            compare(menu.capturing, false, "the run is still alive after tapping Quit");
        }

        /// The language list unfolds to the left of the row it belongs to, so the menu's
        /// window is wider than its card and what counts as a press on it is neither.
        ///
        /// @note Widths and positions are read off hitRect one at a time rather than kept as
        ///       rects. Qt.rect hands back a value type that stays live: a `const closed =
        ///       menu.hitRect` written the obvious way reads as the closed rectangle while the
        ///       assertEquals beside it runs and as the open one by the time the next
        ///       assertion's message is built.
        function test_unfoldingTheLanguageListWidensTheMenu() {
            const menu = make();
            menu.x = 100;
            menu.y = 100;
            wait(50);

            compare(menu.listVisible, false);
            compare(menu.hitRect.x, menu.x + menu.cardLeft, "the closed press area should start at the card");
            const closedWidth = menu.hitRect.width;

            menu.listVisible = true;
            wait(50);

            compare(menu.hitRect.x, menu.x + menu.listLeft, "the press area did not reach the unfolded list");
            verify(menu.hitRect.width > closedWidth, "the press area did not widen");
            compare(menu.hitRect.x + menu.hitRect.width, menu.x + menu.cardLeft + menu.cardWidth,
                    "the list should unfold to the left, leaving the card's right edge where it was");
        }
    }
}
