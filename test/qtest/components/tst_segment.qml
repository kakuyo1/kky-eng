import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The outlined two-way switch, and the delegate wiring it is built out of.
 *
 * The interesting half is not the switch: it is that a Repeater's `modelData` and `index` reach
 * a Text nested one level inside the delegate. Written unqualified, they resolve to nothing,
 * the cells come up empty, and qmllint is documented as blind to it (docs/QML.md section 8) --
 * which is why a pair of eyes was the only check this had. These cases are that pair of eyes.
 */
Item {
    id: root
    width: 320
    height: 240

    readonly property var labels: ["whole", "phrase", "rare"]

    Component {
        id: segmentComponent
        Segment { width: 240; height: 32 }
    }

    TestCase {
        name: "Segment"
        when: windowShown

        function make(properties) {
            const segment = createTemporaryObject(segmentComponent, root, properties);
            verify(segment);
            return segment;
        }

        function test_everyCellShowsItsOwnLabel() {
            const segment = make({ labels: root.labels });

            const shown = Util.textsUnder(segment).map((t) => t.text);
            compare(shown.sort(), root.labels.slice().sort(),
                    "a cell rendered nothing, so modelData did not reach the nested Text");
        }

        function test_onlyTheSelectedCellTakesTheInkFill() {
            const segment = make({ labels: root.labels, currentIndex: 1 });

            const inked = Util.textsUnder(segment).filter((t) => t.parent.color === Tokens.ink);
            compare(inked.length, 1, "exactly one cell should carry the ink fill, which is index doing its job");
            compare(inked[0].text, root.labels[1]);
        }

        function test_tappingACellPicksItsIndex() {
            const segment = make({ labels: root.labels });

            let picked = -1;
            segment.picked.connect(function (index) { picked = index; });

            const cell = Util.textWith(Util.textsUnder(segment), ["rare"]).parent;
            mouseClick(cell, cell.width / 2, cell.height / 2);

            compare(picked, 2);
        }
    }
}
