import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * A labelled switch inside the capture group. The row owns the label and the enabled state;
 * the Switch inside owns the toggling, and the row only forwards the signal.
 */
Item {
    id: root
    width: 300
    height: 120

    Component {
        id: rowComponent
        SwitchRow { width: 260; label: "Selection capture"; checked: true }
    }

    TestCase {
        name: "SwitchRow"
        when: windowShown

        function make(properties) {
            const row = createTemporaryObject(rowComponent, root, properties);
            verify(row);
            return row;
        }

        function switcherOf(row) {
            const switches = Util.findAll(row, (o) => typeof o.interactive === "boolean" && o !== row);
            compare(switches.length, 1);
            return switches[0];
        }

        function test_theRowHandsItsStateToTheSwitch() {
            compare(switcherOf(make({})).checked, true);
            compare(switcherOf(make({ checked: false })).checked, false);
        }

        function test_togglingTheSwitchReportsThroughTheRow() {
            const row = make({});
            let reported = null;
            row.toggled.connect(function (on) { reported = on; });

            const switcher = switcherOf(row);
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            compare(switcher.checked, false);
            compare(reported, false);
        }

        function test_aNonInteractiveRowFadesItsLabel() {
            const labelOf = (row) => Util.textsUnder(row)[0];

            compare(labelOf(make({})).color, Tokens.text);
            compare(labelOf(make({ interactive: false })).color, Tokens.faint,
                    "a placeholder row should read as unavailable without saying why");
        }
    }
}
