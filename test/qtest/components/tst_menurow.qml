import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * One row of the tray menu: a glyph and a label, an optional value on the right, and the
 * danger colour the Quit row is drawn in. The menu itself cannot be photographed on this
 * machine (TEST.md section 5), so the row's own behaviour is all that can be checked at all.
 */
Item {
    id: root
    width: 260
    height: 120

    Component {
        id: rowComponent
        MenuRow { width: 220; label: "Settings" }
    }

    TestCase {
        name: "MenuRow"
        when: windowShown

        function make(properties) {
            const row = createTemporaryObject(rowComponent, root, properties);
            verify(row);
            return row;
        }

        /// @return The row's label and note, in the order they are declared.
        function textsOf(row) {
            const texts = Util.textsUnder(row);
            compare(texts.length, 2);
            return texts;
        }

        function test_theNoteIsShownOnlyWhenThereIsOne() {
            const bare = make({});
            compare(textsOf(bare)[1].visible, false, "an empty note left a gap on the right");
            compare(textsOf(bare)[1].text, "");

            const noted = make({ note: "2" });
            compare(textsOf(noted)[1].visible, true);
            compare(textsOf(noted)[1].text, "2");
        }

        function test_theDangerRowIsDrawnInTheDangerColour() {
            compare(textsOf(make({}))[0].color, Tokens.text);
            compare(textsOf(make({ danger: true }))[0].color, Tokens.danger);
        }

        function test_pickingTheRowReports() {
            const row = make({});
            let picks = 0;
            row.picked.connect(function () { picks++; });

            mouseClick(row, row.width / 2, row.height / 2);

            compare(picks, 1);
        }
    }
}
