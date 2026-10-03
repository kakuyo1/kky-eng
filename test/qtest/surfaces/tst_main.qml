import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Main.qml: the root that owns every surface, and the placement and dismissal arithmetic the
 * surfaces are put up and taken down by.
 *
 * None of this could be driven at all before this target existed -- the root reads
 * Controller.settings on its first binding, and a Controller that was never provided either
 * reads as null or, in a Debug build, trips an assert in its factory and takes the process
 * down. See TEST.md section 5 for what a person was doing instead.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        name: "Main"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// The root is a Window rather than an Item, so it is the one place a panel cannot be
        /// reached through an id: Util.ofType is what gets at one from outside.
        function panel(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        function test_theColourTableFollowsTheStoredTheme() {
            make();
            compare(Controller.settings.theme, "dark", "the fixture should be a non-default theme");

            compare(Tokens.theme, Controller.settings.theme, "Tokens did not follow the settings");
            compare(Tokens.dark, true);
        }

        /// The two halves that were measured on the real window: the hook reports physical
        /// pixels and the windows are placed in device-independent ones, and the payload has
        /// to be rebuilt rather than edited in place, because writing to the JS view of a
        /// QVariantMap silently keeps the old value.
        ///
        /// @note This runs on the offscreen platform, whose device pixel ratio is 1, so the
        ///       division here is the identity: what the case pins down is the copy and the
        ///       field set, not the arithmetic. Run with QT_SCALE_FACTOR set and the same
        ///       assertion would also cover the arithmetic.
        function test_toDipDividesThePositionAndKeepsEveryOtherField() {
            const main = make();

            const payload = { x: 250, y: 500, kind: "word", text: "ubiquitous" };
            const at = main.toDip(payload);
            const ratio = Screen.devicePixelRatio || 1;

            verify(at !== payload, "toDip handed back the payload itself; the callers divide in place through it");
            compare(at.x, 250 / ratio);
            compare(at.y, 500 / ratio);
            compare(at.kind, "word");
            compare(at.text, "ubiquitous");
            compare(payload.x, 250, "toDip wrote through to the payload the caller still holds");
        }

        /// The clamp is the whole point of the function: the panels are wider than the tray
        /// icon's distance from the screen edge, so without it a panel opened from the corner
        /// hangs off the side.
        function test_placeBesideKeepsThePanelOnTheScreenItBelongsTo() {
            const main = make();

            // The tray icon's corner, which is where every panel is opened from and the only
            // anchor placeBeside is ever handed.
            const anchor = main.trayAnchor();
            const screen = main.screenFor(anchor);
            verify(screen, "the anchor is on no screen");

            const width = 332;
            const height = 300;
            const at = main.placeBeside(anchor, width, height);

            verify(at.x >= screen.virtualX + 8, "the panel hangs off the left edge");
            verify(at.x + width <= screen.virtualX + screen.width - 8, "the panel hangs off the right edge");
            verify(at.y >= screen.virtualY + 8, "the panel hangs off the top edge");
            verify(at.y + height <= screen.virtualY + screen.height - 8, "the panel hangs off the bottom edge");
        }

        /// UI.md's "click outside closes", which no photograph can check: the four panels and
        /// the action bar each have to go when a press misses them, and the one the press
        /// landed on has to stay.
        function test_aPressOutsideClosesThePanelItMissed() {
            const main = make();
            const stats = panel(main, "StatsPopup");

            main.showStats();
            compare(stats.visible, true, "showStats() did not put the panel up");

            const card = Qt.point(stats.x + stats.width / 2, stats.y + stats.height / 2);
            main.dismissOutside(card);
            compare(stats.visible, true, "a press on the panel closed it");

            main.dismissOutside(Qt.point(0, 0));
            compare(stats.visible, false, "a press away from the panel left it up");
        }

        /// Opening one panel takes the others down, so the reader never has two on screen
        /// stacked beside each other.
        function test_openingAPanelTakesTheOtherDown() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const settings = panel(main, "SettingsPopup");

            main.showStats();
            compare(stats.visible, true);

            main.showSettings();
            compare(settings.visible, true);
            compare(stats.visible, false, "the statistics panel stayed up behind the settings panel");
        }
    }
}
