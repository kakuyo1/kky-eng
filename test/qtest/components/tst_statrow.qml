import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * One labelled figure in the statistics and cost panels. A clickable row is the part worth
 * asserting: the chevron and the tap target are what tell the reader there is a drill-down
 * behind it, and both are gated on the same flag.
 */
Item {
    id: root
    width: 320
    height: 120

    Component {
        id: rowComponent
        StatRow { width: 200; label: "Known"; value: "128" }
    }

    TestCase {
        name: "StatRow"
        when: windowShown

        function make(properties) {
            const row = createTemporaryObject(rowComponent, root, properties);
            verify(row);
            return row;
        }

        /// @return The chevron, which is the row's only Icon.
        function chevronOf(row) {
            const icons = Util.findAll(row, (o) => typeof o.source === "string");
            compare(icons.length, 1);
            return icons[0];
        }

        function test_theChevronAppearsOnlyOnAClickableRow() {
            compare(chevronOf(make({ clickable: false })).visible, false);
            compare(chevronOf(make({ clickable: true })).visible, true);
        }

        function test_onlyAClickableRowReportsATap() {
            const plain = make({ clickable: false });
            let plainTaps = 0;
            plain.tapped.connect(function () { plainTaps++; });
            mouseClick(plain, plain.width / 2, plain.height / 2);
            compare(plainTaps, 0, "a plain row should not answer a click");

            const clickable = make({ clickable: true });
            let taps = 0;
            clickable.tapped.connect(function () { taps++; });
            mouseClick(clickable, clickable.width / 2, clickable.height / 2);
            compare(taps, 1);
        }
    }
}
