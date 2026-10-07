import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The bubble's countdown and the pronunciation that rides beside the word.
 *
 * The clock is the view's own behaviour (Bubble.qml), so it is checked here, where the real
 * Controller is behind the surface: show() is the only way in, and the count it starts is the
 * one the reader sees. Placement and the drag are the subjects of the sibling cases and of the
 * real-surface tools (TEST.md section 5); what this file cannot see, it does not touch.
 */
Item {
    id: root
    width: 480
    height: 320

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        id: testCase
        name: "Bubble"
        when: windowShown

        /// A bubble as the controller hands it over, with its word, its pronunciation and both
        /// explanations filled in.
        function opened() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                 title: "serendipity",
                 type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                en: "the occurrence of events by chance in a happy way",
                zh: "机缘巧合",
                status: "new",
                x: 200,
                y: 240
            });
            // Short of one tick of the countdown, so the number read back is still the full one.
            wait(50);
            return bubble;
        }

        /// @return The window's own root item, which is the whole surface a reader sees.
        ///
        /// grabImage renders the item it is handed rather than the window around it -- handed
        /// the meta line it wrote a 238x26 strip -- and a Window is not an item, so the surface
        /// is named by the contentItem that holds all of it.
        function surfaceOf(bubble) {
            return bubble.contentItem;
        }

        /// @return The meta line, found by its wording rather than by its place in the column.
        function metaOf(bubble) {
            const found = Util.findAll(bubble, function (o) {
                return typeof o.text === "string" && o.text.indexOf("Disappears in") === 0;
            });
            compare(found.length, 1);
            return found[0];
        }

        /// @return The whole seconds the meta line is showing.
        function secondsOf(meta) {
            const matched = meta.text.match(/([0-9]+)s/);
            verify(matched);
            return Number(matched[1]);
        }

        /// A fresh bubble starts at its full five.
        function test_aFreshBubbleShowsTheWholeCount() {
            const bubble = opened();
            compare(metaOf(bubble).text, "Disappears in 5s");
        }

        /// The number is the clock rather than the setting that seeded it: waiting a second of
        /// real time steps it down.
        function test_theSecondsStepDownWhileTheBubbleIsUp() {
            const meta = metaOf(opened());
            compare(secondsOf(meta), 5);

            tryVerify(function () { return secondsOf(meta) < 5; }, 2000);
            verify(secondsOf(meta) >= 1);
        }

        /// Holding the bubble stops the clock where it is, and letting go carries on from there
        /// rather than refilling it to five.
        function test_holdingTheBubbleStopsTheCount() {
            const bubble = opened();
            tryVerify(function () { return bubble.remainingMs < 4000; }, 2000);

            bubble.hovering = true;
            const held = bubble.remainingMs;
            wait(600);
            compare(bubble.remainingMs, held);

            bubble.hovering = false;
            tryVerify(function () { return bubble.remainingMs < held; }, 1000);
        }

        /// The pronunciation is drawn, and it sits beside the word rather than behind it.
        function test_theWordCarriesItsPronunciation() {
            const bubble = opened();
            const ipa = Util.textWith(Util.textsUnder(bubble), ["/ˌserənˈdɪpəti/"]);
            verify(ipa);
            compare(ipa.visible, true);
        }

        /// A pointer arriving on the card is recorded, and the recording is the card rather than
        /// the window around it. The two readings are the sighting TODO.md's hover jitter needs,
        /// and this is what says the block that writes them runs without throwing.
        function test_hoveringTheCardRecordsIt() {
            const bubble = opened();
            const word = Util.textWith(Util.textsUnder(bubble), ["serendipity"]);
            verify(word);

            mouseMove(word, word.width / 2, word.height / 2);

            tryVerify(function () { return bubble.lastHoverCard.width > 0; }, 1000);
            compare(bubble.hovering, true);
            // The card, not the window: the two shadow margins are not part of it.
            compare(bubble.lastHoverCard.width, bubble.width - 2 * bubble.shadowMargin);
        }

        function test_captureTheBubble() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const bubble = opened();
            wait(100);
            verify(Util.saveSnapshot(testCase, surfaceOf(bubble), lensQaSnapshotDir, "bubble"));
        }

        /// The bubble the word list raises carries no countdown, and the line that would have
        /// carried it goes with it: a card with a gap where the seconds were reads as a card that
        /// stopped drawing. The pair of snapshots is what shows that, since the difference is the
        /// absence of a line rather than a value a case can compare.
        function test_captureTheBubbleWithoutItsCountdown() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                zh: "机缘巧合",
                status: "new",
                countsDown: false,
                x: 200,
                y: 240
            });
            wait(100);

            compare(metaOf(bubble).visible, false, "a bubble with no countdown still drew its clock");
            verify(Util.saveSnapshot(testCase, surfaceOf(bubble), lensQaSnapshotDir, "bubble-review"));
        }
    }
}
