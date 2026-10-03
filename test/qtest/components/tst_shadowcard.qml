import QtQuick
import QtTest
import Lens

/**
 * The rounded card every panel is drawn on, and the margin its window has to leave for the
 * blur. Callers place their content with `anchors.fill: parent` plus that margin, so a card
 * that stopped being inset would put every panel's content in the wrong place at once.
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
    }
}
