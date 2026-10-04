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

        /// The secondary figure. It carries as much as a token count can (the cost panel's),
        /// which is why it is a slot of its own rather than words appended to `value`.
        function test_theNoteIsDrawnOnlyWhenGiven() {
            const plain = make({});
            const plainNotes = Util.findAll(plain, (o) => o.value === "");
            compare(plainNotes.length, 1, "a row should declare exactly one note slot");
            compare(plainNotes[0].visible, false, "an empty note should draw nothing");

            const noted = make({ note: "128000 tokens" });
            const notes = Util.findAll(noted, (o) => o.value === "128000 tokens");
            compare(notes.length, 1);
            compare(notes[0].visible, true);

            const main = Util.findAll(noted, (o) => o.value === "128");
            compare(main.length, 1);
            verify(notes[0].x + notes[0].width <= main[0].x,
                   "the note runs into the main figure it sits in front of");
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
