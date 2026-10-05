import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The settings panel's two newer rows: the sign-in switch and the clipboard policy segment
 * (UI.md section 4.4).
 *
 * The sign-in switch is only read, never driven: its setter writes HKCU, and a case that wrote
 * there would change the machine it runs on. What is assertable is the half that matters -- the
 * switch shows what the controller read back from the registry, not what was last asked for.
 *
 * The clipboard row is driven, because its setter writes the settings document and this target
 * runs against a copy of the fixture (see test/qtest/setup.cpp's writableSettingsCopy).
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        name: "Settings"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// Every engine in the run shares one Controller and one settings document, so a case
        /// that writes one puts it back rather than leaving the next suite reading it.
        function cleanup() {
            Controller.setClipboardPolicy("topmost");
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        /// @return The SwitchRow whose label is `label`, or null. Rows are reached through their
        ///         own label rather than by position, the way the panels cases read StatRow.
        function switchRowLabelled(panel, label) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SwitchRow_QMLTYPE") === 0;
            });
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].label === label)
                    return rows[i];
            }
            return null;
        }

        /// @return The Segment whose first label is `first`, or null.
        function segmentLabelled(panel, first) {
            const segments = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Segment_QMLTYPE") === 0;
            });
            for (let i = 0; i < segments.length; ++i) {
                if (segments[i].labels[0] === first)
                    return segments[i];
            }
            return null;
        }

        function test_theSignInSwitchShowsWhatTheRegistrySays() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);

            const row = switchRowLabelled(panel, "Launch at sign-in");
            verify(row, "the settings panel has no sign-in switch");
            compare(row.checked, Controller.settings.autostart,
                    "the switch is not showing the state read back from the registry");
        }

        function test_theClipboardSegmentPicksThePolicyAndFollowsItBack() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);

            // The fixture carries no policy, so it starts on the default. Pinned here rather
            // than assumed, so the case reads the same whatever order the suites run in.
            Controller.setClipboardPolicy("topmost");
            wait(20);

            const segment = segmentLabelled(panel, "Raise to top");
            verify(segment, "the settings panel has no clipboard segment");
            compare(segment.labels.length, 2, "the clipboard policy is a two-way choice");
            compare(segment.currentIndex, 0, "the segment did not start on the default policy");

            const cell = Util.textWith(Util.textsUnder(segment), ["Give up silently"]).parent;
            mouseClick(cell, cell.width / 2, cell.height / 2);
            wait(20);

            compare(Controller.settings.clipboardPolicy, "silent",
                    "the pick did not reach setClipboardPolicy");
            compare(segment.currentIndex, 1, "the segment did not follow the policy it wrote");
        }
    }
}
