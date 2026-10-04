import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 800
    height: 600

    Component {
        id: barComponent
        SelectionBar {}
    }

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        name: "Channels"
        when: windowShown

        function actionLabels(bar) {
            return Util.textsUnder(bar).map(function (item) { return item.text; });
        }

        function test_theActionBarKeepsAllActionsForEachChannel() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            const kinds = ["word", "entity", "sentence"];
            for (let i = 0; i < kinds.length; ++i) {
                bar.openAt({x: 300, y: 300, kind: kinds[i], text: "selection"});
                tryCompare(bar, "selectionKind", kinds[i]);
                const labels = actionLabels(bar);
                verify(labels.indexOf("Translate") >= 0);
                verify(labels.indexOf("Explain") >= 0);
                verify(labels.indexOf("Copy text") >= 0);
                bar.visible = false;
            }
        }

        function test_entityBubbleHasNoWordOnlyFields() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "New York",
                type: "entity",
                en: "A city in the United States.",
                zh: "美国的一座城市。",
                x: 200,
                y: 240
            });
            tryCompare(bubble, "title", "New York");
            compare(bubble.type, "entity");
            compare(bubble.ipa, "");
            compare(bubble.status, "");
            verify(Util.textWith(Util.textsUnder(bubble), ["Entity"]));
        }

        function test_sentenceBubbleUsesTheSelectedSentenceAsTitle() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "The quiet room felt different.",
                type: "sentence",
                en: "The room seemed changed.",
                zh: "这个房间似乎变了。",
                x: 200,
                y: 240
            });
            tryCompare(bubble, "title", "The quiet room felt different.");
            compare(bubble.type, "sentence");
            compare(bubble.ipa, "");
            compare(bubble.status, "");
            verify(Util.textWith(Util.textsUnder(bubble), ["Sentence"]));
        }
    }
}
