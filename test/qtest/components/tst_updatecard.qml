import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The card a new version raises.
 *
 * What is under test here is the card's own contract: it carries the version, the page and the
 * notes it was shown with, its cross takes it down, and it builds the three handlers those
 * three actions hang on. What each action *writes* is asserted where the document can be read
 * back afterwards -- update_pure_test.cpp drives the duty and reloads the settings file.
 */
Item {
    id: root
    width: 520
    height: 420

    Component {
        id: cardComponent
        UpdateCard {}
    }

    TestCase {
        id: testCase
        name: "UpdateCard"
        when: windowShown

        function opened() {
            const card = createTemporaryObject(cardComponent, root);
            verify(!!card, "Component exists");
            let viewed = 0;
            let skipped = 0;
            card.viewRequested.connect(function () { ++viewed; });
            card.skipRequested.connect(function () { ++skipped; });
            card.show({version: "1.2.0",
                       pageUrl: "https://github.com/kakuyo1/lens/releases/tag/v1.2.0",
                       notes: "Fixed two things."});
            wait(50);
            return {card: card,
                    viewed: function () { return viewed; },
                    skipped: function () { return skipped; }};
        }

        function test_showCarriesTheVersionThePageAndTheNotes() {
            const shown = opened();
            compare(shown.card.version, "1.2.0");
            compare(shown.card.pageUrl, "https://github.com/kakuyo1/lens/releases/tag/v1.2.0");
            compare(shown.card.notes, "Fixed two things.");
            compare(shown.card.visible, true);
            shown.card.visible = false;
        }

        function test_theCrossTakesTheCardDownWithoutAskingForAnything() {
            const shown = opened();
            shown.card.closeCard();
            tryCompare(shown.card, "visible", false);
            compare(shown.viewed(), 0, "the cross opened the release page");
            compare(shown.skipped(), 0, "the cross wrote a skip");
        }

        /// The pill carrying one of the card's action labels, pressed where a reader would press it.
        function pressLabel(card, label) {
            const text = Util.textWith(Util.textsUnder(card), [label]);
            verify(text, "no '" + label + "' label was found");
            const pill = text.parent;
            verify(pill, "the label has no button behind it");
            mouseClick(pill, pill.width / 2, pill.height / 2);
            wait(50);
        }

        function test_viewOpensThePageAndTakesTheCardDown() {
            const shown = opened();
            pressLabel(shown.card, "View");
            compare(shown.viewed(), 1);
            compare(shown.skipped(), 0);
            compare(shown.card.visible, false);
        }

        function test_skipSignalsWithoutOpeningThePage() {
            const shown = opened();
            pressLabel(shown.card, "Skip this version");
            compare(shown.skipped(), 1);
            compare(shown.viewed(), 0, "Skip opened the release page");
            compare(shown.card.visible, false);
        }

        function test_captureTheCard() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            // A card that fades and lifts into place (ShadowCard.qml), so a grab taken before
            // the token is out photographs a half-transparent surface.
            const settled = Tokens.motion.pop + 80;

            Tokens.theme = "light";
            const light = opened();
            wait(settled);
            verify(Util.saveSnapshot(testCase, light.card.contentItem, lensQaSnapshotDir, "update-card-light"));
            light.card.visible = false;

            Tokens.theme = "dark";
            const dark = opened();
            wait(settled);
            verify(Util.saveSnapshot(testCase, dark.card.contentItem, lensQaSnapshotDir, "update-card-dark"));
            dark.card.visible = false;
        }
    }
}
