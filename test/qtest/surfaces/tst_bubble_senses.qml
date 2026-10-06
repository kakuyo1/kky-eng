import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 640
    height: 600

    Component { id: bubbleComponent; Bubble {} }

    TestCase {
        id: testCase
        name: "BubbleSenses"
        when: windowShown

        function opened(count) {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const senses = [
                {en: "a business that keeps and lends money", text: "一家接受存款并提供贷款的金融机构"},
                {en: "the land beside a river", text: "河流两侧的岸边"},
                {en: "a supply kept for future use", text: "储备起来供以后使用的资源"},
                {en: "a fourth sense that must not appear", text: "not displayed"}
            ];
            bubble.show({title: "bank", type: "word", ipa: "/bæŋk/", senses: senses.slice(0, count), status: "new", x: 200, y: 500});
            tryCompare(bubble, "visible", true);
            bubble.dragging = true;
            return bubble;
        }

        function named(bubble, name) {
            return Util.findAll(bubble, function(item) { return item.objectName === name; });
        }

        function test_orderCapSeparatorsAndFirstIpa() {
            const bubble = opened(4);
            compare(bubble.senses.length, 3);
            const english = named(bubble, "senseEnglish");
            compare(english.length, 3);
            compare(english[0].text, "a business that keeps and lends money");
            compare(english[1].text, "the land beside a river");
            const separators = named(bubble, "senseSeparator").filter(function(item) { return item.visible; });
            compare(separators.length, 2);
            for (let i = 0; i < separators.length; ++i) {
                compare(separators[i].height, 1);
                compare(separators[i].color, Tokens.line2);
            }
            const ipa = Util.textsUnder(bubble).filter(function(item) { return item.visible && item.text === "/bæŋk/"; });
            compare(ipa.length, 1);
        }

        function test_moreSensesGrowHeightAndKeepWidth() {
            const one = opened(1);
            const three = opened(3);
            compare(one.width, three.width);
            verify(three.height > one.height);
            compare(named(one, "senseSeparator").filter(function(item) { return item.visible; }).length, 0);
        }

        function test_refreshClearsSensesForASentence() {
            const bubble = opened(3);
            bubble.show({title: "a sentence", type: "sentence", translation: "Una explicación clara.", x: 200, y: 500});
            compare(bubble.senses.length, 0);
            compare(bubble.translation, "Una explicación clara.");
            compare(bubble.ipa, "");
        }

        function test_snapshotBothThemes() {
            if (!lensQaSnapshotDir) skip("Set LENS_QA_SNAPSHOT_DIR to save snapshots");
            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const bubble = opened(3);
                    waitForRendering(bubble.contentItem);
                    compare(Tokens.dark, mode === "dark");
                    verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "bubble-senses-" + mode));
                    compare(named(bubble, "senseEnglish").length, 3);
                }
            } finally {
                Tokens.theme = theme;
            }
        }
    }
}
