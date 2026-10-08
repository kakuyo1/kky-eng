import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The word's origin, under the meaning it belongs to: what the card draws when there is one, and
 * what it leaves out when there is not.
 *
 * The subject is separation. A reader asked for two different things -- what the word means, and
 * where it came from -- and the card has to read as two things rather than as a longer definition
 * (UI.md 4.3). The rule, the label and the colour step are what do that, so they are what this
 * checks. Nothing here reaches the controller: whether an origin is asked for at all is the
 * setting's business and the duty's, and that pair is covered by the offline suite.
 */
Item {
    id: root
    width: 480
    height: 460

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        id: testCase
        name: "BubbleEtymology"
        when: windowShown

        /// A word bubble showing @p etymology, or none at all when it is left out.
        ///
        /// The drag flag holds the countdown, the way the other bubble cases do: the card is
        /// compared over more than one frame here, and a clock that takes it down mid-case would
        /// report a missing label rather than a missing block.
        function opened(etymology) {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const payload = {
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                zh: "机缘巧合",
                status: "new",
                x: 200,
                y: 360
            };
            if (etymology !== undefined)
                payload.etymology = etymology;
            bubble.show(payload);
            tryCompare(bubble, "visible", true);
            bubble.dragging = true;
            return bubble;
        }

        function named(bubble, name) {
            return Util.findAll(bubble, function (item) { return item.objectName === name; });
        }

        function blockOf(bubble) {
            const block = named(bubble, "etymologyBlock");
            compare(block.length, 1);
            return block[0];
        }

        function test_theOriginIsSetOffFromTheMeaningByARuleALabelAndItsColour() {
            const bubble = opened("Coined in 1754, from Horace Walpole's tale of the three princes of Serendip.");
            const block = blockOf(bubble);
            compare(block.visible, true);

            const origin = named(bubble, "etymologyText");
            compare(origin.length, 1);
            compare(origin[0].text, "Coined in 1754, from Horace Walpole's tale of the three princes of Serendip.");
            compare(origin[0].wrapMode, Text.Wrap, "a long origin has to wrap rather than run off the card");

            const label = named(bubble, "etymologyLabel");
            compare(label.length, 1);
            compare(label[0].text, "Etymology");

            const separator = named(bubble, "etymologySeparator");
            compare(separator.length, 1);
            compare(separator[0].height, 1);
            compare(separator[0].color, Tokens.line2);
            compare(separator[0].width, block.width, "the rule does not span the card");

            // The label names the sentence under it, and the sentence follows the meaning: the two
            // are siblings in the bubble's own column, so their `y` is one measure.
            const meaning = Util.textWith(Util.textsUnder(bubble), ["机缘巧合"]);
            verify(meaning);
            verify(label[0].y < origin[0].y);
            verify(block.y > meaning.y, "the origin is not drawn under the meaning");
            compare(block.x, meaning.x, "the rule starts where the meaning does, or the two columns disagree");

            // And the two read as two: the meaning is in the card's main colour, its origin one
            // step down, which is what keeps a paragraph about Latin from reading as a definition.
            compare(meaning.color, Tokens.text);
            compare(origin[0].color, Tokens.muted);
        }

        /// No origin, no block: not a label over an empty line, and no rule standing for a section
        /// that is not there. An empty slot would say the model failed rather than that this word
        /// has no origin recorded for it.
        function test_aWordWithoutAnOriginDrawsNoBlockAtAll() {
            const bubble = opened(undefined);
            compare(bubble.etymology, "");
            compare(blockOf(bubble).visible, false);
        }

        /// A refreshed card is the same card: the next word must not inherit the last one's
        /// origin, and the block goes with it.
        function test_refreshingTheCardTakesTheOriginWithIt() {
            const bubble = opened("From the Greek.");
            compare(blockOf(bubble).visible, true);
            bubble.show({title: "bank", type: "word", zh: "河岸", status: "new", x: 200, y: 360});
            compare(bubble.etymology, "");
            compare(blockOf(bubble).visible, false);
        }

        /// The card pays for what it carries: an origin is more to say, so it is more card.
        function test_anOriginMakesTheCardTallerAndNoWider() {
            const bare = opened(undefined);
            const withOrigin = opened("From the Latin serendipitas, by way of Walpole's tale of the three princes of Serendip.");
            compare(bare.width, withOrigin.width);
            verify(withOrigin.height > bare.height);
        }

        function test_snapshotBothThemes() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const bubble = opened("Coined in 1754, from Horace Walpole's tale of the three princes of Serendip.");
                    waitForRendering(bubble.contentItem);
                    verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "bubble-etymology-" + mode));
                    compare(blockOf(bubble).visible, true);
                }
            } finally {
                Tokens.theme = theme;
            }
        }
    }
}
