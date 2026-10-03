import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The bar that comes up when the reader finishes selecting text.
 *
 * Only the half that needs no Controller is here. Pressing one of the items also arms the
 * card's DragHandler, whose grab calls Controller.cursorPos(), and a Controller that was never
 * provided trips an assert in its factory -- so the press-and-hold scale, which is the case
 * worth having, belongs to lens_qtest_surfaces. See TEST.md section 2.
 */
Item {
    id: root
    width: 400
    height: 400

    Component {
        id: barComponent
        SelectionBar { selectionText: "ubiquitous" }
    }

    TestCase {
        name: "SelectionBar"
        when: windowShown

        /// @return The bar, opened at @p anchor.
        function opened(anchor) {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.openAt({ x: anchor.x, y: anchor.y, kind: "word", text: "ubiquitous" });
            wait(50);
            return bar;
        }

        function test_theBarOffersTheThreeSelectionActions() {
            const bar = opened({ x: 300, y: 300 });

            compare(Util.textsUnder(bar).map((t) => t.text).sort(), ["Copy text", "Explain", "Translate"]);
        }

        /// Every placement is by the card rather than by the window, which is shadowMargin
        /// bigger on each side -- lining the window up with the anchor put the card a
        /// shadow-margin clear of the selection, measured on the real surface.
        function test_theCardClearsTheAnchorByTheGap() {
            const bar = opened({ x: 300, y: 300 });
            compare(bar.y + bar.height - bar.shadowMargin, 300 - bar.gap, "the card should hang above the anchor");
            compare(bar.x + bar.shadowMargin, 300, "the card should be centred on the anchor");

            const top = opened({ x: 300, y: 0 });
            compare(top.y + top.shadowMargin, top.gap, "with no room above, the card should flip below the anchor");
        }
    }
}
