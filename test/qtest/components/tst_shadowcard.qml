import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The rounded card every panel is drawn on, and the margin its window has to leave for the
 * blur. Callers place their content with `anchors.fill: parent` plus that margin, so a card
 * that stopped being inset would put every panel's content in the wrong place at once.
 *
 * It is also where every surface's appearance lives, so the case below takes a window of its
 * own up and down: a card stuck at zero opacity draws a blank surface, which no assertion on
 * its geometry or its contents would notice.
 */
Item {
    id: root
    width: 320
    height: 260

    Component {
        id: cardComponent
        ShadowCard {
            width: 200
            height: 160
            Rectangle { objectName: "content"; anchors.fill: parent }
        }
    }

    Component {
        id: windowComponent
        Window {
            width: 260
            height: 200
            visible: false
            ShadowCard { anchors.fill: parent }
        }
    }

    TestCase {
        name: "ShadowCard"
        when: windowShown

        function test_theCardIsInsetByTheShadowMargin() {
            const card = createTemporaryObject(cardComponent, root);
            verify(card);
            wait(50);

            compare(card.card.x, card.shadowMargin);
            compare(card.card.y, card.shadowMargin);
            compare(card.card.width, card.width - 2 * card.shadowMargin);
            compare(card.card.height, card.height - 2 * card.shadowMargin);

            // What a caller's `anchors.fill: parent` lands on: the card, not the window.
            const content = findChild(card, "content");
            verify(content, "the card did not take the declared content");
            compare(content.mapToItem(card, 0, 0).x, card.shadowMargin);
            compare(content.width, card.card.width);
        }

        /// The appearance, which is the card's second job: it follows the window it is drawn
        /// in, so a panel is faded in by the same code whichever panel it is. Both directions
        /// are asserted, because a card that only ever fades in would leave the *next*
        /// appearance of that window instant.
        function test_theCardFollowsTheWindowItIsDrawnIn() {
            const window = createTemporaryObject(windowComponent, root);
            verify(window);
            const card = Util.ofType(window, "ShadowCard");
            verify(card, "the window did not build its card");

            compare(card.opacity, 0, "a card in a window that is not up should be faded out");

            window.visible = true;
            tryVerify(function () { return card.opacity === 1; },
                      1000, "the card did not fade in with its window");

            window.visible = false;
            tryVerify(function () { return card.opacity === 0; },
                      1000, "the card did not fade out with its window");
        }
    }
}
